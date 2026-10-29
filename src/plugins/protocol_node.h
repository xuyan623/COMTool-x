#pragma once

#include "core/node.h"
#include <QTextCodec>
#include <optional>

namespace corig {

QByteArray encodeText(const QString &text, const QString &encoding = "utf-8", bool hex = false, bool escape = false, const QString &newline = "None");
QString decodeText(const QByteArray &bytes, const QString &encoding);
QByteArray checkedHex(const QString &text);

// Stream framing is retained across input chunks, but discarded on pause so
// a fragment received before a discontinuity cannot fabricate a later frame.
class ProtocolNode final : public Node {
public:
    ProtocolNode(QVariantMap design, QVariantMap definition, QVector<Port> ports, NodeHost host);
    void pause() override;
    void configure(Node &prepared) override;
    NodeResult action(const QString &name, const QVariantMap &args, const BatchContext &context, QVariant &result) override;
protected:
    NodeResult process(const Values &changes, const BatchContext &context, bool tick) override;
private:
    QVariantMap numericFrame(QVector<double> values, double arrival);
    std::optional<QVariantMap> decodeFrame(const QString &format, double arrival);
    void badFrame(const QString &reason);
    QByteArray buffer_, header_;
    qsizetype detectedCount_ = 0;
    quint64 sampleIndex_ = 0;
    std::unique_ptr<QTextDecoder> textDecoder_;
};

std::unique_ptr<Node> makePythonNode(QVariantMap design, QVariantMap definition, QVector<Port> ports, NodeHost host);

} // namespace corig
