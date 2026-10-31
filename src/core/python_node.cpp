#include "native_plugin.h"
#include "resource_policy.h"

namespace corig {

class PythonNode final : public Node {
public:
    using Node::Node;
    NodeResult start(const BatchContext &context) override {
        Node::start(context); active_ = true;
        if (definition_.value("implementation") == "python") call({},{{"__config__",config_}},"lifecycle",{{"phase","start"},{"metadata",true}});
        return {};
    }
    void resume() override {
        Node::resume();
        if (loaded_) call({},{{"__config__",config_}},"lifecycle",{{"phase","resume"},{"metadata",true}});
    }
    void pause() override {
        if (loaded_) call({},{{"__config__",config_}},"lifecycle",{{"phase","pause"},{"metadata",true}});
    }
    void stop() override {
        active_ = false;
        if (!loaded_) return;
        try {
            call({},{{"__config__",config_}},"lifecycle",{{"phase","stop"},{"metadata",true}});
            host_.python({{"package",definition_.value("package_id","scripts")},{"op","drop"},{"node",id_},{"metadata",true}});
        } catch (const std::exception &error) { if (host_.log) host_.log("error",QString::fromUtf8(error.what())); }
        loaded_ = false;
    }
    void configure(Node &prepared) override {
        // Producers read this current configuration even when no graph input
        // arrives. Business validation already ran on the complete candidate.
        if (loaded_) call({},{{"__config__",prepared.config()}},"configuration",{{"metadata",true}});
        Node::configure(prepared);
    }
    double sourceInterval() const override { return definition_.contains("source_interval_ms") ? definition_.value("source_interval_ms").toDouble() / 1000. : -1.; }
    NodeResult action(const QString &name, const QVariantMap &args, const BatchContext &context, QVariant &result) override
    {
        if (name == "reset_state") {
            if (loaded_ && host_.python) host_.python({{"package",definition_.value("package_id","scripts")},{"op","drop"},{"node",id_},{"metadata",true}});
            loaded_ = false; scriptState_.clear(); state_["script_state"] = scriptState_; resumed_ = true;
            if (active_) { start(context); pause(); }
            result = snapshot(); return {};
        }
        if (definition_.value("implementation") == "python") {
            // Actions receive the same current inputs/configuration as process,
            // so a UI/API action can compute against the active experiment.
            QVariantMap inputs{{"args",args},{"__config__",config_},{"__context__",QVariantMap{{"time",context.time},{"batch",QVariant::fromValue(context.batch)},{"changed",QStringList{}},{"source_tick",false}}}};
            for (auto i = inputs_.cbegin(); i != inputs_.cend(); ++i) inputs.insert(i.key(),i.value());
            const auto contract = definition_.value("actions").toMap().value(name).toMap();
            const auto response = call("action_" + name, inputs,contract.value("async").toBool() ? "task.start" : "call",{{"pause_sensitive",!contract.value("requires_paused").toBool()}});
            const auto returned = response.value("result").toMap(); result = returned.value("result");
            if (returned.contains("python_task")) { result = returned; return {}; }
            Values outputs;
            const auto values = returned.value("outputs").toMap();
            for (auto i = values.cbegin(); i != values.cend(); ++i) outputs.insert(i.key(),i.value());
            return NodeResult::output(outputs);
        }
        return Node::action(name,args,context,result);
    }
protected:
    NodeResult process(const Values &changes, const BatchContext &context, bool tick) override
    {
        QVariantMap inputs;
        for (auto i = inputs_.cbegin(); i != inputs_.cend(); ++i) inputs.insert(i.key(),i.value());
        // Retained levels and newly delivered commands are distinct even when
        // both carry true. Scripts see the same changed ports as native nodes.
        inputs["__context__"] = QVariantMap{{"time",context.time}, {"resumed",context.resumed}, {"batch",QVariant::fromValue(context.batch)},
            {"changed",changes.keys()},{"source_tick",tick}};
        inputs["__config__"] = config_;
        auto entry = definition_.value("entry","process").toString();
        if (kind_ == "protocol.python") {
            if (!changes.contains("in")) return {};
            entry = config_.value("operation").toString(); inputs["data"] = changes.value("in");
            inputs["discontinuity"] = context.resumed;
        }
        auto value = call(entry, inputs).value("result");
        if (!value.isValid() || value.isNull()) return {};
        if (kind_ == "protocol.python") {
            if (value.metaType().id() == QMetaType::QByteArray) return NodeResult::output({{"bytes",value}});
            NodeResult result; result.separateFrames = true;
            for (const auto &frame : sampleFrames(value)) result.emissions.append({{"samples",frame}});
            return result;
        }
        auto values = value.toMap();
        const auto names = config_.value("outputs").toStringList();
        if (kind_ == "function.python" && names.size() == 1 && value.metaType().id() != QMetaType::QVariantMap) values.insert(names[0],finiteNumber(value,names[0]));
        Values outputs;
        for (auto i = values.cbegin(); i != values.cend(); ++i) outputs.insert(i.key(),i.value());
        if (kind_ == "function.python") {
            for (const auto &name : names) if (!values.contains(name)) throw Error("Python 缺少输出：" + name);
            if (values.size() != names.size()) throw Error("Python 输出与声明不一致");
            outputs.insert("samples", QVariantMap{{"time",context.time},{"values",values}});
        }
        state_["values"] = values;
        return NodeResult::output(outputs);
    }
private:
    QVariantMap call(const QString &entry, const QVariantMap &inputs, const QString &operation = "call", const QVariantMap &options = {})
    {
        if (!host_.python) throw Error("请先在设置中选择本地 Python 环境");
        const auto code = definition_.value("implementation") == "python" ? definition_.value("code").toString() : config_.value("code").toString();
        QVariantMap request{{"package",definition_.value("package_id","scripts")},{"node",id_},{"op",operation},{"code",code},{"entry",entry},{"inputs",inputs},{"state",scriptState_},
            {"resources",definition_.value("resources",QVariantMap{})},{"dependencies",definition_.value("dependencies",QVariantList{})},{"timeout_ms",config_.value("timeout_ms",resourceLimit("python_timeout_ms"))}};
        if (!definition_.value("package_directory").toString().isEmpty()) {
            request.remove("code"); request.remove("resources");
            request["directory"] = definition_.value("package_directory"); request["code_file"] = definition_.value("code_file");
        }
        for (auto i = options.cbegin(); i != options.cend(); ++i) request[i.key()] = i.value();
        const auto response = host_.python(request);
        scriptState_ = response.value("state").toMap(); state_["script_state"] = scriptState_;
        loaded_ = true;
        if (!response.value("logs").toString().isEmpty() && host_.log) host_.log("info",response.value("logs").toString());
        return response;
    }
    QVariantMap scriptState_;
    bool loaded_ = false;
    bool active_ = false;
};

std::unique_ptr<Node> makePythonNode(QVariantMap design, QVariantMap definition, QVector<Port> ports, NodeHost host)
{
    return std::make_unique<PythonNode>(std::move(design),std::move(definition),std::move(ports),std::move(host));
}


} // namespace corig
