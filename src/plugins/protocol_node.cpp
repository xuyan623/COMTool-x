#include "protocol_node.h"
#include <QJsonDocument>
#include <QRegularExpression>
#include <QtEndian>
#include <bit>
#include <cmath>

namespace corig {

QByteArray checkedHex(const QString &text)
{
    auto normalized = text;
    normalized.remove(QRegularExpression("\\s+"));
    if (normalized.size() % 2 || normalized.contains(QRegularExpression("[^0-9A-Fa-f]"))) throw Error("HEX 需要完整的十六进制字节");
    return QByteArray::fromHex(normalized.toLatin1());
}

QByteArray encodeText(const QString &text, const QString &encoding, bool hex, bool escape, const QString &newline)
{
    QByteArray result;
    if (hex) result = checkedHex(text);
    else {
        auto codec = QTextCodec::codecForName(encoding.toUtf8());
        if (!codec) throw Error("不支持的文本编码：" + encoding);
        QString content = text;
        if (escape) {
            QString expanded;
            for (qsizetype i = 0; i < content.size(); ++i) {
                if (content[i] != '\\' || i + 1 >= content.size()) { expanded += content[i]; continue; }
                const auto code = content[++i];
                if (code == 'n') expanded += '\n';
                else if (code == 'r') expanded += '\r';
                else if (code == 't') expanded += '\t';
                else if (code == '0') expanded += QChar(0);
                else if (code == '\\') expanded += '\\';
                else if (code == 'x' && i + 2 < content.size()) {
                    bool ok; const auto number = content.mid(i + 1, 2).toUInt(&ok,16);
                    if (!ok) throw Error("\\x 后需要两个十六进制字符");
                    expanded += QChar(number); i += 2;
                } else { expanded += '\\'; expanded += code; }
            }
            content = expanded;
        }
        result = codec->fromUnicode(content);
    }
    if (newline == "LF") result += '\n';
    else if (newline == "CRLF") result += "\r\n";
    else if (newline != "None" && !newline.isEmpty()) throw Error("未知换行方式");
    return result;
}

QString decodeText(const QByteArray &bytes, const QString &encoding)
{
    auto codec = QTextCodec::codecForName(encoding.toUtf8());
    if (!codec) throw Error("不支持的文本编码：" + encoding);
    return codec->toUnicode(bytes);
}

ProtocolNode::ProtocolNode(QVariantMap design, QVariantMap definition, QVector<Port> ports, NodeHost host)
    : Node(std::move(design), std::move(definition), std::move(ports), std::move(host))
{
    if (kind_ == "protocol.decode") {
        header_ = checkedHex(config_.value("header").toString());
        if (header_.isEmpty() || header_.size() > 64) throw Error("二进制帧头需要 1～64 字节");
    }
    if (kind_ == "protocol.text") {
        auto codec = QTextCodec::codecForName(config_.value("encoding").toByteArray());
        if (!codec) throw Error("不支持的文本编码");
        textDecoder_.reset(codec->makeDecoder());
    }
    state_ = {{"frames",0}, {"bad_frames",0}, {"buffered_bytes",0}, {"channel_count",0}, {"last_error",""}};
}

void ProtocolNode::pause()
{
    buffer_.clear(); state_["buffered_bytes"] = 0;
    if (textDecoder_) textDecoder_.reset(QTextCodec::codecForName(config_.value("encoding").toByteArray())->makeDecoder());
}

void ProtocolNode::configure(Node &prepared)
{
    auto &candidate = dynamic_cast<ProtocolNode &>(prepared);
    const bool changed = prepared.config() != config_;
    Node::configure(prepared);
    header_ = candidate.header_; textDecoder_ = std::move(candidate.textDecoder_);
    if (changed) { buffer_.clear(); detectedCount_ = 0; }
}

void ProtocolNode::badFrame(const QString &reason)
{
    const auto count = state_.value("bad_frames").toULongLong() + 1;
    state_["bad_frames"] = count; state_["last_error"] = reason;
    if (host_.log && (count <= 8 || count % 1000 == 0)) host_.log("warning", "解码丢弃无效帧：" + reason);
}

QVariantMap ProtocolNode::numericFrame(QVector<double> values, double arrival)
{
    if (values.isEmpty() || values.size() > 128) throw Error("采样数值字段数量无效");
    for (const auto value : values) if (!std::isfinite(value)) throw Error("采样包含非有限数值");
    const auto count = config_.value("channel_count").toInt() ? config_.value("channel_count").toInt() : detectedCount_ ? detectedCount_ : values.size();
    if (count != values.size()) throw Error("采样字段数量与配置不一致");
    detectedCount_ = count;
    const auto timestampIndex = config_.value("timestamp_index").toInt();
    double time;
    if (timestampIndex >= 0) {
        if (timestampIndex >= values.size()) throw Error("时间字段索引超出范围");
        time = values.takeAt(timestampIndex) * timeUnit(config_.value("timestamp_unit").toString());
    } else time = config_.value("time_mode") == "arrival" ? arrival : sampleIndex_ / config_.value("sample_rate_hz").toDouble();
    const auto names = config_.value("channels").toStringList();
    if (!names.isEmpty() && names.size() != values.size()) throw Error("通道名数量与数值数量不一致");
    QVariantMap channels;
    for (qsizetype i = 0; i < values.size(); ++i) channels.insert(names.isEmpty() ? "CH" + QString::number(i) : names[i], values[i]);
    return {{"time",time}, {"values",channels}};
}

std::optional<QVariantMap> ProtocolNode::decodeFrame(const QString &format, double arrival)
{
    if (format == "firewater" || format == "named_ascii") {
        if (format == "named_ascii") {
            const auto start = buffer_.indexOf('$');
            if (start < 0) { buffer_.clear(); return {}; }
            buffer_.remove(0,start);
        }
        const auto end = buffer_.indexOf('\n');
        if (end < 0) return {};
        auto line = buffer_.left(end).trimmed(); buffer_.remove(0,end + 1);
        if (format == "named_ascii") {
            line = line.mid(line.lastIndexOf('$'));
            const auto parts = line.mid(1).split(',');
            if (parts.size() != 3 && parts.size() != 4) throw Error("命名 ASCII 需要 $name,time,value[,checksum]");
            if (parts.size() == 4) {
                int checksum = 0; for (const auto byte : line.left(line.lastIndexOf(','))) checksum += static_cast<unsigned char>(byte);
                bool ok; const auto expected = parts[3].toInt(&ok);
                if (!ok || checksum % 256 != expected) throw Error("命名 ASCII 校验失败");
            }
            bool timeOk, valueOk;
            const auto time = parts[1].toDouble(&timeOk), value = parts[2].toDouble(&valueOk);
            if (!timeOk || !valueOk || parts[0].isEmpty()) throw Error("命名 ASCII 数值或名称无效");
            return QVariantMap{{"time",time * timeUnit(config_.value("timestamp_unit").toString())}, {"values",QVariantMap{{QString::fromUtf8(parts[0]),value}}}};
        }
        if (line.startsWith("image:")) throw Error("当前解码器只接收数值采样帧");
        if (line.contains(':')) line = line.mid(line.indexOf(':') + 1);
        QVector<double> numbers;
        for (const auto &field : line.split(',')) { bool ok; const auto value = field.trimmed().toDouble(&ok); if (!ok) throw Error("FireWater 数值无效"); numbers.append(value); }
        return numericFrame(numbers,arrival);
    }
    if (format == "justfloat") {
        const auto end = buffer_.indexOf(QByteArray::fromHex("0000807f"));
        if (end < 0) return {};
        const auto body = buffer_.left(end); buffer_.remove(0,end + 4);
        if (body.isEmpty() || body.size() % 4) throw Error("JustFloat 帧长度必须是 4 的倍数");
        QVector<double> values;
        for (qsizetype i = 0; i < body.size(); i += 4) values.append(std::bit_cast<float>(qFromLittleEndian<quint32>(body.constData() + i)));
        return numericFrame(values,arrival);
    }
    const auto start = buffer_.indexOf(header_);
    if (start < 0) { buffer_ = buffer_.right(header_.size() - 1); return {}; }
    buffer_.remove(0,start);
    if (buffer_.size() < header_.size() + 1) return {};
    const auto nameLength = static_cast<unsigned char>(buffer_[header_.size()]);
    const auto length = header_.size() + 1 + nameLength + 17;
    if (buffer_.size() < length) return {};
    const auto frame = buffer_.left(length);
    int checksum = 0; for (const auto byte : frame.first(length - 1)) checksum += static_cast<unsigned char>(byte);
    if (!nameLength || checksum % 256 != static_cast<unsigned char>(frame.back())) { buffer_.remove(0,1); throw Error("命名二进制校验失败"); }
    buffer_.remove(0,length);
    const auto time = std::bit_cast<double>(qFromLittleEndian<quint64>(frame.constData() + length - 17));
    const auto value = std::bit_cast<double>(qFromLittleEndian<quint64>(frame.constData() + length - 9));
    return QVariantMap{{"time",time * timeUnit(config_.value("timestamp_unit").toString())}, {"values",QVariantMap{{QString::fromUtf8(frame.mid(header_.size() + 1,nameLength)),value}}}};
}

NodeResult ProtocolNode::process(const Values &changes, const BatchContext &context, bool)
{
    if (kind_ == "protocol.encode") {
        if (!changes.contains("value")) return {};
        const auto value = changes.value("value");
        QString text;
        if (config_.value("format") == "json") {
            const auto encoded = QJsonDocument::fromVariant(QVariantList{toJsonValue(value)}).toJson(QJsonDocument::Compact);
            text = QString::fromUtf8(encoded.mid(1,encoded.size() - 2));
        } else {
            const auto representation = value.metaType().id() == QMetaType::QVariantMap || value.metaType().id() == QMetaType::QVariantList
                ? QString::fromUtf8(QJsonDocument::fromVariant(value).toJson(QJsonDocument::Compact)) : value.toString();
            text = config_.value("template").toString(); text.replace("{value}",representation);
        }
        return NodeResult::output({{"bytes",encodeText(text,config_.value("encoding").toString(),config_.value("format") == "hex",config_.value("escape").toBool(),config_.value("newline").toString())}});
    }
    if (!changes.contains("bytes")) return {};
    const auto data = changes.value("bytes").toByteArray();
    if (kind_ == "protocol.text") return NodeResult::output({{"text",textDecoder_->toUnicode(data)}});
    buffer_ += data;
    if (buffer_.size() > 1024 * 1024) { buffer_.clear(); badFrame("未成帧数据超过 1 MiB"); }
    NodeResult result; result.separateFrames = true;
    while (!buffer_.isEmpty()) {
        try {
            const auto frame = decodeFrame(config_.value("format").toString(), context.time);
            if (!frame) break;
            sampleFrames(*frame);
            ++sampleIndex_; state_["frames"] = sampleIndex_; state_["channel_count"] = frame->value("values").toMap().size();
            result.emissions.append({{"samples",*frame}});
        } catch (const Error &error) { badFrame(error.message); }
    }
    state_["buffered_bytes"] = buffer_.size();
    return result;
}

NodeResult ProtocolNode::action(const QString &name, const QVariantMap &args, const BatchContext &context, QVariant &result)
{
    if (name == "clear") {
        buffer_.clear(); sampleIndex_ = 0; detectedCount_ = 0;
        state_ = {{"frames",0}, {"bad_frames",0}, {"buffered_bytes",0}, {"channel_count",0}, {"last_error",""}};
        result = snapshot(); return {};
    }
    return Node::action(name,args,context,result);
}

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
