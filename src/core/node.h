#pragma once

// A runtime node owns its processing state. The engine owns input retention,
// route validity and batch ordering; views consume observations, never this object.
#include "catalog.h"
#include <functional>
#include <memory>

namespace corig {

struct BatchContext {
    quint64 batch = 0;
    double time = 0.;
    bool stepping = false;
    bool resumed = false;
};

struct CORIG_API NodeResult {
    QVector<Values> emissions;
    QSet<QString> disconnected;
    bool separateFrames = false;
    // Stateful sources request a new deadline after input-driven transitions.
    bool rescheduleSource = false;
    static NodeResult output(const Values &values);
};

struct NodeHost {
    // These callbacks cross actual execution seams: asynchronous IO and the
    // separately running local Python interpreter, not a duplicate data graph.
    std::function<QVariantMap(const QString &, const QString &, const QVariantMap &)> io;
    std::function<QVariantMap(const QVariantMap &)> python;
    std::function<QByteArray(const QString &)> resource;
    std::function<void(const QString &, const QString &)> log;
    std::function<void(const QString &, const Values &, const BatchContext &)> publish;
    std::function<bool(const QString &)> cancelled;
    // Engine experiment time freezes during global pause and resets with it.
    std::function<double()> time;
    std::function<void()> stopExtensions;
    // Installed selections belong to the application session, not to the
    // graph worker. Both use the same immutable catalog for a candidate design.
    std::function<Catalog(const QVariantMap &)> catalog;
    std::function<QVariantMap(const PluginPackage &,const QString &,const QString &,const QVariantMap &)> service;
    std::function<QVariantMap(const QVariantMap &)> projectReadiness;
};

class CORIG_API Node {
public:
    Node(QVariantMap design, QVariantMap definition, QVector<Port> ports, NodeHost host);
    virtual ~Node() = default;
    const QString &id() const { return id_; }
    const QString &kind() const { return kind_; }
    const QVariantMap &config() const { return config_; }
    const QVector<Port> &ports() const { return ports_; }
    const Values &inputs() const { return inputs_; }
    const Values &outputs() const { return outputs_; }
    const QSet<QString> &connectedInputs() const { return connected_; }
    bool deferredInputs() const { return definition_.value("defer_inputs").toBool(); }
    NodeResult execute(const Values &changes, const QSet<QString> &disconnected, const BatchContext &context, bool tick = false);
    virtual NodeResult action(const QString &name, const QVariantMap &arguments, const BatchContext &context, QVariant &result);
    virtual NodeResult start(const BatchContext &context);
    virtual void configure(Node &prepared);
    virtual double sourceInterval() const;
    virtual void resume();
    virtual void pause();
    virtual void stop();
    virtual QVariantMap snapshot(bool detailed = false) const;
    virtual QVariantMap view(const QString &name, const QVariantMap &arguments) const;
    void setConnections(const QSet<QString> &ports, const QSet<QString> &reset = {});
    void retainEmission(const Values &values);
protected:
    virtual NodeResult process(const Values &changes, const BatchContext &context, bool tick) = 0;
    double number(const QString &port) const;
    QVariantMap design_, definition_, config_, state_;
    QString id_, kind_;
    QVector<Port> ports_;
    Values inputs_, outputs_;
    QSet<QString> connected_;
    NodeHost host_;
    quint64 executions_ = 0;
    bool resumed_ = true;
};

CORIG_API std::unique_ptr<Node> makeNode(const QVariantMap &design, const Catalog &catalog, const NodeHost &host);

} // namespace corig
