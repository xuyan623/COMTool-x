#include "logic_node.h"

namespace cx {

LogicNode::LogicNode(QVariantMap design, QVariantMap definition, QVector<Port> ports, NodeHost host)
    : Node(std::move(design),std::move(definition),std::move(ports),std::move(host))
{
    state_ = {{"value",false},{"transitions",0}};
}

void LogicNode::configure(Node &prepared)
{
    if (config_ != prepared.config()) { previous_ = false; state_["value"] = false; }
    Node::configure(prepared);
}

NodeResult LogicNode::process(const Values &changes, const BatchContext &, bool)
{
    if (kind_ == "logic.edge") {
        if (!connected_.contains("in")) {
            previous_ = false;
            NodeResult result; result.disconnected.insert("out"); return result;
        }
        if (!changes.contains("in")) return {};
        const bool value = changes.value("in").toBool();
        const bool rising = !previous_ && value, falling = previous_ && !value;
        previous_ = value; state_["value"] = value;
        const auto mode = config_.value("edge").toString();
        if ((mode == "rising" && rising) || (mode == "falling" && falling) || (mode == "both" && (rising || falling))) {
            state_["transitions"] = state_.value("transitions").toULongLong() + 1;
            return NodeResult::output({{"out",true}});
        }
        return {};
    }
    if (!connected_.contains("a") || (kind_ != "logic.not" && !connected_.contains("b"))) {
        NodeResult result; result.disconnected.insert("out"); return result;
    }
    const bool a = inputs_.value("a").toBool(), b = inputs_.value("b").toBool();
    bool value;
    if (kind_ == "logic.and") value = a && b;
    else if (kind_ == "logic.or") value = a || b;
    else if (kind_ == "logic.not") value = !a;
    else if (kind_ == "logic.nand") value = !(a && b);
    else if (kind_ == "logic.nor") value = !(a || b);
    else if (kind_ == "logic.xor") value = a != b;
    else if (kind_ == "logic.xnor") value = a == b;
    else throw Error("未知逻辑门：" + kind_);
    state_["value"] = value;
    return NodeResult::output({{"out",value}});
}

} // namespace cx
