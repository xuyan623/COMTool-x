#include "recording.h"
#include <QCborValue>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <limits>

namespace cx {
namespace {
const QByteArray Magic("COMTool-x\0REC1\n",15);
constexpr quint32 MaxValueBytes = 16u * 1024u * 1024u;

// Length-prefixed CBOR keeps binary values native and bounds parser allocations.
QVariant decode(const QByteArray &bytes)
{
    QCborParserError error;
    const auto value = QCborValue::fromCbor(bytes,&error);
    if (error.error != QCborError::NoError || error.offset != bytes.size()) throw Error("录制数据损坏或包含多余内容");
    return value.toVariant();
}

QByteArray frameHeader(double time, quint32 bytes)
{
    if (!std::isfinite(time) || time < 0. || time > double(std::numeric_limits<qint64>::max()) / 1000000.) throw Error("录制时间无效");
    QByteArray header(12,Qt::Uninitialized);
    qToBigEndian<qint64>(std::llround(time * 1000000.),header.data());
    qToBigEndian<quint32>(bytes,header.data() + 8);
    return header;
}
} // namespace

void RecordingWriter::begin(const QString &type)
{
    auto candidate = std::make_unique<QTemporaryFile>(QDir::tempPath() + "/COMTool-x-recording-XXXXXX");
    if (!candidate->open()) throw Error("无法创建录制缓存：" + candidate->errorString());
    const auto metadata = QCborValue::fromVariant(QVariantMap{{"format","comtool-x-recording"},{"version",1},{"data_type",type}}).toCbor();
    QByteArray length(4,Qt::Uninitialized); qToBigEndian<quint32>(static_cast<quint32>(metadata.size()),length.data());
    const auto header = Magic + length + metadata;
    if (candidate->write(header) != header.size()) throw Error("录制文件头写入失败");
    file_ = std::move(candidate); type_ = type; count_ = 0; lastTime_ = 0.; validBytes_ = file_->size();
}

void RecordingWriter::append(double time, const QVariant &value)
{
    if (!file_) throw Error("尚未开始录制");
    if (time < lastTime_ || !valueMatches(value,type_)) throw Error("录制信号类型或时间顺序不正确");
    const auto bytes = QCborValue::fromVariant(value).toCbor();
    if (bytes.isEmpty() || bytes.size() > MaxValueBytes) throw Error("单条录制数据超过 16 MiB 或无法编码");
    const auto header = frameHeader(time,static_cast<quint32>(bytes.size()));
    if (file_->pos() != validBytes_ && !file_->seek(validBytes_)) throw Error("录制缓存定位失败");
    if (file_->write(header) != header.size() || file_->write(bytes) != bytes.size()) throw Error("录制写入失败：" + file_->errorString());
    lastTime_ = time; validBytes_ = file_->pos(); ++count_;
}

void RecordingWriter::save(const QString &path, double duration, const std::function<bool()> &cancelled)
{
    if (!file_ || !count_) throw Error("没有可保存的录制数据");
    if (duration < lastTime_) throw Error("录制时长小于最后一条数据时间");
    if (!file_->flush()) throw Error("录制缓存刷新失败");
    QFile source(file_->fileName());
    if (!source.open(QIODevice::ReadOnly)) throw Error("录制缓存读取失败");
    if (path.isEmpty() || !QDir().mkpath(QFileInfo(path).absolutePath())) throw Error("录制保存目录无效");
    QSaveFile target(path);
    if (!target.open(QIODevice::WriteOnly)) throw Error("录制文件无法保存：" + target.errorString());
    qint64 remaining = validBytes_;
    while (remaining > 0) {
        if (cancelled && cancelled()) throw Error("引擎已中断录制保存");
        const auto chunk = source.read(std::min<qint64>(65536,remaining));
        if (chunk.isEmpty() || target.write(chunk) != chunk.size()) throw Error("录制文件复制失败");
        remaining -= chunk.size();
    }
    // A zero-length footer finalizes duration and detects incomplete captures.
    const auto footer = frameHeader(duration,0);
    if (target.write(footer) != footer.size() || (cancelled && cancelled()) || !target.commit()) throw Error("录制保存未完成，原文件保持完整");
}

void RecordingWriter::reset()
{
    file_.reset(); type_.clear(); count_ = 0; validBytes_ = 0; lastTime_ = 0.;
}

RecordingReader::RecordingReader(const QString &path, const std::function<bool()> &cancelled) : file_(path)
{
    if (!file_.open(QIODevice::ReadOnly)) throw Error("录制文件无法打开：" + file_.errorString());
    if (file_.read(Magic.size()) != Magic) throw Error("不是 COMTool-x 录制文件");
    const auto lengthBytes = file_.read(4);
    if (lengthBytes.size() != 4) throw Error("录制文件头不完整");
    const auto length = qFromBigEndian<quint32>(lengthBytes.constData());
    if (!length || length > 4096) throw Error("录制文件头大小无效");
    const auto metadataBytes = file_.read(length);
    if (metadataBytes.size() != length) throw Error("录制文件头已截断");
    const auto metadata = decode(metadataBytes).toMap(); type_ = metadata.value("data_type").toString();
    if (metadata.value("format") != "comtool-x-recording" || metadata.value("version") != 1
        || !QStringList{"number","boolean","bytes","string","object","samples","any"}.contains(type_)) throw Error("录制文件格式或信号类型不受支持");
    double previous = 0.; bool finalized = false;
    while (!file_.atEnd()) {
        if (cancelled && cancelled()) throw Error("引擎已中断录制文件导入");
        const auto header = file_.read(12);
        if (header.size() != 12) throw Error("录制数据头不完整");
        const auto micros = qFromBigEndian<qint64>(header.constData());
        const auto count = qFromBigEndian<quint32>(header.constData() + 8);
        const double time = micros / 1000000.;
        if (micros < 0 || time < previous) throw Error("录制时间顺序损坏");
        if (!count) {
            duration_ = time; finalized = true;
            if (!file_.atEnd()) throw Error("录制结束标记之后存在多余数据");
            break;
        }
        if (count > MaxValueBytes || count > file_.size() - file_.pos()) throw Error("录制数据大小无效或文件已截断");
        const auto offset = file_.pos(); const auto bytes = file_.read(count);
        if (bytes.size() != count || !valueMatches(decode(bytes),type_)) throw Error("录制内容与声明类型不一致");
        entries_.append({offset,count,time}); previous = time;
    }
    if (!finalized || entries_.isEmpty()) throw Error("录制文件未完成或没有数据");
}

qsizetype RecordingReader::indexAt(double position) const
{
    if (!std::isfinite(position)) throw Error("回放位置需要有限数值");
    const auto next = std::upper_bound(entries_.cbegin(),entries_.cend(),position,[](double time,const Entry &entry) { return time < entry.time; });
    return next == entries_.cbegin() ? 0 : std::distance(entries_.cbegin(),next) - 1;
}

QVariant RecordingReader::valueAt(qsizetype index)
{
    if (index < 0 || index >= entries_.size()) throw Error("回放记录索引超出范围");
    const auto &entry = entries_[index];
    if (!file_.seek(entry.offset)) throw Error("回放文件定位失败");
    const auto bytes = file_.read(entry.bytes);
    if (bytes.size() != entry.bytes) throw Error("回放文件在导入后被修改或截断");
    const auto value = decode(bytes);
    if (!valueMatches(value,type_)) throw Error("回放文件内容在导入后已损坏");
    return value;
}

double RecordingReader::timeAt(qsizetype index) const
{
    if (index < 0 || index >= entries_.size()) throw Error("回放记录索引超出范围");
    return entries_[index].time;
}

quint32 RecordingReader::bytesAt(qsizetype index) const
{
    if (index < 0 || index >= entries_.size()) throw Error("回放记录索引超出范围");
    return entries_[index].bytes;
}

} // namespace cx
