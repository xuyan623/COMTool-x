#pragma once

#include "core/node.h"
#include <QFile>
#include <deque>

namespace cx {

// Controls, explicit routing and byte tools share no hidden timing loop. IO
// belongs to the host's separate event thread and reports actual completion.
class DataNode final : public Node {
public:
    DataNode(QVariantMap design, QVariantMap definition, QVector<Port> ports, NodeHost host);
    NodeResult start(const BatchContext &context) override;
    double sourceInterval() const override;
    void configure(Node &prepared) override;
    void stop() override;
    QVariantMap snapshot(bool detailed = false) const override;
    QVariantMap view(const QString &name, const QVariantMap &arguments) const override;
    NodeResult action(const QString &name, const QVariantMap &args, const BatchContext &context, QVariant &result) override;
protected:
    NodeResult process(const Values &changes, const BatchContext &context, bool tick) override;
private:
    QVariant controlValue(const QVariant &value) const;
    QByteArray sendBytes(const QVariantMap &args) const;
    QFile recorder_;
    QByteArray received_, transmitted_, periodicBytes_;
    struct Packet { quint64 first; QString timestamp; QByteArray bytes; };
    std::deque<Packet> receivePackets_;
    quint64 receivedTotal_ = 0, transmittedTotal_ = 0;
    double periodicInterval_ = -1.;
};

} // namespace cx
