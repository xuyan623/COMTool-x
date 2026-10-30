#include "engine.h"
#include "project.h"
#include <QDateTime>
#include <QJsonDocument>
#include <QSaveFile>
#include <algorithm>
#include <limits>

namespace corig {

Engine::Engine(NodeHost host, QObject *parent) : QThread(parent), host_(std::move(host)), catalog_(host_.catalog ? Catalog(QList<PluginPackage>{}) : Catalog{})
{
    host_.time = [this] { return elapsed(Clock::now()); };
    published_.store(std::make_shared<const QVariantMap>(QVariantMap{{"active",false},{"running",false},{"processed",0},{"failed",0}}));
    setObjectName("graph-batches"); start();
}

Engine::~Engine() { shutdown(); }

quint64 Engine::submit(const QString &operation, const QVariantMap &arguments)
{
    const auto id = ++requestSequence_;
    {
        std::lock_guard guard(queueMutex_);
        if (quitting_) throw Error("计算引擎正在退出");
        if (requests_.size() >= 4096) throw Error("操作请求过多，请等待已有操作完成");
        if (operation == "runtime.pause" || operation == "runtime.stop" || operation == "runtime.reset") stopRequested_ = true;
        if (operation == "node.action" && arguments.value("action") == "cancel_file") cancellations_.insert(arguments.value("node").toString());
        requests_.push_back({id,operation,arguments});
    }
    condition_.notify_one();
    return id;
}

void Engine::receive(const QString &node, const Values &outputs)
{
    if (!accepting_) return;
    qsizetype bytes = 0;
    for (auto i = outputs.cbegin(); i != outputs.cend(); ++i) if (i.value().metaType().id() == QMetaType::QByteArray) bytes += i.value().toByteArray().size();
    {
        std::lock_guard guard(queueMutex_);
        // Refuse overload explicitly instead of silently losing unobserved data.
        if (inputQueue_.size() >= 65536 || queuedBytes_ + bytes > 32 * 1024 * 1024) {
            overflowError_ = "接收队列超过处理容量，已暂停；请降低输入速率或缩短处理链";
            accepting_ = false;
        } else { inputQueue_.push_back({node,outputs,false}); queuedBytes_ += bytes; }
    }
    condition_.notify_one();
}

void Engine::ioObservation(const QString &node, const QVariantMap &state)
{
    std::lock_guard guard(queueMutex_); ioStates_.insert(node,state);
}

QVariantMap Engine::observation() const { return *published_.load(); }

void Engine::shutdown()
{
    if (quitting_.exchange(true)) return;
    stopRequested_ = true; accepting_ = false; condition_.notify_one();
    wait();
}

bool Engine::cancelled(const QString &node) const
{
    if (quitting_ || stopRequested_) return true;
    std::lock_guard guard(queueMutex_); return cancellations_.contains(node);
}

double Engine::elapsed(Clock::time_point now) const
{
    return elapsedBase_ + (running_ ? std::chrono::duration<double>(now - resumedAt_).count() : 0.);
}

void Engine::record(const QString &level, const QString &message, const QString &node, const QVariantMap &detail)
{
    auto entry = detail;
    entry.insert("cursor",++journalCursor_); entry.insert("level",level); entry.insert("message",message); entry.insert("node",node);
    entry.insert("time",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    if (batch_) entry.insert("batch",QVariant::fromValue(batch_->context.batch));
    auto bytes = QJsonDocument::fromVariant(entry).toJson(QJsonDocument::Compact).size();
    if (bytes > 256 * 1024) {
        entry.remove("inputs"); entry.remove("outputs"); entry.insert("payload_truncated",true); entry.insert("original_record_bytes",bytes);
        bytes = QJsonDocument::fromVariant(entry).toJson(QJsonDocument::Compact).size();
    }
    entry.insert("record_bytes",bytes); journalBytes_ += bytes;
    journal_.push_back(std::move(entry));
    while (journal_.size() > (debug_ || journalDetail_ ? 10000u : 2000u) || journalBytes_ > 16 * 1024 * 1024) {
        journalBytes_ -= journal_.front().value("record_bytes").toLongLong(); journal_.pop_front();
    }
}

void Engine::applyProject(const QVariantMap &project)
{
    if (running_) throw Error("请先暂停运行再修改功能设计");
    Catalog candidateCatalog = host_.catalog ? host_.catalog(project) : Catalog{};
    if (!host_.catalog) candidateCatalog.applyProject(project);
    validateProject(project,candidateCatalog);
    const auto candidateOrder = executionOrder(project,candidateCatalog);
    std::map<QString,std::unique_ptr<Node>> candidates;
    auto nodeHost = host_;
    nodeHost.publish = [this](const QString &source, const Values &values, const BatchContext &context) { publishAndDrain(source,values,context); };
    nodeHost.cancelled = [this](const QString &node) { return cancelled(node); };
    for (const auto &item : project.value("nodes").toList()) {
        const auto design = item.toMap(); const auto id = design.value("id").toString();
        if (!design.value("placed",true).toBool()) continue;
        auto scopedHost = nodeHost;
        scopedHost.resource = [project](const QString &reference) { return resourceBytes(project,reference); };
        scopedHost.log = [this,id](const QString &level, const QString &message) { record(level,message,id); };
        const auto found = nodes_.find(id);
        if (found == nodes_.end() || found->second->kind() != design.value("plugin").toString()
            || catalog_.definition(design.value("plugin").toString()) != candidateCatalog.definition(design.value("plugin").toString())
            || found->second->config() != candidateCatalog.checkedConfig(design.value("plugin").toString(),design.value("config").toMap()))
            candidates.emplace(id,makeNode(design,candidateCatalog,scopedHost));
    }
    if (batch_) { record("info","暂停期间修改了设计，未完成批次已中止，已执行状态保留"); batch_.reset(); }
    std::map<QString,std::unique_ptr<Node>> next;
    for (const auto &item : project.value("nodes").toList()) {
        const auto design = item.toMap(); const auto id = design.value("id").toString();
        if (!design.value("placed",true).toBool()) continue;
        auto old = nodes_.find(id); auto prepared = candidates.find(id);
        if (old != nodes_.end() && old->second->kind() == design.value("plugin").toString()
            && catalog_.definition(old->second->kind()).value("package_hash") == candidateCatalog.definition(old->second->kind()).value("package_hash")) {
            if (prepared != candidates.end()) old->second->configure(*prepared->second);
            next.emplace(id,std::move(old->second));
        } else next.emplace(id,std::move(prepared->second));
    }
    for (auto &[id,node] : nodes_) if (node) node->stop();
    nodes_ = std::move(next); catalog_ = std::move(candidateCatalog); project_ = project; order_ = candidateOrder;
    QHash<QString,Route> oldRoutes;
    for (const auto &route : routes_) oldRoutes.insert(route.id,route);
    routes_.clear(); outgoing_.clear();
    QHash<QString,QSet<QString>> connected, reset;
    for (const auto &item : project.value("wires").toList()) {
        const auto w = item.toMap(); const auto source = w.value("source").toList(), target = w.value("target").toList();
        Route route{w.value("id").toString(),source[0].toString(),source[1].toString(),target[0].toString(),target[1].toString()};
        const auto old = oldRoutes.value(route.id);
        if (old.source == route.source && old.sourcePort == route.sourcePort && old.target == route.target && old.targetPort == route.targetPort) {
            route.count = old.count; route.steps = old.steps; route.connected = old.connected;
        } else reset[route.target].insert(route.targetPort);
        connected[route.target].insert(route.targetPort);
        outgoing_[route.source].append(routes_.size()); routes_.append(route);
    }
    for (auto &[id,node] : nodes_) node->setConnections(connected.value(id),reset.value(id));
    pendingFrames_.clear(); ++epoch_;
    rebuildSources(Clock::now()); record("info","功能设计已更新");
}

void Engine::rebuildSources(Clock::time_point now)
{
    auto previous = deadlines_;
    deadlines_.clear(); readySources_.clear(); availableSources_.clear(); readyCursor_ = 0;
    QHash<QString,QString> titles;
    for (const auto &item : project_.value("nodes").toList()) { const auto node = item.toMap(); titles[node.value("id").toString()] = node.value("title",node.value("id")).toString(); }
    for (const auto &[id,node] : nodes_) {
        const auto interval = node->sourceInterval();
        if (interval >= 0. || catalog_.definition(node->kind()).value("source").toBool()) availableSources_.append(QVariantMap{{"id",id},{"name",titles.value(id,id)}});
        if (interval == 0. && outgoing_.contains(id)) readySources_.append(id);
        else if (interval > 0.) deadlines_.insert(id, previous.value(id,now));
    }
}

std::optional<Engine::Event> Engine::nextEvent(Clock::time_point now)
{
    // Rotate among complete frames, device input, timed sources and ready
    // sources. A perpetually due timer or busy socket cannot starve the others.
    for (int attempt = 0; attempt < 4; ++attempt) {
        const int group = (schedulerCursor_ + attempt) % 4;
        std::optional<Event> event;
        if (group == 0 && !pendingFrames_.empty()) { event = pendingFrames_.front(); pendingFrames_.pop_front(); }
        else if (group == 1) {
            std::lock_guard guard(queueMutex_);
            if (!inputQueue_.empty()) {
                event = std::move(inputQueue_.front()); inputQueue_.pop_front();
                for (auto i = event->outputs.cbegin(); i != event->outputs.cend(); ++i) if (i.value().metaType().id() == QMetaType::QByteArray) queuedBytes_ -= i.value().toByteArray().size();
            }
        } else if (group == 2) {
            auto due = deadlines_.end();
            for (auto i = deadlines_.begin(); i != deadlines_.end(); ++i) if (i.value() <= now && (due == deadlines_.end() || i.value() < due.value())) due = i;
            if (due != deadlines_.end()) {
                event = Event{due.key(),{},true};
                due.value() = now + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(std::max(.000001,nodes_.at(due.key())->sourceInterval())));
            }
        } else if (group == 3 && !readySources_.isEmpty()) event = Event{readySources_[readyCursor_++ % readySources_.size()],{},true};
        if (event) { schedulerCursor_ = (group + 1) % 4; return event; }
    }
    return {};
}

void Engine::beginSession(bool debug)
{
    if (nodes_.empty()) throw Error("功能设计为空");
    if (active_) return;
    // Ending a session releases devices; only reset restores experiment time
    // and numerical state. Starting again continues from the retained values.
    debug_ = debug; active_ = true;
    batch_.reset(); pendingFrames_.clear(); stopRequested_ = false;
    const BatchContext context{0,0.,false,true};
    for (auto &[id,node] : nodes_) {
        const auto start = node->start(context);
        for (const auto &outputs : start.emissions) pendingFrames_.push_back({id,outputs,false});
        node->resume();
    }
    ++epoch_; rebuildSources(Clock::now()); record("info",debug_ ? "调试会话已就绪" : "运行会话已就绪");
}

void Engine::endSession()
{
    accepting_ = false;
    if (running_) elapsedBase_ = elapsed(Clock::now());
    running_ = false; active_ = false; batch_.reset(); pendingFrames_.clear();
    {
        std::lock_guard guard(queueMutex_); inputQueue_.clear(); queuedBytes_ = 0;
    }
    for (auto &[id,node] : nodes_) node->stop();
    if (host_.stopExtensions) host_.stopExtensions();
    ++epoch_; stopRequested_ = false; record("info","运行已结束，连接已释放");
}

void Engine::beginBatch(const Event &event, bool stepping)
{
    if (!nodes_.contains(event.source)) return;
    batch_ = std::make_unique<Batch>();
    batch_->context = {++batchSequence_,elapsed(Clock::now()),stepping,false};
    batch_->source = event.source; batch_->tick = event.tick;
    if (debug_ || journalDetail_) record("debug","采样批次开始",event.source,{{"kind","batch_start"}});
    if (event.tick) batch_->touched.insert(event.source);
    else {
        nodes_.at(event.source)->retainEmission(event.outputs);
        route(event.source,NodeResult::output(event.outputs));
    }
}

void Engine::route(const QString &source, const NodeResult &result)
{
    if (result.rescheduleSource) {
        const auto delay = nodes_.at(source)->sourceInterval();
        if (delay > 0.) deadlines_[source] = Clock::now() + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(delay));
        else deadlines_.remove(source);
    }
    if (result.separateFrames) {
        for (const auto &outputs : result.emissions) {
            if (pendingFrames_.size() >= 65536) throw Error("完整采样帧超过队列容量，已暂停");
            pendingFrames_.push_back({source,outputs,false});
        }
    }
    for (const auto index : outgoing_.value(source)) {
        auto &wire = routes_[index];
        auto mark = [&] {
            batch_->touched.insert(wire.target);
            if (nodes_.at(wire.target)->deferredInputs() && !batch_->deferredIO.contains(wire.target)) batch_->deferredIO.append(wire.target);
        };
        if (result.disconnected.contains(wire.sourcePort)) {
            wire.connected = false; batch_->changes[wire.target].remove(wire.targetPort); batch_->invalid[wire.target].insert(wire.targetPort); mark();
        }
        if (!result.separateFrames) for (const auto &outputs : result.emissions) if (outputs.contains(wire.sourcePort)) {
            wire.connected = true; ++wire.count; if (batch_->context.stepping) ++wire.steps;
            batch_->invalid[wire.target].remove(wire.targetPort); batch_->changes[wire.target].insert(wire.targetPort,outputs.value(wire.sourcePort)); mark();
        }
    }
}

bool Engine::advanceBatch(int nodeLimit)
{
    if (!batch_) return true;
    int executed = 0;
    for (;;) {
        QString id;
        while (batch_->cursor < order_.size()) {
            const auto candidate = order_[batch_->cursor++];
            if (batch_->touched.contains(candidate) && !nodes_.at(candidate)->deferredInputs()) { id = candidate; break; }
        }
        if (id.isEmpty() && batch_->ioCursor < batch_->deferredIO.size()) id = batch_->deferredIO[batch_->ioCursor++];
        if (id.isEmpty()) { if (debug_ || journalDetail_) record("debug","采样批次完成",{},{{"kind","batch_end"}}); batch_.reset(); return true; }
        auto &node = nodes_.at(id);
        const auto input = batch_->changes.value(id);
        const auto started = Clock::now();
        currentNode_ = id;
        const auto result = node->execute(input,batch_->invalid.value(id),batch_->context,batch_->tick && id == batch_->source);
        nodeErrors_.remove(id);
        ++processed_; ++executed;
        if (debug_ || journalDetail_ || batch_->context.stepping) {
            QVariantMap inputs, outputs;
            for (auto i = input.cbegin(); i != input.cend(); ++i) inputs.insert(i.key(),i.value());
            for (const auto &emission : result.emissions) for (auto i = emission.cbegin(); i != emission.cend(); ++i) outputs.insert(i.key(),i.value());
            record("debug","节点执行完成",id,{{"kind","node"},{"inputs",toJsonValue(inputs)}, {"outputs",toJsonValue(outputs)}, {"duration_us",std::chrono::duration<double,std::micro>(Clock::now() - started).count()}});
        }
        route(id,result);
        if (nodeLimit > 0 && executed >= nodeLimit) {
            bool remaining = batch_->ioCursor < batch_->deferredIO.size();
            for (auto at = batch_->cursor; !remaining && at < order_.size(); ++at)
                remaining = batch_->touched.contains(order_[at]) && !nodes_.at(order_[at])->deferredInputs();
            if (!remaining) { if (debug_ || journalDetail_) record("debug","采样批次完成",{},{{"kind","batch_end"}}); batch_.reset(); return true; }
            return false;
        }
    }
}

void Engine::publishAndDrain(const QString &source, const Values &values, const BatchContext &)
{
    if (batch_) throw Error("同步发布只能用于批次之外的节点动作");
    beginBatch({source,values,false},false);
    advanceBatch();
    // Codec frame boundaries generated by this publication belong to the same
    // action completion; all actual downstream writes finish before returning.
    while (!pendingFrames_.empty()) {
        const auto event = pendingFrames_.front(); pendingFrames_.pop_front();
        beginBatch(event,false); advanceBatch();
    }
}

QVariant Engine::dispatch(const Request &request)
{
    const auto &op = request.operation; const auto &args = request.arguments;
    if (op == "project.apply") { applyProject(args.value("project").toMap()); return QVariantMap{{"updated",true}}; }
    if (op == "project.load" || op == "project.replace") {
        auto project = op == "project.load" ? loadProject(args.value("path").toString()) : args.value("project").toMap();
        if (args.contains("plugin_selections")) project["plugin_selections"] = args.value("plugin_selections");
        if (host_.projectReadiness) {
            const auto readiness = host_.projectReadiness(project);
            if (!readiness.isEmpty()) return readiness;
        }
        Catalog checked = host_.catalog ? host_.catalog(project) : Catalog{};
        if (!host_.catalog) checked.applyProject(project);
        validateProject(project,checked);
        // Validate every constructor before ending the old session. Malformed
        // expressions/models cannot replace the user's still usable document.
        NodeHost validateHost = host_; validateHost.resource = [project](const QString &ref) { return resourceBytes(project,ref); };
        for (const auto &entry : project.value("nodes").toList()) makeNode(entry.toMap(),checked,validateHost);
        endSession(); nodes_.clear(); routes_.clear(); elapsedBase_ = 0.; batchSequence_ = processed_ = failed_ = 0; currentNode_.clear(); nodeErrors_.clear();
        applyProject(project); return project;
    }
    if (op == "python.probe") {
        if (!host_.python) throw Error("Python 通道不可用");
        return host_.python({{"op","probe"},{"dependencies",args.value("dependencies",QVariantList{})},{"timeout_ms",5000}});
    }
    if (op == "node.configure") {
        const auto id = args.value("node").toString();
        if (!nodes_.contains(id)) throw Error("节点不存在：" + id);
        auto &node = nodes_.at(id);
        const auto changes = args.value("changes").toMap();
        const auto properties = catalog_.definition(node->kind()).value("properties").toMap();
        for (auto i = changes.cbegin(); i != changes.cend(); ++i)
            if (!properties.value(i.key()).toMap().value("runtime_editable").toBool()) throw Error("该参数需要暂停后修改：" + i.key());
        auto design = args.value("design").toMap();
        design["config"] = catalog_.checkedConfig(node->kind(),changes,node->config());
        const auto nextPorts = catalog_.ports(node->kind(),design.value("config").toMap());
        if (nextPorts.size() != node->ports().size()) throw Error("运行时调参不能改变端口");
        for (qsizetype i = 0; i < nextPorts.size(); ++i) if (nextPorts[i].describe() != node->ports()[i].describe()) throw Error("运行时调参不能改变端口");
        auto resourceProject = project_; auto resources = resourceProject.value("resources").toMap();
        const auto imported = args.value("resources").toMap(); for (auto i = imported.cbegin(); i != imported.cend(); ++i) resources[i.key()] = i.value();
        resourceProject["resources"] = resources;
        auto nodeHost = host_;
        nodeHost.resource = [resourceProject](const QString &ref) { return resourceBytes(resourceProject,ref); };
        nodeHost.publish = [this](const QString &source, const Values &values, const BatchContext &context) { publishAndDrain(source,values,context); };
        nodeHost.cancelled = [this](const QString &key) { return cancelled(key); };
        nodeHost.log = [this,id](const QString &level, const QString &message) { record(level,message,id); };
        auto prepared = makeNode(design,catalog_,nodeHost); node->configure(*prepared);
        auto items = project_.value("nodes").toList();
        for (auto &item : items) if (item.toMap().value("id") == id) item = design;
        project_["nodes"] = items; project_["resources"] = resources; rebuildSources(Clock::now());
        record("info","运行参数已更新",id); return QVariantMap{{"node",id},{"config",node->config()},{"__resources",resources}};
    }
    if (op == "runtime.start") {
        if (args.contains("debug")) debug_ = args.value("debug").toBool();
        beginSession(debug_);
        if (batch_) batch_->context.stepping = false;
        running_ = true; accepting_ = true; resumedAt_ = Clock::now();
        for (auto &[id,node] : nodes_) node->resume();
        rebuildSources(resumedAt_); record("info","正在运行"); return QVariantMap{{"running",true}};
    }
    if (op == "runtime.debug") { debug_ = args.value("enabled").toBool(); record("info",debug_ ? "调试模式已开启" : "已切换普通运行",{},{{"kind","debug_mode"}}); return QVariantMap{{"debug",debug_}}; }
    if (op == "log.configure") { journalDetail_ = args.value("detail").toBool(); record("info",journalDetail_ ? "普通运行将记录节点明细" : "普通运行仅记录关键事件"); return QVariantMap{{"detail",journalDetail_}}; }
    if (op == "runtime.pause") {
        accepting_ = false;
        if (running_) elapsedBase_ = elapsed(Clock::now());
        running_ = false;
        for (auto &[id,node] : nodes_) node->pause();
        { std::lock_guard guard(queueMutex_); inputQueue_.clear(); queuedBytes_ = 0; }
        stopRequested_ = false;
        record("info","运行已暂停，状态与设备连接保留"); return QVariantMap{{"running",false}};
    }
    if (op == "runtime.stop") { endSession(); return QVariantMap{{"active",false}}; }
    if (op == "runtime.reset") {
        const auto design = args.value("project",project_).toMap(); endSession(); nodes_.clear(); routes_.clear(); elapsedBase_ = 0.; processed_ = failed_ = 0;
        currentNode_.clear(); nodeErrors_.clear(); applyProject(design); record("info","运行状态已重置"); return QVariantMap{{"reset",true}};
    }
    if (op == "runtime.step") {
        if (running_) throw Error("请先暂停再单步");
        beginSession(true);
        if (!batch_) {
            Event event;
            if (!pendingFrames_.empty()) { event = pendingFrames_.front(); pendingFrames_.pop_front(); }
            else {
                const auto selected = args.value("source").toString();
                if (!selected.isEmpty()) event = {selected,{},true};
                else {
                    for (const auto &id : order_) if (nodes_.at(id)->sourceInterval() >= 0. && outgoing_.contains(id)) { event = {id,{},true}; break; }
                }
            }
            if (event.source.isEmpty()) throw Error("没有可推进的信号源；请提供输入或启用定时器");
            // Manual advancement uses one source period after the first anchor,
            // rather than adding the time spent paused at the keyboard.
            if (batchSequence_ > 0 && event.tick) elapsedBase_ += std::max(0.,nodes_.at(event.source)->sourceInterval());
            beginBatch(event,true);
        }
        const bool complete = advanceBatch(args.value("mode","batch") == "node" ? 1 : -1);
        return QVariantMap{{"batch_complete",complete}, {"batch",QVariant::fromValue(batchSequence_)}};
    }
    if (op == "runtime.emit") {
        if (!active_) throw Error("请先建立运行会话");
        Values outputs; const auto values = fromJsonValue(args.value("outputs")).toMap();
        for (auto i = values.cbegin(); i != values.cend(); ++i) outputs.insert(i.key(),i.value());
        publishAndDrain(args.value("node").toString(),outputs,{}); return QVariantMap{{"published",true}};
    }
    if (op == "node.state" || op == "view.read") {
        const auto id = args.value("node").toString();
        if (!nodes_.contains(id)) throw Error("节点不存在：" + id);
        return op == "node.state" ? nodes_.at(id)->snapshot(args.value("detailed",true).toBool()) : nodes_.at(id)->view(args.value("view").toString(),args.value("args").toMap());
    }
    if (op == "node.action") {
        const auto id = args.value("node").toString(), action = args.value("action").toString();
        if (!nodes_.contains(id)) throw Error("节点不存在：" + id);
        const auto spec = catalog_.definition(nodes_.at(id)->kind()).value("actions").toMap();
        if (!spec.contains(action)) throw Error("动作未声明：" + action);
        const auto contract = spec.value(action).toMap();
        if (contract.value("requires_running",true).toBool() && !active_) throw Error("请先建立运行会话");
        if (contract.value("requires_paused",false).toBool() && running_) throw Error("请先暂停运行");
        auto arguments = fromJsonValue(args.value("args")).toMap();
        const auto schema = contract.value("args").toMap();
        for (auto i = schema.cbegin(); i != schema.cend(); ++i) if (i.value().toMap().value("required").toBool() && !arguments.contains(i.key())) throw Error("动作缺少参数：" + i.key());
        arguments = checkedProperties(schema,arguments,{});
        // Argument defaults absent from declarations must remain absent; an
        // optional numeric/string argument is not the same as an explicit null.
        for (auto i = arguments.begin(); i != arguments.end();) { if (!i.value().isValid() || i.value().isNull()) i = arguments.erase(i); else ++i; }
        { std::lock_guard guard(queueMutex_); if (action == "send_file") cancellations_.remove(id); }
        QVariant result;
        const BatchContext context{batchSequence_,elapsed(Clock::now()),!running_,false};
        const auto emission = nodes_.at(id)->action(action,arguments,context,result);
        if (!emission.emissions.isEmpty() || !emission.disconnected.isEmpty() || emission.rescheduleSource) {
            if (batch_) { record("info","节点动作中止了未完成的单步批次",id); batch_.reset(); }
            beginBatch({id,{},false},!running_);
            nodes_.at(id)->retainEmission(emission.emissions.isEmpty() ? Values{} : emission.emissions.back());
            route(id,emission); advanceBatch();
            while (!pendingFrames_.empty()) { const auto event = pendingFrames_.front(); pendingFrames_.pop_front(); beginBatch(event,!running_); advanceBatch(); }
        }
        rebuildSources(Clock::now()); return result;
    }
    if (op == "log.export") {
        QSaveFile file(args.value("path").toString()); if (!file.open(QIODevice::WriteOnly)) throw Error("日志导出文件无法打开：" + file.errorString());
        const auto node = args.value("node").toString(), level = args.value("level").toString(), text = args.value("text").toString(); const auto selectedBatch = args.value("batch",0).toULongLong();
        int count = 0;
        for (const auto &entry : journal_) if ((!selectedBatch || entry.value("batch").toULongLong() == selectedBatch) && (node.isEmpty() || entry.value("node") == node) && (level.isEmpty() || entry.value("level") == level) && (text.isEmpty() || entry.value("message").toString().contains(text,Qt::CaseInsensitive))) {
            const auto bytes = QJsonDocument::fromVariant(toJsonValue(entry)).toJson(QJsonDocument::Compact) + '\n';
            if (file.write(bytes) != bytes.size()) throw Error("日志写入失败"); ++count;
        }
        if (!file.commit()) throw Error("日志保存失败"); return QVariantMap{{"path",file.fileName()},{"records",count}};
    }
    if (op == "log.query") {
        QVariantList records; const auto after = args.value("after",0).toULongLong(); const auto limit = std::clamp(args.value("limit",200).toInt(),1,2000);
        const auto node = args.value("node").toString(), level = args.value("level").toString(), text = args.value("text").toString(); const auto batch = args.value("batch",0).toULongLong();
        for (const auto &entry : journal_) if (entry.value("cursor").toULongLong() > after && (!batch || entry.value("batch").toULongLong() == batch) && (node.isEmpty() || entry.value("node") == node)
            && (level.isEmpty() || entry.value("level") == level) && (text.isEmpty() || entry.value("message").toString().contains(text,Qt::CaseInsensitive))) {
            records.append(entry); if (records.size() >= limit) break;
        }
        return QVariantMap{{"records",records}, {"first_cursor",journal_.empty() ? journalCursor_ : journal_.front().value("cursor")},
            {"latest_cursor",QVariant::fromValue(journalCursor_)}, {"gap",!journal_.empty() && after + 1 < journal_.front().value("cursor").toULongLong()},
            {"count",static_cast<qulonglong>(journal_.size())},{"retained_bytes",journalBytes_},{"detail",journalDetail_}};
    }
    if (op == "log.clear") { journal_.clear(); journalBytes_ = 0; return QVariantMap{{"cleared",true}}; }
    throw Error("未知运行操作：" + op);
}

void Engine::publishObservation(Clock::time_point now, bool force)
{
    if (!force && now - lastPublished_ < std::chrono::milliseconds(50)) return;
    QVariantMap nodeStates, wires;
    QHash<QString,QVariantMap> io;
    { std::lock_guard guard(queueMutex_); io = ioStates_; }
    for (const auto &[id,node] : nodes_) {
        auto state = node->snapshot(false);
        if (io.contains(id)) state["state"] = io.value(id);
        nodeStates.insert(id,state);
    }
    for (const auto &wire : routes_) wires.insert(wire.id,QVariantMap{{"count",QVariant::fromValue(wire.count)},{"step_count",QVariant::fromValue(wire.steps)},{"connected",wire.connected}});
    QVariantMap cursor;
    if (batch_) {
        QString next;
        for (auto at = batch_->cursor; at < order_.size(); ++at) if (batch_->touched.contains(order_[at]) && !nodes_.at(order_[at])->deferredInputs()) { next = order_[at]; break; }
        if (next.isEmpty() && batch_->ioCursor < batch_->deferredIO.size()) next = batch_->deferredIO[batch_->ioCursor];
        cursor = {{"batch",QVariant::fromValue(batch_->context.batch)}, {"source",batch_->source}, {"next_node",next}};
    }
    auto state = std::make_shared<const QVariantMap>(QVariantMap{{"active",active_},{"running",running_},{"debug",debug_},{"elapsed",elapsed(now)},
        {"processed",QVariant::fromValue(processed_)},{"failed",QVariant::fromValue(failed_)},{"batch",QVariant::fromValue(batchSequence_)},
        {"epoch",QVariant::fromValue(epoch_)},{"nodes",nodeStates},{"wires",wires},{"cursor",cursor},{"sources",availableSources_},{"current_node",currentNode_},{"errors",nodeErrors_},{"log_detail",journalDetail_},
        {"latest_log",journal_.empty() ? QVariantMap{} : journal_.back()}});
    published_.store(state); lastPublished_ = now;
}

void Engine::run()
{
    lastPublished_ = Clock::now(); resumedAt_ = lastPublished_;
    while (!quitting_) {
        try {
            std::optional<Request> request;
            QString overflow;
            {
                std::lock_guard guard(queueMutex_);
                if (!requests_.empty()) { request = std::move(requests_.front()); requests_.pop_front(); }
                overflow = std::move(overflowError_); overflowError_.clear();
            }
            if (!overflow.isEmpty()) {
                elapsedBase_ = elapsed(Clock::now()); running_ = false; accepting_ = false; ++failed_; record("error",overflow);
            }
            if (request) {
                try {
                    const auto result = dispatch(*request);
                    const bool read = request->operation == "view.read" || request->operation == "node.state" || request->operation == "log.query" || request->operation == "python.probe";
                    publishObservation(Clock::now(),!read); emit completed(request->id,result,{});
                } catch (const std::exception &error) { const auto message = QString::fromUtf8(error.what()); record("error",message,request->arguments.value("node").toString(),{{"kind","operation_error"},{"operation",request->operation}}); publishObservation(Clock::now(),true); emit completed(request->id,{},message); }
                // Service a batch after each request while running. A large
                // collection of visible views cannot starve real computation.
            }
            const auto now = Clock::now();
            if (running_) {
                if (!batch_) {
                    const auto event = nextEvent(now);
                    if (event) beginBatch(*event,false);
                }
                if (batch_) advanceBatch();
            }
            publishObservation(Clock::now());
            if (!running_ || (readySources_.isEmpty() && pendingFrames_.empty())) {
                auto wake = Clock::now() + std::chrono::milliseconds(50);
                if (running_) for (auto i = deadlines_.cbegin(); i != deadlines_.cend(); ++i) wake = std::min(wake,i.value());
                std::unique_lock lock(queueMutex_);
                condition_.wait_until(lock,wake,[&] { return quitting_ || !requests_.empty() || (running_ && !inputQueue_.empty()) || !overflowError_.isEmpty(); });
            }
        } catch (const std::exception &error) {
            const auto node = currentNode_;
            elapsedBase_ = elapsed(Clock::now()); running_ = false; accepting_ = false;
            if (stopRequested_ || quitting_) {
                // Global controls intentionally interrupt long plugin calls;
                // retained completed state is not an algorithm execution error.
                record("info","引擎控制已中断未完成批次，已执行状态保留",node,{{"kind","execution_interrupted"}});
            } else {
                ++failed_; nodeErrors_[node] = QString::fromUtf8(error.what());
                record("error",QString::fromUtf8(error.what()),node,{{"kind","execution_error"}});
            }
            batch_.reset(); publishObservation(Clock::now(),true);
        }
    }
    endSession(); nodes_.clear();
}

} // namespace corig
