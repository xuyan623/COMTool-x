#include "timer_node.h"
#include <algorithm>
#include <cmath>

namespace cx {

TimerNode::TimerNode(QVariantMap design, QVariantMap definition, QVector<Port> ports, NodeHost host)
    : Node(std::move(design),std::move(definition),std::move(ports),std::move(host)) {}

double TimerNode::sourceInterval() const
{
    const auto now = host_.time ? host_.time() : lastTime_;
    return enabled_ ? std::max(.000001,due_ - now) : -1.;
}

void TimerNode::configure(Node &prepared)
{
    const bool changed = config_ != prepared.config();
    Node::configure(prepared);
    if (enabled_ && changed) due_ = (host_.time ? host_.time() : lastTime_) + config_.value("interval_ms").toDouble() / 1000.;
}

void TimerNode::stop() { enabled_ = false; }

QVariantMap TimerNode::snapshot(bool detailed) const
{
    auto result = Node::snapshot(detailed);
    const auto now = host_.time ? host_.time() : lastTime_;
    result["state"] = QVariantMap{{"enabled",enabled_},{"ticks",QVariant::fromValue(ticks_)},
        {"remaining_seconds",enabled_ ? std::max(0.,due_ - now) : 0.},{"mode",config_.value("mode")}};
    return result;
}

NodeResult TimerNode::action(const QString &name, const QVariantMap &args, const BatchContext &context, QVariant &result)
{
    if (name != "start" && name != "stop") return Node::action(name,args,context,result);
    if (args.contains("interval_ms")) {
        const auto interval = finiteNumber(args.value("interval_ms"),"触发间隔");
        if (interval < 1. || interval > 3600000. || interval != std::floor(interval)) throw Error("触发间隔需要 1～3600000 的整数毫秒");
        config_["interval_ms"] = interval;
    }
    const auto emission = process({{name,true}},context,false);
    result = snapshot(false).value("state"); return emission;
}

NodeResult TimerNode::process(const Values &changes, const BatchContext &context, bool tick)
{
    lastTime_ = context.time;
    NodeResult result;
    if (changes.value("stop").toBool()) { enabled_ = false; result.rescheduleSource = true; return result; }
    if (changes.value("start").toBool()) {
        enabled_ = true; due_ = context.time + config_.value("interval_ms").toDouble() / 1000.;
        result.rescheduleSource = true; return result;
    }
    if (!tick || !enabled_) return result;
    // A stale scheduler wake after a restart must not emit prematurely.
    if (context.time + 1e-9 >= due_) {
        ++ticks_; result.emissions.append(Values{{"tick",true}});
        if (config_.value("mode") == "countdown") enabled_ = false;
        else due_ = context.time + config_.value("interval_ms").toDouble() / 1000.;
    }
    result.rescheduleSource = true; return result;
}

} // namespace cx
