#include "instrument_node.h"
#include "terminal_screen.h"
#include "protocol_node.h"
#include "core/series.h"
#include "core/mesh.h"
#include <QFileInfo>
#include <QSaveFile>
#include <QTextStream>
#include <numbers>
#include <cmath>
#include <map>

namespace corig {

static QStringList plotColors{"#66B4F4","#F3AB65","#83CFAD","#D992BB","#B2A1EA","#E1CB78","#67C9C4","#E88F8F"};

class PlotNode final : public Node {
public:
    using Node::Node;
    void configure(Node &prepared) override
    {
        Node::configure(prepared);
        for (auto &[name,series] : series_) series.resize(config_.value("max_points").toLongLong());
        lastView_.clear();
    }
    QVariantMap snapshot(bool detailed) const override
    {
        auto result = Node::snapshot(detailed); QVariantMap channels; quint64 count = 0;
        for (const auto &[name,series] : series_) { channels.insert(name,QVariantMap{{"points",series.size()},{"total",QVariant::fromValue(series.total())},{"last_time",series.lastTime()}}); count += series.size(); }
        result["channels"] = channels; result["retained_points"] = count; result["version"] = version_; result["display_paused"] = paused_;
        return result;
    }
    QVariantMap view(const QString &, const QVariantMap &args) const override
    {
        const auto observedVersion = paused_ ? frozenVersion_ : version_;
        if (!lastView_.isEmpty() && lastArguments_ == args && lastVersion_ == observedVersion) return lastView_;
        const auto &observed = paused_ ? frozenSeries_ : series_;
        QVariantList channels;
        const bool fft = args.value("fft",false).toBool();
        double latest = 0.; for (const auto &[name,series] : observed) latest = std::max(latest,series.lastTime());
        const double unit = timeUnit(config_.value("x_unit","s").toString());
        const bool follow = args.value("follow",config_.value("follow")).toBool();
        const double end = follow ? latest : args.value("x_max",latest / unit).toDouble() * unit;
        const double begin = follow ? end - config_.value("window_seconds").toDouble() : args.value("x_min",(end - config_.value("window_seconds").toDouble()) / unit).toDouble() * unit;
        const auto visibility = config_.value("visibility").toMap(), colors = config_.value("colors").toMap();
        const auto wanted = config_.value("channels").toStringList();
        const auto width = std::clamp(args.value("width",1200).toInt(),100,10000);
        double yMin = std::numeric_limits<double>::infinity(), yMax = -yMin, xMin = fft ? 0. : begin / unit, xMax = fft ? 1. : end / unit;
        qsizetype index = 0;
        for (const auto &[name,series] : observed) {
            auto points = series.points(begin,end,fft ? 0 : width * 2);
            if (fft) points = spectrum(points);
            else for (auto &point : points) point.rx() /= unit;
            const bool visible = visibility.value(name,true).toBool() && (wanted.isEmpty() || wanted.contains(name));
            if (visible) for (const auto &point : points) { yMin = std::min(yMin,point.y()); yMax = std::max(yMax,point.y()); }
            if (fft && !points.isEmpty()) { xMin = 0.; xMax = std::max(xMax,points.back().x()); }
            channels.append(QVariantMap{{"name",name},{"color",colors.value(name,plotColors[index++ % plotColors.size()])},{"visible",visible},{"points",QVariant::fromValue(std::move(points))}});
        }
        if (!std::isfinite(yMin)) { yMin = -1.; yMax = 1.; }
        if (yMin == yMax) { yMin -= 1.; yMax += 1.; }
        const auto margin = (yMax - yMin) * .05;
        lastView_ = {{"kind","plot"},{"channels",channels},{"x_min",xMin},{"x_max",xMax > xMin ? xMax : xMin + 1.},
            {"y_min",args.value("y_min",yMin - margin)},{"y_max",args.value("y_max",yMax + margin)},
            {"fft",fft},{"version",observedVersion},{"paused",paused_},{"line_width",config_.value("line_width")},
            {"x_label",fft ? "频率 / Hz" : config_.value("x_label").toString() + " / " + config_.value("x_unit").toString()},
            {"y_label",config_.value("y_label")}};
        lastArguments_ = args; lastVersion_ = observedVersion; return lastView_;
    }
    NodeResult action(const QString &name, const QVariantMap &args, const BatchContext &context, QVariant &result) override
    {
        if (name == "clear") { series_.clear(); frozenSeries_.clear(); frozenVersion_ = ++version_; lastView_.clear(); result = snapshot(false); return {}; }
        if (name == "pause_display") {
            const bool pause = args.value("value").toBool();
            if (pause && !paused_) { frozenSeries_ = series_; frozenVersion_ = version_; }
            if (!pause) frozenSeries_.clear();
            paused_ = pause; lastView_.clear(); result = snapshot(false); return {};
        }
        if (name == "read_data") {
            QVariantMap channels;
            const auto selected = args.value("channel").toString();
            for (const auto &[channel,series] : series_) if (selected.isEmpty() || channel == selected) {
                const auto raw = series.points();
                channels.insert(channel,QVariantMap{{"points",describePoints(raw.mid(args.value("offset",0).toLongLong(),args.value("limit",10000).toLongLong()))},{"retained",series.size()},{"total",QVariant::fromValue(series.total())}});
            }
            result = QVariantMap{{"channels",channels}}; return {};
        }
        if (name == "export_csv") {
            QSaveFile file(args.value("path").toString());
            if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) throw Error("CSV 文件无法打开：" + file.errorString());
            QTextStream stream(&file); stream.setRealNumberPrecision(17);
            stream << "channel,time,value\n";
            quint64 count = 0;
            for (const auto &[channel,series] : series_) {
                auto label = channel; label.replace('"',"\"\"");
                for (const auto &point : series.points()) { stream << '"' << label << "\"," << point.x() << ',' << point.y() << '\n'; ++count; }
            }
            stream.flush(); if (!file.commit()) throw Error("CSV 保存失败：" + file.errorString());
            result = QVariantMap{{"path",file.fileName()},{"points",count}}; return {};
        }
        return Node::action(name,args,context,result);
    }
protected:
    NodeResult process(const Values &changes, const BatchContext &context, bool) override
    {
        const auto append = [&](const QString &name, double x, double y) {
            auto found = series_.find(name);
            if (found == series_.end()) found = series_.emplace(name,Series(config_.value("max_points").toLongLong())).first;
            found->second.append(x,y);
        };
        if (changes.contains("samples")) for (const auto &item : sampleFrames(changes.value("samples"))) {
            const auto frame = item.toMap(), values = frame.value("values").toMap();
            for (auto i = values.cbegin(); i != values.cend(); ++i) append(i.key(),frame.value("time").toDouble(),i.value().toDouble());
        }
        const auto labels = config_.value("input_labels").toMap();
        for (const auto &input : config_.value("inputs").toStringList()) if (changes.contains(input)) append(labels.value(input,input).toString(),context.time,number(input));
        ++version_; return {};
    }
private:
    std::map<QString,Series> series_;
    std::map<QString,Series> frozenSeries_;
    quint64 version_ = 0, frozenVersion_ = 0;
    bool paused_ = false;
    mutable QVariantMap lastView_, lastArguments_;
    mutable quint64 lastVersion_ = ~quint64(0);
};

class AttitudeNode final : public Node {
public:
    AttitudeNode(QVariantMap design, QVariantMap definition, QVector<Port> ports, NodeHost host)
        : Node(std::move(design),std::move(definition),std::move(ports),std::move(host))
    {
        static const bool registered = QMetaType::registerConverter<MeshPtr,QVariantMap>([](const MeshPtr &mesh) { return mesh->describe(); });
        Q_UNUSED(registered);
        mapping_ = axisMapping(config_); reference_ = checkedQuaternion(config_.value("reference_quaternion").toList()).toRotationMatrix();
        const auto file = config_.value("model_file").toString();
        mesh_ = file.isEmpty() ? airplane() : loadMesh(host_.resource(file),QFileInfo(file).fileName());
        evaluate();
    }
    void configure(Node &prepared) override
    {
        auto &candidate = dynamic_cast<AttitudeNode &>(prepared);
        mesh_ = candidate.mesh_; mapping_ = candidate.mapping_; reference_ = candidate.reference_;
        Node::configure(prepared); evaluate();
    }
    QVariantMap view(const QString &, const QVariantMap &) const override
    {
        return {{"kind","attitude"},{"mesh",QVariant::fromValue(mesh_)},{"matrix",describeMatrix(rotation_)},{"config",config_},{"state",state_}};
    }
    NodeResult action(const QString &name, const QVariantMap &args, const BatchContext &context, QVariant &result) override
    {
        if (name == "geometry") {
            const auto offset = args.value("offset",0).toLongLong(), limit = args.value("limit",10000).toLongLong();
            QVariantList values; const bool vertices = args.value("component","vertices") == "vertices";
            // Each geometry entry stays one triplet in the paged response.
            if (vertices) for (auto i = offset; i < std::min<qsizetype>(mesh_->vertices.size(),offset + limit); ++i) { const auto v = mesh_->vertices[i]; values.append(QVariant(QVariantList{v.x(),v.y(),v.z()})); }
            else for (auto i = offset; i < std::min<qsizetype>(mesh_->triangles.size(),offset + limit); ++i) { const auto f = mesh_->triangles[i]; values.append(QVariant(QVariantList{f[0],f[1],f[2]})); }
            result = QVariantMap{{"values",values},{"total",vertices ? mesh_->vertices.size() : mesh_->triangles.size()},{"model",mesh_->describe()}}; return {};
        }
        return Node::action(name,args,context,result);
    }
protected:
    NodeResult process(const Values &, const BatchContext &, bool) override { evaluate(); return {}; }
private:
    void evaluate()
    {
        try {
            QMatrix3x3 input;
            if (config_.value("mode") == "quaternion") input = checkedQuaternion({number("w"),number("x"),number("y"),number("z")}).toRotationMatrix();
            else {
                const auto scale = config_.value("angle_unit") == "deg" ? std::numbers::pi / 180 : 1.;
                input = eulerMatrix(number("roll") * scale,number("pitch") * scale,number("yaw") * scale,config_.value("euler_order").toString(),config_.value("euler_frame").toString());
            }
            if (config_.value("direction") == "world_to_body") input = input.transposed();
            rotation_ = reference_.transposed() * mapping_ * input * mapping_.transposed();
            auto q = QQuaternion::fromRotationMatrix(rotation_).normalized(); if (q.scalar() < 0.f) q = -q;
            state_["matrix"] = describeMatrix(rotation_); state_["quaternion"] = QVariantList{q.scalar(),q.x(),q.y(),q.z()};
            state_["valid"] = true; state_["error"] = QString{};
        } catch (const Error &error) { state_["valid"] = false; state_["error"] = error.message; }
        state_["model"] = mesh_->describe();
    }
    MeshPtr mesh_;
    QMatrix3x3 rotation_, mapping_, reference_;
};

class TerminalNode final : public Node {
public:
    TerminalNode(QVariantMap design, QVariantMap definition, QVector<Port> ports, NodeHost host)
        : Node(std::move(design),std::move(definition),std::move(ports),std::move(host)), screen_(config_.value("columns").toInt(),config_.value("rows").toInt(),config_.value("history_lines").toInt())
    {
        setDecoder();
    }
    NodeResult start(const BatchContext &) override { return NodeResult::output({{"resize",QVariantMap{{"columns",screen_.columns()},{"rows",screen_.rows()}}}}); }
    void configure(Node &prepared) override
    {
        const bool encoding = config_.value("encoding") != prepared.config().value("encoding");
        Node::configure(prepared); if (encoding) setDecoder(); screen_.resize(config_.value("columns").toInt(),config_.value("rows").toInt());
    }
    QVariantMap snapshot(bool detailed) const override
    {
        auto result = Node::snapshot(detailed); result["screen"] = screen_.describe(false); return result;
    }
    QVariantMap view(const QString &, const QVariantMap &) const override { return screen_.describe(true); }
    NodeResult action(const QString &name, const QVariantMap &args, const BatchContext &context, QVariant &result) override
    {
        if (name == "send") { const auto bytes = args.contains("hex") ? checkedHex(args.value("hex").toString()) : encodeText(args.value("text").toString(),config_.value("encoding").toString()); result = QVariantMap{{"requested_bytes",bytes.size()}}; return NodeResult::output({{"tx",bytes}}); }
        if (name == "resize") { screen_.resize(args.value("columns").toInt(),args.value("rows").toInt()); result = screen_.describe(false); return NodeResult::output({{"resize",QVariantMap{{"columns",screen_.columns()},{"rows",screen_.rows()}}}}); }
        if (name == "read_screen") { result = screen_.describe(args.value("styles",false).toBool()); return {}; }
        if (name == "read_history") { result = screen_.history(args.value("offset",0).toInt(),args.value("limit",200).toInt(),args.value("styles",false).toBool()); return {}; }
        if (name == "clear") { screen_.clear(); result = screen_.describe(false); return {}; }
        if (name == "previous_page" || name == "next_page") { screen_.scroll(name == "previous_page" ? -screen_.rows() : screen_.rows()); result = screen_.describe(true); return {}; }
        return Node::action(name,args,context,result);
    }
protected:
    NodeResult process(const Values &changes, const BatchContext &, bool) override
    {
        if (!changes.contains("rx")) return {};
        const auto bytes = changes.value("rx").toByteArray(); screen_.feed(decoder_->toUnicode(bytes));
        state_["received_bytes"] = state_.value("received_bytes").toULongLong() + bytes.size();
        return NodeResult::output({{"through",bytes}});
    }
private:
    void setDecoder()
    {
        auto codec = QTextCodec::codecForName(config_.value("encoding").toByteArray());
        if (!codec) throw Error("终端编码不受支持"); decoder_.reset(codec->makeDecoder());
    }
    TerminalScreen screen_;
    std::unique_ptr<QTextDecoder> decoder_;
};

std::unique_ptr<Node> makeInstrumentNode(QVariantMap design, QVariantMap definition, QVector<Port> ports, NodeHost host)
{
    const auto type = design.value("plugin").toString();
    if (type == "plot.curves") return std::make_unique<PlotNode>(std::move(design),std::move(definition),std::move(ports),std::move(host));
    if (type == "plot.attitude") return std::make_unique<AttitudeNode>(std::move(design),std::move(definition),std::move(ports),std::move(host));
    if (type == "terminal.ansi") return std::make_unique<TerminalNode>(std::move(design),std::move(definition),std::move(ports),std::move(host));
    throw Error("未知仪表类型：" + type);
}

} // namespace corig
