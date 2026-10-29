#pragma once

#include "core/node.h"

namespace corig {

// Periodic and one-shot timing use engine time, so global pause freezes them.
// Repeated starts always restart the full interval; stop wins within a batch.
class TimerNode final : public Node {
public:
    TimerNode(QVariantMap design, QVariantMap definition, QVector<Port> ports, NodeHost host);
    // Remaining delay is queried by the shared engine scheduler.
    double sourceInterval() const override;
    // Changing timing parameters restarts an enabled timer with the new interval.
    void configure(Node &prepared) override;
    // Ending the engine session disables this source without retaining a task.
    void stop() override;
    // Countdown observations derive from the same frozen experiment clock.
    QVariantMap snapshot(bool detailed = false) const override;
    // Start/stop share exactly the same transitions as the boolean inputs.
    NodeResult action(const QString &name, const QVariantMap &args, const BatchContext &context, QVariant &result) override;
protected:
    NodeResult process(const Values &changes, const BatchContext &context, bool tick) override;
private:
    double due_ = 0., lastTime_ = 0.;
    bool enabled_ = false;
    quint64 ticks_ = 0;
};

} // namespace corig
