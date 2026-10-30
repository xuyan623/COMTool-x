#include "native_plugin.h"

namespace corig {

class PythonNode final : public Node {
public:
    using Node::Node;
    double sourceInterval() const override { return definition_.contains("source_interval_ms") ? definition_.value("source_interval_ms").toDouble() / 1000. : -1.; }
    NodeResult action(const QString &name, const QVariantMap &args, const BatchContext &context, QVariant &result) override
    {
        if (name == "reset_state") { if (loaded_ && host_.python) host_.python({{"op","drop"},{"node",id_}}); loaded_ = false; scriptState_.clear(); state_["script_state"] = scriptState_; resumed_ = true; result = snapshot(); return {}; }
        if (definition_.value("implementation") == "python") {
            // Actions receive the same current inputs/configuration as process,
            // so a UI/API action can compute against the active experiment.
            QVariantMap inputs{{"args",args},{"__config__",config_},{"__context__",QVariantMap{{"time",context.time},{"batch",QVariant::fromValue(context.batch)},{"changed",QStringList{}},{"source_tick",false}}}};
            for (auto i = inputs_.cbegin(); i != inputs_.cend(); ++i) inputs.insert(i.key(),i.value());
            const auto response = call("action_" + name, inputs);
            const auto returned = response.value("result").toMap(); result = returned.value("result");
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
    QVariantMap call(const QString &entry, const QVariantMap &inputs)
    {
        if (!host_.python) throw Error("请先在设置中选择本地 Python 环境");
        const auto code = definition_.value("implementation") == "python" ? definition_.value("code").toString() : config_.value("code").toString();
        const auto response = host_.python({{"node",id_},{"code",code},{"entry",entry},{"inputs",inputs},{"state",scriptState_},
            {"resources",definition_.value("resources",QVariantMap{})},{"dependencies",definition_.value("dependencies",QVariantList{})},{"timeout_ms",config_.value("timeout_ms",1000)}});
        scriptState_ = response.value("state").toMap(); state_["script_state"] = scriptState_;
        loaded_ = true;
        if (!response.value("logs").toString().isEmpty() && host_.log) host_.log("info",response.value("logs").toString());
        return response;
    }
    QVariantMap scriptState_;
    bool loaded_ = false;
};

std::unique_ptr<Node> makePythonNode(QVariantMap design, QVariantMap definition, QVector<Port> ports, NodeHost host)
{
    return std::make_unique<PythonNode>(std::move(design),std::move(definition),std::move(ports),std::move(host));
}


} // namespace corig
