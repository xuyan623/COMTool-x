#pragma once

#include "core/node.h"
#include <deque>
#include <random>
class QJSEngine;
class QJSValue;

namespace cx {

// Sources and scalar math share the engine's batch clock. History-bearing
// functions re-anchor time after pause while preserving accumulated results.
class MathNode final : public Node {
public:
    MathNode(QVariantMap design, QVariantMap definition, QVector<Port> ports, NodeHost host);
    ~MathNode() override;
    double sourceInterval() const override;
    void resume() override;
    void pause() override;
    void configure(Node &prepared) override;
    NodeResult action(const QString &name, const QVariantMap &args, const BatchContext &context, QVariant &result) override;
    QVariantMap snapshot(bool detailed = false) const override;
protected:
    NodeResult process(const Values &changes, const BatchContext &context, bool tick) override;
private:
    void resetHistory();
    void compileExpressions();
    std::unique_ptr<QJSEngine> expressionEngine_;
    std::unique_ptr<QJSValue> expression_;
    QStringList expressionInputs_, expressionOutputs_;
    std::mt19937_64 random_;
    std::deque<double> window_;
    double previousTime_ = 0., previousX_ = 0., value_ = 0.;
    bool anchored_ = false, timerEnabled_ = false;
};

} // namespace cx
