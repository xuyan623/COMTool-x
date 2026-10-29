#pragma once

#include "core/node.h"

namespace corig {

// Boolean gates retain input levels; edge detection publishes one true update
// per selected transition. No distinct event type or hidden polling is used.
class LogicNode final : public Node {
public:
    LogicNode(QVariantMap design, QVariantMap definition, QVector<Port> ports, NodeHost host);
    // Configuration changes establish a new edge baseline at false.
    void configure(Node &prepared) override;
protected:
    // Evaluate new levels once per engine batch, preserving same-value updates.
    NodeResult process(const Values &changes, const BatchContext &context, bool tick) override;
private:
    bool previous_ = false;
};

} // namespace corig
