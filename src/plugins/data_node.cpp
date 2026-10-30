#include "data_node.h"
#include "protocol_node.h"
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QDateTime>

namespace corig {

DataNode::DataNode(QVariantMap design, QVariantMap definition, QVector<Port> ports, NodeHost host)
    : Node(std::move(design),std::move(definition),std::move(ports),std::move(host))
{
    if (kind_.startsWith("control.")) state_["value"] = config_.value("value", config_.value("checked", config_.value("text",0.)));
    if (kind_ == "data.gate") state_["enabled"] = false;
    if (kind_.startsWith("io.")) state_ = {{"connected",false}, {"rx_bytes",0}, {"tx_bytes",0}, {"clients",QVariantList{}}};
    if (kind_ == "data.recorder") state_ = {{"written_bytes",0}, {"path",config_.value("path")}};
    if (kind_ == "debug.console") state_ = {{"rx_bytes",0}, {"tx_requested_bytes",0}, {"periodic",false}, {"file_sending",false}};
}

QVariant DataNode::controlValue(const QVariant &value) const
{
    if (kind_ == "control.number" || kind_ == "control.slider") {
        const auto number = finiteNumber(value, "控件值");
        if (config_.contains("minimum") && number < config_.value("minimum").toDouble()) throw Error("控件值小于下限");
        if (config_.contains("maximum") && number > config_.value("maximum").toDouble()) throw Error("控件值超过上限");
        return number;
    }
    if (kind_ == "control.checkbox") return value.toBool();
    if (kind_ == "control.select") {
        const auto options = config_.value("choices").toList();
        if (!options.isEmpty() && !options.contains(value)) throw Error("控件值不在选项中");
    }
    return value.toString();
}

NodeResult DataNode::start(const BatchContext &)
{
    if (kind_.startsWith("control.") && kind_ != "control.button") {
        const auto value = controlValue(state_.value("value"));
        state_["value"] = value;
        const auto out = std::find_if(ports_.cbegin(), ports_.cend(), [](const auto &p) { return p.direction == "out"; });
        if (out != ports_.cend()) return NodeResult::output({{out->id,value}});
    }
    return {};
}

double DataNode::sourceInterval() const { return periodicInterval_; }

void DataNode::configure(Node &prepared)
{
    if (kind_ == "data.recorder" && prepared.config() != config_) recorder_.close();
    Node::configure(prepared);
    if (kind_.startsWith("control.")) state_["value"] = config_.value("value", config_.value("checked", config_.value("text",state_.value("value"))));
}

void DataNode::stop()
{
    periodicInterval_ = -1.; recorder_.close(); state_["periodic"] = false;
    if (kind_.startsWith("io.") && host_.io) {
        try { state_ = host_.io(id_,"disconnect",{}); }
        catch (const Error &error) { if (host_.log) host_.log("warning",error.message); }
    }
}

NodeResult DataNode::process(const Values &changes, const BatchContext &, bool tick)
{
    if (kind_.startsWith("io.")) {
        if (!host_.io) throw Error("通信执行器不可用");
        if (changes.contains("resize")) state_ = host_.io(id_,"resize",changes.value("resize").toMap());
        if (changes.contains("tx")) state_ = host_.io(id_,"send",{{"data",changes.value("tx")}});
        return {};
    }
    if (kind_ == "data.fork") {
        if (!connected_.contains("in")) { NodeResult r; for (const auto &name : config_.value("outputs").toStringList()) r.disconnected.insert(name); return r; }
        if (!changes.contains("in")) return {};
        Values result;
        for (const auto &name : config_.value("outputs").toStringList()) result.insert(name,changes.value("in"));
        return NodeResult::output(result);
    }
    if (kind_ == "data.extract") {
        if (!connected_.contains("samples")) {
            NodeResult r;
            for (const auto &port : ports_) if (port.direction == "out") r.disconnected.insert(port.id);
            return r;
        }
        if (!changes.contains("samples")) return {};
        const auto frames = sampleFrames(changes.value("samples"));
        if (frames.size() != 1) throw Error("通道提取每批次需要一个采样帧");
        const auto frame = frames[0].toMap(), channels = frame.value("values").toMap(), mapping = config_.value("channels").toMap();
        Values output{{"time",frame.value("time")}};
        QStringList missing;
        for (auto i = mapping.cbegin(); i != mapping.cend(); ++i) {
            if (channels.contains(i.value().toString())) output.insert(i.key(),channels.value(i.value().toString()));
            else missing.append(i.value().toString());
        }
        state_["last_time"] = frame.value("time"); state_["channels"] = channels.keys(); state_["missing"] = missing;
        return NodeResult::output(output);
    }
    if (kind_ == "data.gate" || kind_ == "data.select") {
        QString chosen;
        if (kind_ == "data.gate") {
            const bool enabled = inputs_.value("enabled").toBool(); state_["enabled"] = enabled;
            if (!enabled) { NodeResult r; r.disconnected.insert("out"); return r; }
            chosen = "in";
        } else { chosen = inputs_.value("select").toBool() ? "a" : "b"; state_["selected"] = chosen; }
        auto value = inputs_.value(chosen);
        if (!value.isValid()) value = defaultValue(config_.value("data_type").toString());
        if (!value.isValid()) { NodeResult r; r.disconnected.insert("out"); return r; }
        return NodeResult::output({{"out",value}});
    }
    if (kind_ == "display.value") {
        if (changes.contains("value")) state_["value"] = changes.value("value");
        return {};
    }
    if (kind_ == "data.recorder") {
        if (!changes.contains("bytes")) return {};
        if (!recorder_.isOpen()) {
            const auto path = config_.value("path").toString();
            if (!QDir().mkpath(QFileInfo(path).absolutePath())) throw Error("无法创建记录目录");
            recorder_.setFileName(path);
            if (!recorder_.open(QIODevice::WriteOnly | (config_.value("append").toBool() ? QIODevice::Append : QIODevice::Truncate))) throw Error("记录文件无法打开：" + recorder_.errorString());
            state_["path"] = path;
        }
        const auto bytes = changes.value("bytes").toByteArray();
        const auto content = config_.value("format") == "hex" ? QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toUtf8() + "  " + bytes.toHex(' ') + '\n' : bytes;
        if (recorder_.write(content) != content.size() || !recorder_.flush()) throw Error("写入记录失败：" + recorder_.errorString());
        state_["written_bytes"] = state_.value("written_bytes").toULongLong() + content.size();
        return NodeResult::output({{"through",bytes}});
    }
    if (kind_ == "debug.console") {
        Values output;
        if (changes.contains("rx")) {
            const auto bytes = changes.value("rx").toByteArray();
            receivePackets_.push_back({receivedTotal_,QDateTime::currentDateTime().toString("HH:mm:ss.zzz"),bytes});
            received_ += bytes; receivedTotal_ += bytes.size();
            received_ = received_.right(config_.value("history_bytes").toInt()); state_["rx_bytes"] = receivedTotal_;
            while (!receivePackets_.empty() && (receivePackets_.front().first + receivePackets_.front().bytes.size() <= receivedTotal_ - received_.size() || receivePackets_.size() > 4096)) receivePackets_.pop_front();
            output.insert("through",bytes);
        }
        if (changes.contains("send")) output.insert("tx",changes.value("send"));
        if (tick && periodicInterval_ > 0.) output.insert("tx",periodicBytes_);
        if (output.contains("tx")) {
            const auto bytes = output.value("tx").toByteArray(); transmitted_ += bytes; transmittedTotal_ += bytes.size();
            transmitted_ = transmitted_.right(config_.value("history_bytes").toInt()); state_["tx_requested_bytes"] = transmittedTotal_;
        }
        return NodeResult::output(output);
    }
    if (kind_.startsWith("control.")) return {};
    throw Error("未注册的原生节点：" + kind_);
}

QByteArray DataNode::sendBytes(const QVariantMap &args) const
{
    if (args.value("hex").metaType().id() == QMetaType::QString) return checkedHex(args.value("hex").toString());
    return encodeText(args.value("text").toString(),args.value("encoding",config_.value("encoding","utf-8")).toString(),
        args.value("hex",false).toBool(),args.value("escape",false).toBool(),args.value("newline",config_.value("newline","None")).toString());
}

NodeResult DataNode::action(const QString &name, const QVariantMap &args, const BatchContext &context, QVariant &result)
{
    if (kind_.startsWith("io.")) {
        auto request = args;
        if (name == "connect") {
            request["config"] = config_; request["kind"] = kind_;
            if (!config_.value("key_file").toString().isEmpty()) request["private_key"] = host_.resource(config_.value("key_file").toString());
        }
        if (name == "send") request["data"] = sendBytes(args);
        state_ = host_.io(id_,name,request); result = state_; return {};
    }
    if (kind_.startsWith("control.") && (name == "set_value" || name == "click")) {
        if (kind_ == "control.button") {
            state_["clicks"] = state_.value("clicks").toULongLong() + 1;
            Values output;
            for (const auto &port : ports_) if (port.direction == "out") output.insert(port.id,true);
            result = state_; return NodeResult::output(output);
        }
        const auto value = controlValue(args.value("value")); state_["value"] = value; result = state_;
        for (const auto &port : ports_) if (port.direction == "out") return NodeResult::output({{port.id,value}});
    }
    if (kind_ == "debug.console") {
        if (name == "send") {
            const auto bytes = sendBytes(args); transmitted_ += bytes; transmittedTotal_ += bytes.size();
            transmitted_ = transmitted_.right(config_.value("history_bytes").toInt()); state_["tx_requested_bytes"] = transmittedTotal_;
            result = QVariantMap{{"requested_bytes",bytes.size()}}; return NodeResult::output({{"tx",bytes}});
        }
        if (name == "read") {
            const auto base = receivedTotal_ - received_.size();
            const auto cursor = std::max(base,args.value("cursor",base).toULongLong());
            const auto bytes = received_.mid(static_cast<qsizetype>(cursor - base),args.value("limit",65536).toInt());
            result = QVariantMap{{"data",bytes}, {"text",decodeText(bytes,config_.value("encoding").toString())}, {"cursor",cursor + bytes.size()}, {"first_cursor",base}, {"gap",args.value("cursor",base).toULongLong() < base}};
            return {};
        }
        if (name == "clear") { received_.clear(); transmitted_.clear(); receivePackets_.clear(); receivedTotal_ = transmittedTotal_ = 0; state_["rx_bytes"] = 0; state_["tx_requested_bytes"] = 0; result = state_; return {}; }
        if (name == "save_receive") {
            QSaveFile file(args.value("path").toString());
            if (!file.open(QIODevice::WriteOnly)) throw Error("接收数据保存失败：" + file.errorString());
            const auto bytes = args.value("binary",true).toBool() ? received_ : decodeText(received_,config_.value("encoding").toString()).toUtf8();
            if (file.write(bytes) != bytes.size() || !file.commit()) throw Error("接收数据保存失败");
            result = QVariantMap{{"path",file.fileName()}, {"bytes",bytes.size()}}; return {};
        }
        if (name == "periodic_start") {
            periodicBytes_ = sendBytes(args); periodicInterval_ = args.value("interval_ms",1000).toDouble() / 1000.;
            if (periodicInterval_ < .001) throw Error("定时发送周期至少为 1 ms");
            state_["periodic"] = true; result = state_; return {};
        }
        if (name == "periodic_stop") { periodicInterval_ = -1.; state_["periodic"] = false; result = state_; return {}; }
        if (name == "cancel_file") { state_["file_sending"] = false; result = state_; return {}; }
        if (name == "send_file") {
            QFile file(args.value("path").toString());
            if (!file.open(QIODevice::ReadOnly)) throw Error("发送文件无法打开：" + file.errorString());
            if (!host_.publish) throw Error("运行会话不可用");
            quint64 sent = 0; state_["file_sending"] = true;
            try {
                while (!file.atEnd()) {
                    if (host_.cancelled && host_.cancelled(id_)) break;
                    const auto bytes = file.read(16384);
                    if (bytes.isEmpty() && file.error() != QFileDevice::NoError) throw Error("读取发送文件失败");
                    // Each publication completes its real downstream processing,
                    // including device writes, before the next chunk is read.
                    host_.publish(id_,{{"tx",bytes}},context);
                    sent += bytes.size();
                    state_["file_sent_bytes"] = sent;
                }
            } catch (...) { state_["file_sending"] = false; throw; }
            state_["file_sending"] = false;
            result = QVariantMap{{"sent_bytes",sent}, {"cancelled",!file.atEnd()}}; return {};
        }
    }
    return Node::action(name,args,context,result);
}

QVariantMap DataNode::snapshot(bool detailed) const
{
    auto result = Node::snapshot(detailed);
    if (kind_ == "debug.console") {
        result["receive_tail"] = decodeText(received_.right(16384),config_.value("encoding").toString());
        result["receive_hex"] = QString::fromLatin1(received_.right(8192).toHex(' '));
        result["send_tail"] = decodeText(transmitted_.right(4096),config_.value("encoding").toString());
    }
    return result;
}

QVariantMap DataNode::view(const QString &name, const QVariantMap &args) const
{
    if (kind_ != "debug.console") return Node::view(name,args);
    const auto limit = std::clamp(args.value("limit",65536).toInt(),1,1024 * 1024);
    const bool hex = args.value("hex",false).toBool(), timestamps = args.value("timestamps",false).toBool();
    const auto render = [&](const QByteArray &bytes) { return hex ? QString::fromLatin1(bytes.toHex(' ')) : decodeText(bytes,config_.value("encoding").toString()); };
    QString text;
    if (timestamps) {
        const auto first = receivedTotal_ - std::min<quint64>(received_.size(),limit);
        for (const auto &packet : receivePackets_) if (packet.first + packet.bytes.size() > first)
            text += "[" + packet.timestamp + "] " + render(packet.bytes.mid(static_cast<qsizetype>(first > packet.first ? first - packet.first : 0))) + '\n';
    } else text = render(received_.right(limit));
    return {{"text",text},{"cursor",receivedTotal_},{"first_cursor",receivedTotal_ - received_.size()},{"send_text",render(transmitted_.right(limit))},{"state",state_}};
}

} // namespace corig
