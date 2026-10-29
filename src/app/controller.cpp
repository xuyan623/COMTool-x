#include "controller.h"
#include "workspaces.h"
#include "api_server.h"
#include "quick/graph_geometry.h"
#include <QQmlEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QGuiApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QDrag>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMimeData>
#include <QPainterPathStroker>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <algorithm>
#include <limits>

namespace corig {

// The view bridge exposes the same observed state and checked actions used by
// official views. User QML never receives a live engine or device object.
class UserViewBridge final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap state READ state NOTIFY changed)
    Q_PROPERTY(QVariantMap config READ config NOTIFY changed)
public:
    UserViewBridge(Controller &owner, QString node, QString view) : QObject(&owner), owner_(owner), node_(std::move(node)), view_(std::move(view)) {
        connect(&owner,&Controller::viewChanged,this,[this](const QString &key) { if (key == node_ + "/" + view_) emit changed(); });
        connect(&owner,&Controller::projectChanged,this,&UserViewBridge::changed);
    }
    QVariantMap state() const { return owner_.viewState(node_,view_); }
    QVariantMap config() const { return owner_.nodeDesign(node_).value("config").toMap(); }
    Q_INVOKABLE QVariant action(const QString &name, const QVariantMap &args = {}) { return owner_.invoke("node.action",{{"node",node_},{"action",name},{"args",args}}); }
signals:
    void changed();
private:
    Controller &owner_;
    QString node_, view_;
};

Controller::Controller(QString dataDirectory, QObject *parent) : QObject(parent), dataDirectory_(QDir(dataDirectory).absolutePath()), project_(emptyProject())
{
    QDir().mkpath(dataDirectory_);
    settings_ = std::make_unique<QSettings>(dataDirectory_ + "/settings.ini",QSettings::IniFormat);
    theme_ = settings_->value("theme","dark").toString();
    python_ = std::make_unique<PythonHost>([this] { return closing_ || (engine_ && engine_->interrupted()); });
    const auto interpreter = settings_->value("python").toString();
    if (QFileInfo(interpreter).isFile()) python_->selectInterpreter(interpreter);
    io_ = std::make_unique<IoHub>();
    NodeHost host;
    host.io = [this](const QString &id, const QString &op, const QVariantMap &args) { return io_->transact(id,op,args); };
    host.python = [this](const QVariantMap &request) { return python_->call(request); };
    host.stopExtensions = [this] { python_->close(); };
    engine_ = std::make_unique<Engine>(host);
    io_->setCallbacks([this](const QString &id, const Values &values) { engine_->receive(id,values); },[this](const QString &id, const QVariantMap &state) { engine_->ioObservation(id,state); });
    connect(engine_.get(),&Engine::completed,this,[this](quint64 id, const QVariant &value, const QString &error) {
        const auto found = callbacks_.find(id);
        if (found == callbacks_.end()) return;
        const auto callback = std::move(found.value()); callbacks_.erase(found); callback(value,error);
    });
    tasks_.setMaxThreadCount(2);
    poll_.setInterval(16); viewClock_.start();
    connect(&poll_,&QTimer::timeout,this,[this] {
        const auto next = engine_->observation(); if (runtime_ != next) { runtime_ = next; emit runtimeChanged(); }
        // Only visible consumers request expensive view data. One outstanding
        // request per view bounds GUI work independently of graph frequency.
        for (auto i = viewArguments_.cbegin(); i != viewArguments_.cend(); ++i) {
            if (viewPending_.contains(i.key())) continue;
            const auto parts = i.key().split('/'); if (parts.size() != 2 || nodeDesign(parts[0]).isEmpty()) continue;
            const auto items = viewItems_.value(i.key());
            bool visible = false; for (const auto &item : items) if (item && item->property("visible").toBool()) visible = true;
            if (!visible) continue;
            const auto interval = std::clamp(nodeDesign(parts[0]).value("config").toMap().value("refresh_ms",50).toInt(),16,1000);
            if (viewClock_.elapsed() - viewRequestedAt_.value(i.key(),-1000) < interval) continue;
            viewRequestedAt_[i.key()] = viewClock_.elapsed();
            viewPending_.insert(i.key()); const auto key = i.key();
            const auto request = engine_->submit("view.read",{{"node",parts[0]},{"view",parts[1]},{"args",i.value()}});
            callbacks_.insert(request,[this,key](const QVariant &result, const QString &error) {
                viewPending_.remove(key);
                if (error.isEmpty()) { views_[key] = result.toMap(); observedViews_[key] = observedViews_.value(key).toULongLong() + 1; emit viewChanged(key); }
            });
        }
    });
    refreshGraph(); engine_->submit("project.apply",{{"project",project_}}); poll_.start();
}

Controller::~Controller() { shutdown(); }

void Controller::shutdown()
{
    if (closing_.exchange(true)) return;
    poll_.stop(); api_.reset();
    tasks_.waitForDone();
    if (engine_) engine_->shutdown();
    if (io_) io_->shutdown();
    workspaces_.reset();
    settings_->sync();
}

void Controller::attachUi(QQmlEngine *engine)
{
    qmlEngine_ = engine; engine->rootContext()->setContextProperty("App",this);
    workspaces_ = std::make_unique<Workspaces>(engine,*this);
    api_ = std::make_unique<ApiServer>(*this);
    workspaces_->open("graph");
}

QObject *Controller::workspaces() const { return workspaces_.get(); }

void Controller::tell(const QString &message, const QString &level)
{
    notice_ = message; noticeLevel_ = level; emit noticeChanged();
}

QVariant Controller::invoke(const QString &operation, const QVariantMap &arguments)
{
    try { return command(operation,arguments); }
    catch (const std::exception &error) { const auto message = QString::fromUtf8(error.what()); tell(message,"error"); return QVariantMap{{"ok",false},{"error",message}}; }
}

QVariantMap Controller::launchEngine(const QString &operation, const QVariantMap &arguments, std::function<void(const QVariant &,const QString &)> completed)
{
    const auto id = newId("job");
    jobs_[id] = {{"id",id},{"op",operation},{"state","running"}};
    const auto request = engine_->submit(operation,arguments);
    const bool execution = operation.startsWith("runtime."); if (execution) { ++runtimePending_; emit runtimeChanged(); }
    callbacks_.insert(request,[this,id,operation,execution,completed = std::move(completed)](const QVariant &value, const QString &error) {
        if (execution) { --runtimePending_; runtime_ = engine_->observation(); emit runtimeChanged(); }
        QString failure = error;
        try { if (completed) completed(value,error); } catch (const std::exception &e) { failure = QString::fromUtf8(e.what()); }
        auto published = value;
        if (operation == "node.configure") { auto map = value.toMap(); map.remove("__resources"); published = map; }
        finishJob(id,published,failure);
    });
    return {{"job",id},{"state","running"}};
}

QVariantMap Controller::launchTask(const QString &operation, std::function<QVariant()> task, std::function<void(const QVariant &,const QString &)> completed)
{
    const auto id = newId("job"); jobs_[id] = {{"id",id},{"op",operation},{"state","running"}};
    QPointer<Controller> guard(this);
    tasks_.start([guard,id,task = std::move(task),completed = std::move(completed)] {
        QVariant result; QString error;
        try { result = task(); } catch (const std::exception &e) { error = QString::fromUtf8(e.what()); }
        if (guard) QMetaObject::invokeMethod(guard,[guard,id,result,error,completed] {
            if (!guard || guard->closing_) return;
            QString failure = error;
            try { if (completed) completed(result,error); } catch (const std::exception &e) { failure = QString::fromUtf8(e.what()); }
            guard->finishJob(id,result,failure);
        },Qt::QueuedConnection);
    });
    return {{"job",id},{"state","running"}};
}

void Controller::finishJob(const QString &id, const QVariant &result, const QString &error)
{
    auto &entry = jobs_[id]; entry["state"] = error.isEmpty() ? "complete" : "failed";
    if (error.isEmpty()) entry["result"] = result; else { entry["error"] = error; tell(error,"error"); }
    // Completed results are bounded; active jobs are never evicted.
    if (jobs_.size() > 512) for (auto i = jobs_.begin(); i != jobs_.end() && jobs_.size() > 384;) {
        if (i.key() != id && i.value().value("state") != "running") i = jobs_.erase(i); else ++i;
    }
    emit jobFinished(id);
}

QVariantMap Controller::job(const QString &id) const
{
    if (!jobs_.contains(id)) return {{"id",id},{"state","missing"},{"error","任务不存在或结果已过期"}};
    return jobs_.value(id);
}

void Controller::requireEditable(bool layout) const
{
    if (readOnly_) throw Error("工程处于只读模式，请先进入编辑模式");
    if (busy_) throw Error("工程操作尚未完成");
    if (!layout && engine_->observation().value("running").toBool()) throw Error("请先暂停运行再修改功能设计");
}

void Controller::refreshGraph() { graphNodes_ = graphLayout(project_,catalog_); }

void Controller::adopt(const QVariantMap &project, bool dirty, const QString &selection)
{
    project_ = project; dirty_ = dirty;
    catalog_.clearPython();
    for (const auto &manifest : project_.value("user_plugins").toList()) catalog_.addPython(manifest.toMap());
    refreshGraph();
    if (!selection.isEmpty()) {
        selected_ = selection; selectedKind_ = nodeDesign(selection).isEmpty() ? uiItem(selection).isEmpty() ? "wire" : "ui" : "node";
        selectedNodes_ = selectedKind_ == "node" ? QStringList{selection} : QStringList{}; emit selectionChanged();
    }
    emit catalogChanged(); emit projectChanged();
}

QVariantMap Controller::commit(EditResult edit, const QString &label, bool remember, bool runtimeOnly)
{
    const auto old = project_;
    const auto accepted = [this,edit,old,remember,runtimeOnly,label](const QVariant &, const QString &error) {
        busy_ = false; emit busyChanged();
        if (!error.isEmpty()) return;
        if (remember && !runtimeOnly) { undo_.append(old); if (undo_.size() > 100) undo_.removeFirst(); redo_.clear(); }
        if (!runtimeOnly) adopt(edit.project,true,edit.selected);
        tell(label);
    };
    if (edit.engineChanged) {
        busy_ = true; emit busyChanged();
        return launchEngine("project.apply",{{"project",edit.project}},accepted);
    }
    accepted({},{}); return {{"result",edit.result},{"updated",true}};
}

QVariantMap Controller::appState() const
{
    return {{"application","CoRig"},{"version","0.3.0"},{"project",project_.value("name")},{"path",projectPath_},{"dirty",dirty_},{"read_only",readOnly_},
        {"busy",busy_},{"runtime",engine_->observation()},{"workspaces",workspaces_ ? workspaces_->state() : QVariantMap{}},
        {"python",pythonPath()},{"theme",theme_},{"view_observations",observedViews_},{"selection",QVariantMap{{"id",selected_},{"kind",selectedKind_},{"nodes",selectedNodes_}}}};
}

QVariantMap Controller::nodeDesign(const QString &id) const
{
    for (const auto &item : project_.value("nodes").toList()) if (item.toMap().value("id") == id) return item.toMap();
    return {};
}
QVariantMap Controller::nodeState(const QString &id) const { return runtime_.value("nodes").toMap().value(id).toMap(); }
QVariantMap Controller::definition(const QString &id) const { try { return catalog_.definition(id); } catch (const Error &) { return {}; } }
QVariantMap Controller::uiItem(const QString &id) const
{
    for (const auto &item : project_.value("ui").toMap().value("items").toList()) if (item.toMap().value("id") == id) return item.toMap();
    return {};
}

QVariantMap Controller::inspector() const
{
    if (selectedKind_ == "ui") { const auto item = uiItem(selected_); return {{"item",item},{"values",item.value("properties")},{"schema",uiPropertySchema(item,catalog_,project_)}}; }
    if (selectedKind_ == "wire") for (const auto &item : project_.value("wires").toList()) if (item.toMap().value("id") == selected_)
        return {{"item",item},{"values",item},{"schema",QVariantMap{{"color",QVariantMap{{"type","string"}}},{"width",QVariantMap{{"type","number"},{"min",.5},{"max",10}}},
            {"routing",QVariantMap{{"type","string"},{"choices",QStringList{"curve","polyline"}},{"choice_labels",QVariantMap{{"curve","平滑曲线"},{"polyline","折线"}}}}},
            {"points",QVariantMap{{"type","json"},{"label","路径点"},{"format","points"}}}}}};
    const auto node = nodeDesign(selected_); if (node.isEmpty()) return {};
    return {{"item",node},{"schema",definition(node.value("plugin").toString()).value("properties")},{"values",readOnly_ ? nodeState(selected_).value("config",node.value("config")) : node.value("config")},{"runtime",nodeState(selected_)}};
}

QVariantMap Controller::libraryPreferences() const
{
    return {{"favorites",settings_->value("library/favorites",QStringList{})},{"recent",settings_->value("library/recent",QStringList{})},
        {"view",settings_->value("library/view","grid")},{"size",settings_->value("library/size",1.)}};
}
QVariantList Controller::catalogEntries(const QString &query, const QStringList &category, const QString &scope) const
{
    auto result = catalog_.search(query,category);
    const auto preferences = libraryPreferences();
    const auto filter = preferences.value(scope == "favorites" ? "favorites" : "recent").toStringList();
    if (scope != "all") result.erase(std::remove_if(result.begin(),result.end(),[&](const QVariant &v) { return !filter.contains(v.toMap().value("id").toString()); }),result.end());
    if (scope == "recent") std::stable_sort(result.begin(),result.end(),[&](const QVariant &a, const QVariant &b) { return filter.indexOf(a.toMap().value("id").toString()) < filter.indexOf(b.toMap().value("id").toString()); });
    return result;
}

QVariantMap Controller::previewGeometry(const QString &plugin, const QVariantMap &config) const { return nodeGeometry({{"plugin",plugin},{"config",catalog_.checkedConfig(plugin,config)}},catalog_); }

void Controller::openLibrary(const QString &page)
{
    if (page != "graph" && page != "layout") return;
    libraryTarget_ = page; emit preferencesChanged();
    if (workspaces_) workspaces_->open("library");
}

void Controller::dragObject(const QString &page, const QString &item)
{
    QDrag drag(this); auto mime = new QMimeData;
    mime->setData("application/x-corig-object",QJsonDocument::fromVariant(QVariantMap{{"page",page},{"id",item}}).toJson(QJsonDocument::Compact));
    drag.setMimeData(mime); drag.exec(Qt::MoveAction);
}

QVariantMap Controller::connectionOptions(const QString &node, const QString &port) const
{
    QVariantMap result; QString type; QSet<QString> occupied; QHash<QString,QStringList> outgoing;
    for (const auto &entry : graphNodes_) if (entry.toMap().value("id") == node)
        for (const auto &value : entry.toMap().value("geometry").toMap().value("ports").toList()) if (value.toMap().value("id") == port && value.toMap().value("direction") == "out") type = value.toMap().value("data_type").toString();
    if (type.isEmpty()) return result;
    for (const auto &entry : project_.value("wires").toList()) { const auto w = entry.toMap(); const auto a = w.value("source").toList(), b = w.value("target").toList(); occupied.insert(a[0].toString()+"/"+a[1].toString()); occupied.insert(b[0].toString()+"/"+b[1].toString());
        if (!catalog_.definition(nodeDesign(b[0].toString()).value("plugin").toString()).value("breaks_cycle").toBool()) outgoing[a[0].toString()].append(b[0].toString());
    }
    // The same cycle-breaking declaration used by executionOrder governs the
    // preview. Walk descendants once per target, never mutate a trial project.
    for (const auto &entry : graphNodes_) { const auto target = entry.toMap(); const auto id = target.value("id").toString(); QSet<QString> reached;
        std::function<void(QString)> visit = [&](const QString &key) { if (reached.contains(key)) return; reached.insert(key); for (const auto &next : outgoing.value(key)) visit(next); }; visit(id);
        const bool cycle = reached.contains(node) && !catalog_.definition(target.value("plugin").toString()).value("breaks_cycle").toBool();
        for (const auto &value : target.value("geometry").toMap().value("ports").toList()) { const auto p = value.toMap(); const auto key = id+"/"+p.value("id").toString(); QString reason;
            if (p.value("direction") != "in") reason = "方向相同，需要输入凹口";
            else if (occupied.contains(key)) reason = "端口已占用，请先取出连接囊泡";
            else if (type != p.value("data_type") && type != "any" && p.value("data_type") != "any") reason = "类型不符：" + type + " / " + p.value("data_type").toString();
            else if (occupied.contains(node+"/"+port)) reason = "输出端口已占用";
            else if (cycle) reason = "连接会形成同步环路";
            result.insert(key,QVariantMap{{"compatible",reason.isEmpty()},{"reason",reason}});
        }
    }
    return result;
}

QString Controller::hitWire(double x, double y, double scale) const
{
    QHash<QString,QVariantMap> nodes, geometries;
    for (const auto &entry : graphNodes_) { const auto n = entry.toMap(); nodes.insert(n.value("id").toString(),n); geometries.insert(n.value("id").toString(),n.value("geometry").toMap()); }
    QPainterPathStroker stroke; stroke.setWidth(12. / std::max(.05,scale));
    for (const auto &entry : project_.value("wires").toList()) { const auto wire = entry.toMap(); if (stroke.createStroke(wirePath(wire,nodes,geometries)).contains({x,y})) return wire.value("id").toString(); }
    return {};
}
QVariantMap Controller::hitPort(double x, double y, double scale) const
{
    for (const auto &entry : graphNodes_) {
        const auto node = entry.toMap(); const auto geometry = node.value("geometry").toMap();
        for (const auto &p : geometry.value("ports").toList()) { auto port = p.toMap(); const auto point = portPosition(node,geometry,port.value("id").toString());
            if (QLineF({x,y},point).length() <= 12. / std::max(.05,scale)) { port["node"] = node.value("id"); port["world_x"] = point.x(); port["world_y"] = point.y(); return port; }
        }
    }
    return {};
}
QVariantMap Controller::wireEditState(const QString &id, double x, double y) const
{
    for (const auto &entry : project_.value("wires").toList()) if (entry.toMap().value("id") == id) {
        const auto wire = entry.toMap(); const auto points = wire.value("points").toList(); QVariantList handles;
        for (qsizetype i = 0; i < points.size(); ++i) handles.append(QVariantMap{{"index",i},{"insertion",false},{"position",points[i]}});
        QHash<QString,QVariantMap> nodes, layouts;
        for (const auto &nodeEntry : graphNodes_) { const auto node = nodeEntry.toMap(); nodes[node.value("id").toString()] = node; layouts[node.value("id").toString()] = node.value("geometry").toMap(); }
        QVector<QPainterPath> segments; wirePath(wire,nodes,layouts,&segments);
        double nearest = std::numeric_limits<double>::infinity(); qsizetype insertion = 0;
        for (qsizetype i = 0; i < segments.size(); ++i) {
            const auto midpoint = segments[i].pointAtPercent(.5);
            handles.append(QVariantMap{{"index",i},{"insertion",true},{"position",QVariantList{midpoint.x(),midpoint.y()}}});
            for (int sample = 0; sample <= 32; ++sample) { const auto distance = QLineF(QPointF(x,y),segments[i].pointAtPercent(sample / 32.)).length(); if (distance < nearest) { nearest = distance; insertion = i; } }
        }
        return {{"id",id},{"points",points},{"handles",handles},{"routing",wire.value("routing","curve")},{"insertion_index",insertion}};
    }
    return {};
}

void Controller::dragPlugin(const QString &plugin)
{
    try { if (!QStringList{"layout.row","layout.column","layout.grid","layout.tabs"}.contains(plugin)) catalog_.definition(plugin); QDrag drag(this); auto mime = new QMimeData; mime->setData("application/x-corig-plugin",plugin.toUtf8()); drag.setMimeData(mime); drag.exec(Qt::CopyAction); }
    catch (const Error &e) { tell(e.message,"error"); }
}
void Controller::dragUiItem(const QString &item)
{
    if (uiItem(item).isEmpty()) return;
    QDrag drag(this); auto mime = new QMimeData; mime->setData("application/x-corig-ui",item.toUtf8()); drag.setMimeData(mime); drag.exec(Qt::MoveAction);
}

void Controller::observeView(const QString &node, const QString &view, const QVariantMap &arguments) { viewArguments_[node + "/" + view] = arguments; }
QVariantMap Controller::viewState(const QString &node, const QString &view) const { return views_.value(node + "/" + view); }
void Controller::registerView(QObject *item, const QString &node, const QString &view)
{
    const auto key = node + "/" + view;
    // A reused loader can change node identity without destroying its item.
    for (auto i = viewItems_.begin(); i != viewItems_.end();) {
        if (i.key() != key) i.value().removeAll(item);
        if (i.value().isEmpty()) { viewArguments_.remove(i.key()); viewRequestedAt_.remove(i.key()); i = viewItems_.erase(i); } else ++i;
    }
    if (viewItems_.value(key).contains(item)) return;
    viewItems_[key].append(item); observeView(node,view);
    connect(item,&QObject::destroyed,this,[this,key] {
        auto &items = viewItems_[key]; items.erase(std::remove_if(items.begin(),items.end(),[](const auto &item) { return item.isNull(); }),items.end());
        if (items.isEmpty()) { viewItems_.remove(key); viewArguments_.remove(key); viewRequestedAt_.remove(key); }
    });
}

QVariantMap Controller::importPlugin(const QString &path) const
{
    QFile file(QUrl(path).isLocalFile() ? QUrl(path).toLocalFile() : path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 16 * 1024 * 1024) throw Error("插件声明无法读取或超过 16 MiB");
    QJsonParseError parse; auto manifest = QJsonDocument::fromJson(file.readAll(),&parse).toVariant().toMap();
    if (parse.error != QJsonParseError::NoError) throw Error("插件声明需要 JSON：" + parse.errorString());
    const QDir directory(QFileInfo(file).absolutePath());
    const auto codeFile = manifest.take("code_file").toString();
    if (!codeFile.isEmpty()) { QFile code(directory.filePath(codeFile)); if (!code.open(QIODevice::ReadOnly)) throw Error("Python 插件代码无法读取"); manifest["code"] = QString::fromUtf8(code.readAll()); }
    auto resources = manifest.value("resources").toMap();
    for (const auto &name : manifest.take("resource_files").toStringList()) {
        if (QDir::isAbsolutePath(name) || QDir::cleanPath(name).startsWith("..")) throw Error("插件资源必须在声明所在目录内");
        QFile source(directory.filePath(name)); if (!source.open(QIODevice::ReadOnly) || source.size() > 16 * 1024 * 1024) throw Error("插件资源无法读取或过大：" + name);
        resources[name] = source.readAll();
    }
    manifest["resources"] = resources; return manifest;
}

QString Controller::userViewUrl(const QString &node, const QString &view)
{
    try {
        const auto spec = catalog_.definition(nodeDesign(node).value("plugin").toString());
        if (spec.value("builtin").toBool()) return {};
        const auto key = spec.value("id").toString();
        const auto resource = spec.value("views").toMap().value(view).toMap().value("qml").toString();
        if (resource.isEmpty()) throw Error("用户视图缺少 qml 资源声明");
        if (!pluginDirectories_.contains(key)) {
            auto directory = std::make_shared<QTemporaryDir>(); if (!directory->isValid()) throw Error("无法展开用户视图资源");
            const auto resources = spec.value("resources").toMap();
            for (auto i = resources.cbegin(); i != resources.cend(); ++i) {
                const auto relative = QDir::cleanPath(i.key());
                if (QDir::isAbsolutePath(relative) || relative.startsWith("..")) throw Error("用户资源路径越出插件目录");
                const auto target = directory->filePath(relative); QDir().mkpath(QFileInfo(target).absolutePath());
                QSaveFile output(target); if (!output.open(QIODevice::WriteOnly)) throw Error("用户资源无法展开");
                const auto bytes = i.value().metaType().id() == QMetaType::QByteArray ? i.value().toByteArray() : i.value().toString().toUtf8();
                if (output.write(bytes) != bytes.size() || !output.commit()) throw Error("用户资源写入失败");
            }
            pluginDirectories_[key] = directory;
        }
        const auto relative = QDir::cleanPath(resource);
        if (QDir::isAbsolutePath(relative) || relative.startsWith("..")) throw Error("用户视图路径无效");
        const auto path = pluginDirectories_[key]->filePath(relative);
        if (!QFileInfo(path).isFile()) throw Error("用户视图资源不存在：" + resource);
        return QUrl::fromLocalFile(path).toString();
    } catch (const Error &e) { tell(e.message,"error"); return {}; }
}
QObject *Controller::userViewBridge(const QString &node, const QString &view)
{
    const auto key = node + "/" + view;
    if (!viewBridges_.value(key)) viewBridges_[key] = new UserViewBridge(*this,node,view);
    return viewBridges_[key];
}

bool Controller::confirmExit(const QString &window, bool discard)
{
    if (busy_) { tell("请等待工程操作完成后退出","warning"); return false; }
    if (dirty_ && !discard && !exitDiscarded_) { emit exitConfirmation(window); return false; }
    QTimer::singleShot(50,qApp,&QCoreApplication::quit); return true;
}

} // namespace corig

#include "controller.moc"
