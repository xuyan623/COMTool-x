#include "controller.h"
#include "workspaces.h"
#include "quick/graph_geometry.h"
#include "updates.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QQuickWindow>
#include <QSaveFile>
#include <QUrl>
#include <QRegularExpression>
#include <QClipboard>
#include <QGuiApplication>
#include <QDesktopServices>
#include <QProcess>
#include <algorithm>

namespace corig {

// Discovery lists the real command surface. Node/view-specific schemas stay
// with their declarations, so adding a plugin cannot widen unrelated commands.
QVariantMap Controller::schema() const
{
    const QMap<QString,QStringList> fields{
        {"app.state",{}},{"app.quit",{"discard"}},{"app.restart",{"discard","path","plugin_selections"}},{"preferences.set",{"theme","python","read_only"}},
        {"update.state",{}},{"update.check",{}},{"update.install",{}},
        {"registry",{}},{"schema",{}},{"project.get",{}},{"project.validate",{}},{"project.new",{"name","discard"}},
        {"project.open",{"path","discard","plugin_selections"}},{"project.save",{"path","include_plugins"}},
        {"project.export",{"path","include_plugins"}},{"project.options",{"include_plugins"}},{"project.rename",{"name"}},
        {"history.undo",{}},{"history.redo",{}},{"batch",{"commands"}},
        {"graph.node.add",{"id","plugin","title","config","position","orientation","placed"}},
        {"graph.node.remove",{"id","ids"}},{"graph.node.move",{"id","position"}},{"graph.node.rotate",{"id","degrees"}},
        {"graph.node.patch",{"id","changes"}},{"graph.wire.add",{"id","source","target","routing","color","width","points"}},
        {"graph.wire.remove",{"id"}},{"graph.wire.patch",{"id","changes"}},{"graph.select",{"id","kind","ids"}},{"graph.fit",{}},{"graph.overlaps",{}},
        {"graph.wire.handles",{"id","x","y"}},{"graph.activity",{}},
        {"graph.connection.options",{"node","port"}},
        {"node.configure",{"node","changes","persist"}},{"node.state",{"node","detailed"}},{"node.action",{"node","action","args"}},
        {"runtime.state",{}},{"runtime.start",{"debug"}},{"runtime.debug",{"enabled"}},{"runtime.pause",{}},{"runtime.stop",{}},{"runtime.reset",{}},{"runtime.step",{"mode","source"}},
        {"runtime.emit",{"node","outputs"}},{"log.query",{"after","limit","node","level","text","batch"}},{"log.clear",{}},{"log.configure",{"detail"}},
        {"log.export",{"path","node","level","text","batch"}},{"io.ports",{}},{"clipboard.read",{}},
        {"ui.update",{}},{"ui.container.add",{"id","parent","plugin","properties"}},{"ui.view.add",{"id","parent","node","view","properties"}},
        {"ui.item.move",{"id","parent","index"}},{"ui.item.remove",{"id"}},{"ui.item.patch",{"id","properties"}},
        {"ui.item.schema",{"id"}},{"ui.view.bind",{"id","node","view"}},
        {"editor.state",{"page"}},{"editor.configure",{"page","zoom","pan_x","pan_y","left_width","right_width","fit","animation"}},
        {"editor.add",{"id","page","plugin"}},{"editor.place",{"page","id","ids","position","parent","index"}},
        {"editor.withdraw",{"page","id","ids"}},{"editor.remove",{"page","id","ids","confirmed","revision"}},
        {"editor.replace",{"page","id","ids","plugin","confirmed","revision"}},{"editor.reorder",{"page","id","index"}},
        {"editor.copy",{"page","ids"}},{"editor.paste",{"page","payload"}},{"editor.plan",{"page","ids","operation","plugin"}},
        {"sync.plan",{"direction"}},{"sync.apply",{"direction","keys","revision"}},
        {"library.search",{"query","category","scope"}},{"library.state",{}},{"library.favorite",{"plugin","enabled"}},
        {"library.configure",{"view","size"}},{"library.preview",{"plugin"}},{"plugin.import",{"path","manifest"}},
        {"plugin.state",{}},{"plugin.tree",{"scope"}},{"plugin.settings",{}},{"plugin.directory",{}},{"plugin.enable",{"plugin","enabled"}},
        {"plugin.export",{"plugin","path","plugins","directory","overwrite","scope"}},{"plugin.export.dialog",{"scope","plugin"}},
        {"plugin.remove",{"plugin","package"}},{"python.probe",{"dependencies"}},{"job.get",{"id"}},
        {"workspace.open",{"page","window"}},{"workspace.close",{"page","discard"}},{"workspace.detach",{"page","x","y"}},
        {"workspace.move",{"page","window","index"}},{"window.close",{"window","discard"}},{"window.pin",{"window","enabled"}},
        {"window.configure",{"window","x","y","width","height","state","focus"}},
        {"view.read",{"node","view","args"}},{"view.actions",{"node","view"}},{"view.call",{"node","view","action","args"}},
        {"controls.list",{"page"}},{"controls.get",{"id","page"}},{"controls.set",{"id","page","property","value"}},
        {"controls.click",{"id","page"}},{"controls.focus",{"id","page"}},{"controls.commit",{"id","page"}},
        {"controls.key",{"id","page","key","text","modifiers"}},
        {"controls.pointer",{"id","page","type","x","y","button","buttons","delta_x","delta_y","modifiers"}},
        {"capture",{"target","path","width","height"}}
    };
    QVariantMap commands;
    for (auto i = fields.cbegin(); i != fields.cend(); ++i) commands.insert(i.key(),QVariantMap{{"arguments",i.value()}});
    return {{"application","CoRig"},{"api_version",3},{"version","0.4.0"},{"commands",commands},{"async","A job is complete only after its operation succeeds; poll job.get"},
        {"values",QVariantMap{{"bytes","{type:bytes,base64:...,length:N}"},{"samples","{time:seconds,values:{name:number}}"}}}};
}

QVariant Controller::command(const QString &op, const QVariantMap &args)
{
    const auto commands = schema().value("commands").toMap();
    if (!commands.contains(op)) throw Error("未知操作：" + op);
    const auto fields = commands.value(op).toMap().value("arguments").toStringList();
    for (auto i = args.cbegin(); i != args.cend(); ++i) if (!fields.contains(i.key())) throw Error("操作 " + op + " 不接受参数：" + i.key());
    if (op == "schema") return schema();
    if (op == "app.state") return appState();
    if (op == "registry") return registry();
    if (op == "job.get") return job(args.value("id").toString());
    if (op == "runtime.state") return engine_->observation();
    if (op == "project.get") return project_;
    if (op == "plugin.state") return pluginState();
    if (op == "plugin.tree") return plugins_->tree(project_,args.value("scope","project").toString());
    if (op == "plugin.export.dialog") { emit pluginExportRequested(args.value("scope","project").toString(),args.value("plugin").toString()); return QVariantMap{{"opened",true}}; }
    if (op == "plugin.directory") { if (!QDesktopServices::openUrl(QUrl::fromLocalFile(pluginState().value("directory").toString()))) throw Error("无法打开插件文件夹"); return QVariantMap{{"opened",true}}; }
    if (op == "plugin.settings") { emit pluginSettingsRequested(); return QVariantMap{{"opened",true}}; }
    if (op == "update.state") return QVariantMap{{"supported",!maintenanceTool().isEmpty()},{"version","0.4.0"}};
    if (op == "update.check") return launchTask(op,[this] { return checkUpdates([this] { return closing_.load(); }); });
    if (op == "update.install") {
        if (dirty_) throw Error("请先保存当前工程，再开始更新");
        if (busy_) throw Error("请等待当前工程操作完成");
        const auto tool = maintenanceTool(); if (tool.isEmpty()) throw Error("请使用安装版 CoRig 进行安装式更新");
        if (!QProcess::startDetached(tool,{"--start-updater"})) throw Error("无法启动安装式更新");
        exitDiscarded_ = true; QTimer::singleShot(50,qApp,&QCoreApplication::quit);
        return QVariantMap{{"updating",true}};
    }
    if (op == "graph.connection.options") {
        const auto node = args.value("node").toString(), port = args.value("port").toString();
        if (nodeDesign(node).isEmpty()) throw Error("节点不存在：" + node);
        const auto geometry = previewGeometry(nodeDesign(node).value("plugin").toString(),nodeDesign(node).value("config").toMap());
        bool found = false;
        for (const auto &entry : geometry.value("ports").toList()) found |= entry.toMap().value("id") == port;
        if (!found) throw Error("端口不存在：" + port);
        return connectionOptions(node,port);
    }
    if (op == "ui.item.schema") {
        const auto item = uiItem(args.value("id").toString());
        if (item.isEmpty()) throw Error("界面对象不存在");
        return uiPropertySchema(item,catalog_,project_);
    }
    if (op == "editor.state" || op == "editor.configure") return editorView(op,args);
    if (op == "window.configure") return configureWindow(args);
    if (op == "sync.plan") return synchronizationPlan(project_,catalog_,args.value("direction").toString());
    if (op == "editor.plan") return editorImpact(project_,catalog_,args);
    if (op == "editor.copy") {
        const auto payload = copyEditorObjects(project_,args.value("page","graph").toString(),args.value("ids").toStringList());
        QGuiApplication::clipboard()->setText(QString::fromUtf8(QJsonDocument::fromVariant(payload).toJson(QJsonDocument::Compact)));
        return QVariantMap{{"copied",args.value("ids")}};
    }
    if (op == "clipboard.read") return QVariantMap{{"text",QGuiApplication::clipboard()->text()}};
    if (op == "io.ports") { const auto package = catalog_.servicePackage("io"); return launchTask(op,[this,package] { return pluginService(package,{},"list_ports",{}); }); }
    if (op == "project.validate") { validateProject(project_,catalog_); return QVariantMap{{"valid",true}}; }
    if (op == "preferences.set") {
        if (args.contains("theme")) { const auto theme = args.value("theme").toString(); if (theme != "dark" && theme != "light") throw Error("主题需要 dark 或 light"); theme_ = theme; settings_->setValue("theme",theme); }
        if (args.contains("python")) { if (engine_->observation().value("active").toBool()) throw Error("请先停止会话再切换 Python 环境"); python_->selectInterpreter(args.value("python").toString()); settings_->setValue("python",pythonPath()); }
        if (args.contains("read_only")) readOnly_ = args.value("read_only").toBool();
        emit preferencesChanged(); return appState();
    }
    if (op == "app.quit") {
        const bool discard = args.value("discard",false).toBool();
        if (dirty_ && !discard) throw Error("工程有未保存修改，请先保存或明确丢弃");
        if (busy_) throw Error("请等待工程操作完成");
        exitDiscarded_ = true;
        QTimer::singleShot(50,qApp,&QCoreApplication::quit); return QVariantMap{{"quitting",true}};
    }
    if (op == "app.restart") {
        if (dirty_ && !args.value("discard").toBool()) throw Error("请先保存当前工程，再重启 CoRig");
        if (busy_) throw Error("请等待工程操作完成");
        QStringList arguments{"--data-dir",dataDirectory_};
        const auto path = args.value("path",projectPath_).toString();
        if (!path.isEmpty()) arguments.append(path);
        if (args.contains("plugin_selections")) arguments << "--plugin-selections" << QString::fromLatin1(QJsonDocument::fromVariant(args.value("plugin_selections")).toJson(QJsonDocument::Compact).toBase64());
        if (!QProcess::startDetached(QCoreApplication::applicationFilePath(),arguments)) throw Error("无法重新启动 CoRig");
        exitDiscarded_ = true; QTimer::singleShot(50,qApp,&QCoreApplication::quit);
        return QVariantMap{{"restarting",true}};
    }
    if (op == "project.open" || op == "project.new") {
        requireEditable();
        if (dirty_ && !args.value("discard").toBool()) throw Error("请先保存当前工程，或明确丢弃修改");
        auto path = args.value("path").toString(); if (QUrl(path).isLocalFile()) path = QUrl(path).toLocalFile();
        if (op == "project.open" && path.isEmpty()) throw Error("请选择工程文件");
        busy_ = true; emit busyChanged();
        QVariantMap request = op == "project.open" ? QVariantMap{{"path",path},{"plugin_selections",args.value("plugin_selections",QVariantMap{})}} : QVariantMap{{"project",emptyProject(args.value("name","未命名工程").toString())}};
        return launchEngine(op == "project.open" ? "project.load" : "project.replace",request,
            [this,path,args](const QVariant &result, const QString &error) {
                busy_ = false; emit busyChanged(); if (!error.isEmpty()) return;
                const auto readiness = result.toMap();
                if (readiness.value("requires_plugin_selection").toBool() || readiness.value("requires_restart").toBool() || readiness.value("requires_plugins").toBool()) {
                    pendingPluginOpen_ = readiness; pendingPluginOpen_["path"] = path;
                    pendingPluginOpen_["plugin_selections"] = args.value("plugin_selections",QVariantMap{});
                    emit pluginOpenRequested(); return;
                }
                pendingPluginOpen_.clear();
                projectPath_ = path; undo_.clear(); redo_.clear(); views_.clear();
                // Qt can reuse layout items when another project has the same
                // item IDs. Keep their subscriptions, reset per-project ranges.
                for (auto &arguments : viewArguments_) arguments.clear();
                pluginDirectories_.clear(); selected_.clear(); selectedNodes_.clear(); emit selectionChanged(); adopt(result.toMap(),false); tell("工程已打开");
            });
    }
    if (op == "project.options") {
        if (args.contains("include_plugins")) {
            requireEditable(true);
            if (args.value("include_plugins").metaType().id() != QMetaType::Bool) throw Error("include_plugins 需要布尔值");
            auto next = project_; next["save_options"] = QVariantMap{{"include_plugins",args.value("include_plugins")}};
            return commit({next,{}, {},false},"工程保存选项已修改");
        }
        return project_.value("save_options",QVariantMap{{"include_plugins",true}});
    }
    if (op == "project.save" || op == "project.export") {
        if (busy_) throw Error("工程操作尚未完成");
        const bool exporting = op == "project.export";
        const auto includeValue = args.value("include_plugins",project_.value("save_options").toMap().value("include_plugins",true));
        if (includeValue.metaType().id() != QMetaType::Bool) throw Error("include_plugins 需要布尔值");
        const bool include = includeValue.toBool();
        auto path = args.value("path",projectPath_).toString(); if (QUrl(path).isLocalFile()) path = QUrl(path).toLocalFile();
        if (exporting && !args.contains("path")) throw Error("请选择工程导出路径");
        if (path.isEmpty()) path = dataDirectory_ + "/project/" + project_.value("name").toString().replace(QRegularExpression("[\\\\/:*?\"<>|]"),"_") + ".crpj";
        if (path.endsWith(".corig",Qt::CaseInsensitive)) path.chop(6);
        if (!path.endsWith(".crpj",Qt::CaseInsensitive)) path += ".crpj";
        path = QFileInfo(path).absoluteFilePath(); const auto design = project_, snapshot = catalog_.embed(project_,include), loaded = catalog_.embed(project_,true);
        busy_ = true; emit busyChanged();
        return launchTask(op,[path,snapshot,include] { QDir().mkpath(QFileInfo(path).absolutePath()); saveProject(path,snapshot); return QVariantMap{{"path",path},{"include_plugins",include},{"dependencies",snapshot.value("plugin_dependencies")}}; },
            [this,path,design,snapshot,loaded,exporting](const QVariant &, const QString &error) {
                busy_ = false; emit busyChanged(); if (!error.isEmpty()) return;
                if (!exporting) {
                    projectPath_ = path;
                    if (project_ == design) {
                        const auto choices = project_.value("plugin_selections"); project_ = loaded;
                        project_["save_options"] = snapshot.value("save_options");
                        if (choices.isValid()) project_["plugin_selections"] = choices;
                        dirty_ = false;
                    }
                    emit projectChanged();
                }
                tell(exporting ? "工程已导出" : "工程已保存");
            });
    }
    if (op == "project.rename") { requireEditable(true); auto next = project_; const auto name = args.value("name").toString().trimmed(); if (name.isEmpty()) throw Error("工程名称不能为空"); next["name"] = name; return commit({next,{}, {},false},"工程已重命名"); }
    if (op == "history.undo" || op == "history.redo") {
        requireEditable(); auto &source = op == "history.undo" ? undo_ : redo_; if (source.isEmpty()) throw Error("没有可" + QString(op == "history.undo" ? "撤销" : "重做") + "的操作");
        const auto candidate = source.last(), previous = project_; busy_ = true; emit busyChanged();
        return launchEngine("project.apply",{{"project",candidate}},[this,op,candidate,previous](const QVariant &, const QString &error) {
            busy_ = false; emit busyChanged(); if (!error.isEmpty()) return;
            auto &from = op == "history.undo" ? undo_ : redo_; auto &to = op == "history.undo" ? redo_ : undo_; from.removeLast(); to.append(previous); adopt(candidate,true);
        });
    }
    if (op == "graph.select") {
        selected_ = args.value("id").toString(); selectedKind_ = args.value("kind","node").toString(); selectedNodes_ = args.value("ids").toStringList();
        if (selectedKind_ == "node" && selectedNodes_.isEmpty() && !selected_.isEmpty()) selectedNodes_.append(selected_);
        emit selectionChanged(); return QVariantMap{{"id",selected_},{"kind",selectedKind_},{"ids",selectedNodes_}};
    }
    if (op == "graph.fit") { if (workspaces_) workspaces_->open("graph"); emit graphFocusRequested({}); return QVariantMap{{"fitted",true}}; }
    if (op == "graph.activity") {
        const auto page = workspaces_ ? workspaces_->page("graph") : nullptr;
        const auto renderer = page ? page->property("wireRenderer").value<QObject *>() : nullptr;
        QVariantMap result;
        if (!renderer || !QMetaObject::invokeMethod(renderer,"activityState",Q_RETURN_ARG(QVariantMap,result))) throw Error("功能设计视图尚未打开");
        return result;
    }
    if (op == "graph.overlaps") { QVariantList overlaps; for (const auto &entry : graphNodes_) if (entry.toMap().value("overlap").toBool()) overlaps.append(entry.toMap().value("id")); return overlaps; }
    if (op == "graph.wire.handles") return wireEditState(args.value("id").toString(),args.value("x",0.).toDouble(),args.value("y",0.).toDouble());
    if (op == "node.configure") {
        const auto id = args.value("node").toString(); const auto design = nodeDesign(id); if (design.isEmpty()) throw Error("节点不存在：" + id);
        const bool running = engine_->observation().value("running").toBool(); const bool persist = args.value("persist",!readOnly_).toBool();
        if (running || !persist) {
            if (busy_) throw Error("工程操作尚未完成");
            if (persist && readOnly_) throw Error("只读模式不能保存运行参数到设计");
            const auto prepared = editProject(project_,catalog_,op,args); auto changes = args.value("changes").toMap();
            QVariantMap normalized;
            for (const auto &entry : prepared.project.value("nodes").toList()) if (entry.toMap().value("id") == id) normalized = entry.toMap();
            const auto checked = normalized.value("config").toMap(); for (auto i = changes.begin(); i != changes.end(); ++i) i.value() = checked.value(i.key());
            return launchEngine(op,{{"node",id},{"design",normalized},{"changes",changes},{"resources",prepared.project.value("resources")}},[this,id,persist](const QVariant &result, const QString &error) {
                if (!error.isEmpty()) return;
                const auto config = result.toMap().value("config");
                if (persist) { auto next = project_; auto nodes = next.value("nodes").toList(); for (auto &entry : nodes) { auto item = entry.toMap(); if (item.value("id") == id) { item["config"] = config; entry = item; } } next["nodes"] = nodes; next["resources"] = result.toMap().value("__resources"); undo_.append(project_); redo_.clear(); adopt(next,true,id); }
            });
        }
        requireEditable(); return commit(editProject(project_,catalog_,op,args),"节点参数已更新");
    }
    if (op == "batch" || op.startsWith("graph.node.") || op.startsWith("graph.wire.") || op.startsWith("ui.") || op.startsWith("editor.") || op == "sync.apply") {
        const bool layoutOnly = op.startsWith("ui.") || (op.startsWith("editor.") && args.value("page") == "layout") || op == "graph.node.move" || op == "graph.node.rotate" || op == "graph.node.patch" || op == "graph.wire.patch" || op == "sync.apply";
        requireEditable(layoutOnly || op == "batch");
        auto arguments = args;
        if (op == "editor.paste" && !arguments.contains("payload")) { const auto json = QJsonDocument::fromJson(QGuiApplication::clipboard()->text().toUtf8()); if (!json.isObject()) throw Error("剪贴板没有可粘贴对象"); arguments["payload"] = json.toVariant(); }
        const auto edit = editProject(project_,catalog_,op,arguments);
        if (edit.engineChanged && engine_->observation().value("running").toBool()) throw Error("请先暂停运行再修改功能设计");
        if (op == "graph.node.add") { auto recent = settings_->value("library/recent").toStringList(); const auto plugin = args.value("plugin").toString(); recent.removeAll(plugin); recent.prepend(plugin); while (recent.size() > 24) recent.removeLast(); settings_->setValue("library/recent",recent); emit preferencesChanged(); }
        return commit(edit,"设计已更新");
    }
    if (op == "plugin.export") {
        if (args.contains("plugins")) {
            auto directory = args.value("directory").toString(); if (QUrl(directory).isLocalFile()) directory = QUrl(directory).toLocalFile();
            if (directory.isEmpty()) throw Error("请选择插件导出目录");
            const auto inventory = plugins_->tree(project_,args.value("scope","project").toString()).value("plugins").toList();
            QMap<QString,QString> paths;
            for (const auto &entry : inventory) paths.insert(entry.toMap().value("id").toString(),entry.toMap().value("relative_path").toString());
            QList<QPair<PluginPackage,QString>> selected; QVariantList conflicts; QSet<QString> seen;
            for (const auto &entry : args.value("plugins").toList()) {
                const auto id = entry.toString(); if (seen.contains(id)) continue; seen.insert(id);
                if (!paths.contains(id)) throw Error("导出选择不属于当前工程：" + id);
                const auto path = QDir(directory).filePath(PluginPackage::checkedPath(paths.value(id)));
                if (QFileInfo::exists(path)) conflicts.append(path);
                selected.append({plugins_->find(id,args.value("scope")=="library" ? emptyProject() : project_),path});
            }
            if (selected.isEmpty()) throw Error("请先勾选要导出的插件");
            if (!conflicts.isEmpty() && !args.value("overwrite").toBool()) return QVariantMap{{"requires_confirmation",true},{"conflicts",conflicts}};
            return launchTask(op,[selected] {
                QVariantList written;
                for (const auto &entry : selected) { try { entry.first.save(entry.second); written.append(entry.second); } catch (const Error &error) { throw Error(QString("已导出 %1 个插件；%2").arg(written.size()).arg(error.message)); } }
                return QVariantMap{{"paths",written},{"count",written.size()}};
            });
        }
        auto path = args.value("path").toString(); if (QUrl(path).isLocalFile()) path = QUrl(path).toLocalFile();
        if (path.isEmpty()) throw Error("请选择插件导出路径");
        if (!path.endsWith(".crpl",Qt::CaseInsensitive)) path += ".crpl";
        const auto package = plugins_->find(args.value("plugin").toString(),args.value("scope")=="library" ? emptyProject() : project_);
        return launchTask(op,[package,path] { package.save(path); return QVariantMap{{"path",path},{"id",package.id()},{"version",package.version()}}; });
    }
    if (op == "plugin.import" || op == "plugin.remove" || op == "plugin.enable") {
        requireEditable();
        if (op == "plugin.import") {
            auto path = args.value("path").toString(); if (QUrl(path).isLocalFile()) path = QUrl(path).toLocalFile();
            const auto package = args.contains("manifest") ? PluginPackage::fromPython(fromJsonValue(args.value("manifest")).toMap())
                : path.endsWith(".crpl",Qt::CaseInsensitive) ? PluginPackage::open(path) : PluginPackage::fromPython(importPlugin(path));
            const auto installed = plugins_->install(package,path);
            if (installed.value("restart_required").toBool()) { emit pluginsChanged(); tell("插件更新已保存，重启 CoRig 后生效"); return installed; }
        } else if (op == "plugin.enable") plugins_->setEnabled(args.value("plugin").toString(),args.value("enabled").toBool(),project_);
        else {
            const auto id = args.value("package",args.value("plugin")).toString();
            const auto package = plugins_->find(id,project_); plugins_->remove(package.id(),project_);
        }
        const auto design = project_; const bool wasDirty = dirty_;
        return launchEngine("project.apply",{{"project",design}},[this,design,wasDirty](const QVariant &,const QString &error) {
            if (error.isEmpty()) { adopt(design,wasDirty); tell("插件库已更新"); } emit pluginsChanged();
        });
    }
    if (op == "library.state") return libraryPreferences();
    if (op == "library.search") return catalogEntries(args.value("query").toString(),args.value("category").toStringList(),args.value("scope","all").toString());
    if (op == "library.preview") { const auto plugin = args.value("plugin").toString(); return QVariantMap{{"definition",catalog_.definition(plugin)},{"geometry",previewGeometry(plugin)}}; }
    if (op == "library.favorite") {
        const auto id = args.value("plugin").toString(); catalog_.definition(id); auto items = settings_->value("library/favorites").toStringList(); items.removeAll(id); if (args.value("enabled").toBool()) items.append(id); settings_->setValue("library/favorites",items); emit preferencesChanged(); return libraryPreferences();
    }
    if (op == "library.configure") {
        if (args.contains("view")) { const auto view = args.value("view").toString(); if (view != "grid" && view != "list") throw Error("浏览方式需要 grid 或 list"); settings_->setValue("library/view",view); }
        if (args.contains("size")) settings_->setValue("library/size",std::clamp(finiteNumber(args.value("size"),"预览比例"),.6,2.));
        emit preferencesChanged(); return libraryPreferences();
    }
    if (op.startsWith("workspace.") || op.startsWith("window.")) {
        if (!workspaces_) throw Error("工作区尚未建立"); const auto page = args.value("page").toString();
        if (op == "workspace.open") return workspaces_->open(page,args.value("window").toString());
        if (op == "workspace.close") return workspaces_->closePage(page,args.value("discard").toBool());
        if (op == "workspace.detach") return workspaces_->detach(page,{args.value("x").toInt(),args.value("y").toInt()});
        if (op == "workspace.move") return workspaces_->movePage(page,args.value("window").toString(),args.value("index",-1).toInt());
        if (op == "window.pin") return workspaces_->pinWindow(args.value("window").toString(),args.value("enabled").toBool());
        return QVariantMap{{"closed",workspaces_->closeWindow(args.value("window").toString(),args.value("discard").toBool())}};
    }
    if (op == "capture") return capture(args);
    if (op.startsWith("controls.")) return controls(op,args);
    if (op == "view.actions") return viewActions(args.value("node").toString(),args.value("view").toString());
    if (op == "view.call") return viewCall(args);
    if (op == "runtime.start") {
        if (busy_) throw Error("请等待工程操作完成");
        return launchEngine(op,args);
    }
    // Reset restores the document's initial design, including parameters that
    // were overridden only for this run in read-only mode.
    if (op == "runtime.reset") return launchEngine(op,{{"project",project_}});
    if (op.startsWith("runtime.") || op.startsWith("log.") || op == "node.state" || op == "node.action" || op == "view.read" || op == "python.probe") {
        auto normalized = args;
        // With no explicit observation arguments, inspect the same range/FFT
        // settings currently displayed by the user's view.
        if (op == "view.read" && !args.contains("args")) {
            const auto key = args.value("node").toString() + "/" + args.value("view").toString();
            if (viewArguments_.contains(key)) normalized["args"] = viewArguments_.value(key);
        }
        if (normalized.contains("path") && QUrl(normalized.value("path").toString()).isLocalFile()) normalized["path"] = QUrl(normalized.value("path").toString()).toLocalFile();
        return launchEngine(op,normalized);
    }
    throw Error("操作尚未提供处理入口：" + op);
}

QVariantList Controller::viewActions(const QString &node, const QString &view) const
{
    const auto design = nodeDesign(node); if (design.isEmpty()) throw Error("节点不存在");
    const auto kind = design.value("plugin").toString(); QVariantList actions;
    const auto add = [&](const QString &name, const QString &description) { actions.append(QVariantMap{{"action",name},{"description",description}}); };
    if (kind == "plot.curves") { add("range","设置坐标范围与跟随"); add("fft","切换频谱"); add("export_image","导出 PNG 或 SVG"); }
    if (kind == "plot.attitude") { add("camera","调整方位、俯仰和缩放"); add("reset_camera","恢复相机"); add("export_image","导出 PNG"); }
    const auto declared = definition(kind).value("actions").toMap();
    for (auto i = declared.cbegin(); i != declared.cend(); ++i) actions.append(QVariantMap{{"action",i.key()},{"contract",i.value()}});
    Q_UNUSED(view); return actions;
}

QVariant Controller::viewCall(const QVariantMap &args)
{
    const auto node = args.value("node").toString(), view = args.value("view").toString(), action = args.value("action").toString();
    const auto arguments = args.value("args").toMap(); const auto key = node + "/" + view;
    if (action == "range" || action == "fft") {
        for (const auto &candidate : viewItems_.value(key)) if (candidate && candidate->property("visible").toBool()) {
            QVariant result;
            if (QMetaObject::invokeMethod(candidate,"viewAction",Q_RETURN_ARG(QVariant,result),Q_ARG(QVariant,QVariant(action)),Q_ARG(QVariant,QVariant(arguments)))) return result;
        }
        auto options = viewArguments_.value(key);
        for (auto i = arguments.cbegin(); i != arguments.cend(); ++i) options[i.key()] = i.value();
        if (action == "fft") options["fft"] = arguments.value("enabled",true);
        observeView(node,view,options); return QVariantMap{{"updated",true}};
    }
    if (action == "camera" || action == "reset_camera" || action == "export_image") {
        QPointer<QObject> item;
        for (const auto &candidate : viewItems_.value(key)) if (candidate && candidate->property("visible").toBool()) { item = candidate; break; }
        if (!item) throw Error("请先打开目标视图");
        QVariant result;
        if (!QMetaObject::invokeMethod(item,"viewAction",Q_RETURN_ARG(QVariant,result),Q_ARG(QVariant,QVariant(action)),Q_ARG(QVariant,QVariant(arguments)))) throw Error("目标视图不支持该操作");
        return result;
    }
    return launchEngine("node.action",{{"node",node},{"action",action},{"args",arguments}});
}

QVariant Controller::capture(const QVariantMap &args)
{
    if (!workspaces_) throw Error("界面尚未打开");
    const auto target = args.value("target","window").toString();
    QQuickItem *item = nullptr;
    if (target == "window") { const auto windows = workspaces_->nativeWindows(); if (!windows.isEmpty()) item = windows.first()->contentItem(); }
    else if (target.startsWith("view.")) { const auto key = target.mid(5); const auto separator = key.lastIndexOf('.'); for (const auto &candidate : viewItems_.value(key.left(separator) + "/" + key.mid(separator + 1))) if (candidate && candidate->property("visible").toBool()) { item = qobject_cast<QQuickItem *>(candidate); break; } }
    else { workspaces_->open(target); item = workspaces_->page(target); }
    if (!item || !item->window()) throw Error("截图目标尚未显示");
    if (!item->window()->isExposed()) throw Error("请先显示并恢复目标窗口，再获取截图");
    const auto size = QSize(args.value("width",item->width()).toInt(),args.value("height",item->height()).toInt());
    if (size.width() < 1 || size.height() < 1 || size.width() > 8192 || size.height() > 8192) throw Error("截图尺寸需要在 1～8192 之间");
    auto path = args.value("path").toString(); if (QUrl(path).isLocalFile()) path = QUrl(path).toLocalFile();
    if (path.isEmpty()) { QDir().mkpath(dataDirectory_ + "/captures"); path = dataDirectory_ + "/captures/" + newId(target) + ".png"; }
    const auto grab = item->grabToImage(size); if (!grab) throw Error("无法启动内部截图");
    const auto id = newId("job"); jobs_[id] = {{"id",id},{"op","capture"},{"state","running"}};
    connect(grab.data(),&QQuickItemGrabResult::ready,this,[this,grab,id,path] { const bool saved = grab->saveToFile(path); finishJob(id,QVariantMap{{"path",path},{"width",grab->image().width()},{"height",grab->image().height()}},saved ? QString{} : "截图保存失败"); });
    item->window()->update(); return QVariantMap{{"job",id},{"state","running"}};
}

} // namespace corig
