#include "recording_node.h"
#include <QDir>
#include <QFileInfo>
#include <QUrl>
#include <algorithm>

namespace corig {
namespace {
// File dialogs and structured actions accept local URLs or filesystem paths.
QString localPath(const QString &value)
{
    if (value.trimmed().isEmpty()) throw Error("请选择录制文件路径");
    const QUrl url(value);
    const auto path = url.isLocalFile() ? url.toLocalFile() : value;
    if (!QDir::isAbsolutePath(path) && !url.scheme().isEmpty()) throw Error("录制文件只支持本地路径");
    return QFileInfo(path).absoluteFilePath();
}
} // namespace

RecorderNode::RecorderNode(QVariantMap design, QVariantMap definition, QVector<Port> ports, NodeHost host)
    : Node(std::move(design),std::move(definition),std::move(ports),std::move(host)) {}

double RecorderNode::durationAt(double time) const
{
    return accumulated_ + (phase_ == Phase::Recording ? std::max(0.,time - startedAt_) : 0.);
}

void RecorderNode::configure(Node &prepared)
{
    if (config_.value("data_type") != prepared.config().value("data_type")) {
        if ((phase_ == Phase::Recording || phase_ == Phase::Paused) && writer_.count()) throw Error("请先保存或重置当前录制，再修改信号类型");
        writer_.reset(); phase_ = Phase::Empty; accumulated_ = 0.; error_.clear(); savedPath_.clear();
    }
    Node::configure(prepared);
}

void RecorderNode::stop()
{
    if (phase_ == Phase::Recording) {
        accumulated_ = durationAt(host_.time ? host_.time() : lastTime_);
        phase_ = Phase::Paused;
    }
}

QVariantMap RecorderNode::snapshot(bool detailed) const
{
    auto result = Node::snapshot(detailed);
    result["state"] = QVariantMap{{"recording",phase_ == Phase::Recording},{"saved",phase_ == Phase::Saved},
        {"records",QVariant::fromValue(writer_.count())},{"written_bytes",writer_.bytes()},
        {"duration",durationAt(host_.time ? host_.time() : lastTime_)},{"saved_path",savedPath_},{"last_error",error_}};
    return result;
}

NodeResult RecorderNode::action(const QString &name, const QVariantMap &args, const BatchContext &context, QVariant &result)
{
    lastTime_ = context.time;
    if (name == "start") {
        if (phase_ != Phase::Recording) {
            if (phase_ == Phase::Empty || phase_ == Phase::Saved) {
                writer_.begin(config_.value("data_type").toString()); accumulated_ = 0.; savedPath_.clear();
            }
            startedAt_ = context.time; phase_ = Phase::Recording; error_.clear();
        }
    } else if (name == "pause" || name == "save") {
        if (phase_ == Phase::Recording) { accumulated_ = durationAt(context.time); phase_ = Phase::Paused; }
        if (name == "save") {
            const auto path = localPath(args.value("path").toString());
            writer_.save(path,accumulated_,[this] { return host_.cancelled && host_.cancelled(id_); });
            savedPath_ = path; phase_ = Phase::Saved; error_.clear();
        }
    } else if (name == "reset") {
        writer_.reset(); phase_ = Phase::Empty; accumulated_ = startedAt_ = 0.; error_.clear(); savedPath_.clear();
    } else return Node::action(name,args,context,result);
    result = snapshot(false).value("state"); return {};
}

NodeResult RecorderNode::process(const Values &changes, const BatchContext &context, bool)
{
    lastTime_ = context.time; QVariant result;
    // A simultaneous pause wins, so a shared trigger never records by accident.
    if (changes.value("pause").toBool()) action("pause",{},context,result);
    else if (changes.value("start").toBool()) action("start",{},context,result);
    if (!connected_.contains("in")) { NodeResult disconnected; disconnected.disconnected.insert("out"); return disconnected; }
    if (!changes.contains("in")) return {};
    const auto value = changes.value("in");
    if (phase_ == Phase::Recording) try { writer_.append(durationAt(context.time),value); }
    catch (const Error &error) {
        accumulated_ = durationAt(context.time); phase_ = Phase::Paused; error_ = error.message;
        if (host_.log) host_.log("error","录制已暂停：" + error_);
    }
    return NodeResult::output({{"out",value}});
}

PlaybackNode::PlaybackNode(QVariantMap design, QVariantMap definition, QVector<Port> ports, NodeHost host)
    : Node(std::move(design),std::move(definition),std::move(ports),std::move(host))
{
    const auto path = config_.value("path").toString();
    if (!path.isEmpty()) try {
        auto reader = std::make_unique<RecordingReader>(localPath(path),[this] { return host_.cancelled && host_.cancelled(id_); });
        if (config_.value("data_type") != "any" && reader->dataType() != config_.value("data_type")) throw Error("录制文件类型与回放端口类型不一致");
        reader_ = std::move(reader);
    } catch (const Error &error) {
        // An unavailable external file cannot prevent engine reset or bypass.
        error_ = error.message;
    }
}

double PlaybackNode::positionAt(double time) const
{
    if (!reader_) return 0.;
    return std::clamp(position_ + (playing_ ? std::max(0.,time - anchor_) * speed_ : 0.),0.,reader_->duration());
}

double PlaybackNode::sourceInterval() const
{
    if (!playing_ || !cutoff_ || !reader_) return -1.;
    const auto position = positionAt(host_.time ? host_.time() : lastTime_);
    const auto due = next_ < reader_->count() ? reader_->timeAt(next_) : reader_->duration();
    return std::max(.000001,(due - position) / speed_);
}

void PlaybackNode::configure(Node &prepared)
{
    auto &candidate = dynamic_cast<PlaybackNode &>(prepared);
    if (config_ != candidate.config_) {
        if (!candidate.config_.value("path").toString().isEmpty() && !candidate.reader_) throw Error(candidate.error_);
        const bool keepCutoff = cutoff_ && candidate.reader_ && config_.value("data_type") == candidate.config_.value("data_type");
        reader_ = std::move(candidate.reader_); position_ = 0.; cursor_ = next_ = 0; playing_ = false; cutoff_ = keepCutoff; error_.clear();
    }
    Node::configure(prepared);
}

void PlaybackNode::stop()
{
    position_ = positionAt(host_.time ? host_.time() : lastTime_); playing_ = false;
}

QVariantMap PlaybackNode::snapshot(bool detailed) const
{
    auto result = Node::snapshot(detailed);
    result["state"] = QVariantMap{{"loaded",bool(reader_)},{"cutoff",cutoff_},{"playing",playing_},{"speed",speed_},
        {"position",positionAt(host_.time ? host_.time() : lastTime_)},{"duration",reader_ ? reader_->duration() : 0.},
        {"records",reader_ ? reader_->count() : 0},{"sample_index",reader_ ? cursor_ : -1},
        {"path",reader_ ? reader_->path() : QString{}},{"last_error",error_}};
    return result;
}

NodeResult PlaybackNode::action(const QString &name, const QVariantMap &args, const BatchContext &context, QVariant &result)
{
    lastTime_ = context.time;
    NodeResult emission;
    if (name == "load") {
        auto candidate = std::make_unique<RecordingReader>(localPath(args.value("path").toString()),[this] { return host_.cancelled && host_.cancelled(id_); });
        if (config_.value("data_type") != "any" && candidate->dataType() != config_.value("data_type")) throw Error("录制文件类型与回放端口类型不一致");
        // Validate first: a failed replacement leaves source selection intact.
        reader_ = std::move(candidate); position_ = 0.; cursor_ = next_ = 0; playing_ = false; error_.clear();
    } else if (name == "cutoff") {
        const bool enabled = args.value("value").toBool();
        if (enabled && !reader_) throw Error("请先导入有效录制文件");
        if (enabled != cutoff_) {
            position_ = positionAt(context.time); playing_ = false; cutoff_ = enabled;
            if (enabled) next_ = reader_->indexAt(position_);
        }
    } else if (name == "play") {
        if (!reader_ || !cutoff_) throw Error("请先导入文件并打开截断开关，再播放");
        if (!playing_) {
            if (position_ >= reader_->duration() && next_ >= reader_->count()) { position_ = 0.; cursor_ = next_ = 0; }
            anchor_ = context.time; playing_ = true;
        }
    } else if (name == "pause") {
        position_ = positionAt(context.time); playing_ = false;
    } else if (name == "seek" || name == "step") {
        if (!reader_) throw Error("请先导入有效录制文件");
        const auto target = name == "seek" ? std::clamp(finiteNumber(args.value("position"),"回放位置"),0.,reader_->duration())
            : reader_->timeAt(std::clamp<qsizetype>(cursor_ + args.value("direction").toInt(),0,reader_->count() - 1));
        const auto index = reader_->indexAt(target);
        const auto value = cutoff_ ? reader_->valueAt(index) : QVariant{};
        position_ = target; anchor_ = context.time; cursor_ = index; next_ = cutoff_ ? index + 1 : index;
        if (name == "step") playing_ = false;
        if (cutoff_) emission.emissions.append(Values{{"out",value}});
    } else if (name == "set_speed") {
        const auto speed = finiteNumber(args.value("value"),"回放倍速");
        if (speed < .125 || speed > 16.) throw Error("回放倍速需要在 0.125～16 之间");
        position_ = positionAt(context.time); anchor_ = context.time; speed_ = speed;
    } else return Node::action(name,args,context,result);
    emission.rescheduleSource = true; result = snapshot(false).value("state"); return emission;
}

NodeResult PlaybackNode::process(const Values &changes, const BatchContext &context, bool tick)
{
    lastTime_ = context.time;
    if (!cutoff_) {
        if (!connected_.contains("in")) { NodeResult result; result.disconnected.insert("out"); return result; }
        return changes.contains("in") ? NodeResult::output({{"out",changes.value("in")}}) : NodeResult{};
    }
    NodeResult result;
    if (!tick || !playing_ || !reader_) return result;
    const auto position = positionAt(context.time);
    qsizetype bytes = 0;
    try {
        // Bounded bursts catch up without dropping records or starving commands.
        while (next_ < reader_->count() && reader_->timeAt(next_) <= position + 1e-9 && result.emissions.size() < 64) {
            if (!result.emissions.isEmpty() && bytes + reader_->bytesAt(next_) > 4 * 1024 * 1024) break;
            if (host_.cancelled && host_.cancelled(id_)) break;
            bytes += reader_->bytesAt(next_);
            result.emissions.append(Values{{"out",reader_->valueAt(next_)}}); cursor_ = next_++;
        }
        if (next_ == reader_->count() && position + 1e-9 >= reader_->duration()) { position_ = reader_->duration(); playing_ = false; }
    } catch (const Error &error) {
        position_ = position; playing_ = false; error_ = error.message;
        if (host_.log) host_.log("error","回放已暂停：" + error_);
    }
    result.separateFrames = result.emissions.size() > 1;
    result.rescheduleSource = true; return result;
}

} // namespace corig
