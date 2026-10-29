#include "graph_geometry.h"
#include <QFont>
#include <QFontMetricsF>
#include <QSet>
#include <cmath>

namespace corig {

QVariantMap nodeGeometry(const QVariantMap &node, const Catalog &catalog)
{
    const auto kind = node.value("plugin").toString(); const auto definition = catalog.definition(kind);
    const auto ports = catalog.ports(kind,catalog.checkedConfig(kind,node.value("config").toMap()));
    QVector<Port> inputs, outputs;
    for (const auto &port : ports) (port.direction == "in" ? inputs : outputs).append(port);
    QFont font("Microsoft YaHei UI",10); QFontMetricsF measure(font);
    const auto title = node.value("title",definition.value("name")).toString();
    const int angle = node.value("orientation",0).toInt(); const bool vertical = angle == 90 || angle == 270;
    const auto count = std::max(inputs.size(),outputs.size());
    double inputWidth = 0., outputWidth = 0.;
    for (const auto &port : inputs) inputWidth = std::max(inputWidth,measure.horizontalAdvance(port.label));
    for (const auto &port : outputs) outputWidth = std::max(outputWidth,measure.horizontalAdvance(port.label));
    const double inputZone = inputs.isEmpty() ? 0. : std::clamp(inputWidth + 38.,94.,250.);
    const double outputZone = outputs.isEmpty() ? 0. : std::clamp(outputWidth + 38.,94.,250.);
    double middle = std::clamp(measure.horizontalAdvance(title) + 28.,112.,190.);
    const double verticalColumn = std::clamp(std::max(inputWidth,outputWidth)+28.,100.,250.);
    double width = vertical ? std::max({middle,verticalColumn * count,180.}) : inputZone + middle + outputZone;
    double height = vertical ? 104. + (inputs.isEmpty() ? 0. : 80.) + (outputs.isEmpty() ? 0. : 80.) : std::max(104.,44. + count * 42.);
    // Explicit resizing may enlarge a card, while its port labels and spacing
    // remain the minimum usable size. Resizing never changes a port contract.
    const auto size = node.value("size").toList();
    width = std::max(width,size.value(0).toDouble()); height = std::max(height,size.value(1).toDouble());
    if (!vertical) middle = width-inputZone-outputZone;
    const bool inputFirst = angle == 0 || angle == 90;
    const double firstZone = inputFirst ? inputZone : outputZone;
    const bool firstPresent = inputFirst ? !inputs.isEmpty() : !outputs.isEmpty();
    QVariantList inputArea, outputArea, center;
    if (!vertical) {
        inputArea = {angle == 0 ? 0. : width-inputZone,0.,inputZone,height};
        outputArea = {angle == 0 ? width-outputZone : 0.,0.,outputZone,height}; center = {firstZone,0.,middle,height};
    } else {
        inputArea = {0.,angle == 90 ? 0. : height-80.,width,inputs.isEmpty()?0.:80.};
        outputArea = {0.,angle == 270 ? 0. : height-80.,width,outputs.isEmpty()?0.:80.}; center = {0.,firstPresent?80.:0.,width,height-(inputs.isEmpty()?0.:80.)-(outputs.isEmpty()?0.:80.)};
    }
    QVariantList points;
    const auto add = [&](const QVector<Port> &side, bool input) {
        for (qsizetype i = 0; i < side.size(); ++i) {
            const auto &port = side[i];
            double x, y, normalX = 0., normalY = 0., labelX, labelY, labelWidth;
            QString alignment;
            if (vertical) {
                x = width * (i + .5) / std::max<qsizetype>(1,side.size());
                const bool top = input ? angle == 90 : angle == 270;
                y = top ? 2. : height-2.; normalY = top ? -1. : 1.;
                // Top and bottom ports share one text rhythm: section heading
                // at 14 px, name at 32 px, type below it, clear of either edge.
                labelWidth = width / std::max<qsizetype>(1,side.size()) - 12.; labelX = x - labelWidth / 2.; labelY = top ? 32. : height - 48.; alignment = "center";
            } else {
                const bool left = input ? angle == 0 : angle == 180;
                x = left ? 2. : width-2.; y = (height + 20.) / 2. + (i - (side.size()-1)/2.) * 42.; normalX = left ? -1. : 1.;
                labelWidth = (input ? inputZone : outputZone) - 30.; labelX = left ? 18. : width - labelWidth - 18.; labelY = y - 17.; alignment = left ? "left" : "right";
            }
            auto point = port.describe(); point.insert("x",x); point.insert("y",y); point.insert("nx",normalX); point.insert("ny",normalY);
            point.insert("label_x",labelX); point.insert("label_y",labelY); point.insert("label_width",labelWidth); point.insert("alignment",alignment); points.append(point);
        }
    };
    add(inputs,true); add(outputs,false);
    static const QHash<QString,QString> symbols{{"sin","sin x"},{"cos","cos x"},{"tan","tan x"},{"asin","sin⁻¹"},{"acos","cos⁻¹"},{"atan","tan⁻¹"},{"abs","|x|"},{"sqrt","√x"},{"exp","eˣ"},{"log","ln x"},{"log10","log₁₀"},{"floor","⌊x⌋"},{"ceil","⌈x⌉"},{"negate","−x"},{"sign","sgn x"},{"degrees","rad / °"},{"radians","° / rad"},{"add","Σ"},{"subtract","a − b"},{"multiply","a × b"},{"divide","a / b"},{"minimum","min"},{"maximum","max"},{"power","aᵇ"},{"atan2","atan2"},{"greater","a > b"},{"less","a < b"},{"equal","a = b"},{"integral","∫ x dt"},{"derivative","dx / dt"},{"expression","ƒ(x)"},{"python","Py"},{"limit","clamp"},{"linear_map","map"}};
    QString symbol = kind.startsWith("function.") ? symbols.value(kind.mid(9)) : QString{};
    if (kind.startsWith("logic.")) {
        static const QHash<QString,QString> logicSymbols{{"and","∧"},{"or","∨"},{"not","¬"},{"nand","¬∧"},{"nor","¬∨"},{"xor","⊕"},{"xnor","⊙"},{"edge","↑↓"}};
        symbol = logicSymbols.value(kind.mid(6));
    }
    if (kind == "source.constant") { const auto values = node.value("config").toMap().value("values").toMap(); symbol = values.size()==1 ? QString::number(values.first().toDouble(),'g',5) : "{…}"; }
    QString icon = kind.startsWith("io.") ? "connection" : kind == "control.button" ? "button" : kind == "control.text" ? "text-input" : kind.startsWith("control.") ? "sliders" : kind == "plot.curves" ? "wave" : kind == "plot.attitude" ? "attitude" : kind.startsWith("source.") ? "clock" : kind.startsWith("protocol.") ? "decode" : kind == "data.fork" ? "fork" : kind == "data.gate" || kind == "data.select" ? "switch" : kind == "terminal.ansi" || kind == "debug.console" ? "terminal" : "wave";
    if (kind == "display.value") icon = "display";
    return {{"width",width},{"height",height},{"ports",points},{"title",title},{"type",kind},{"title_y",0.},{"vertical",vertical},{"center",center},{"input_area",inputArea},{"output_area",outputArea},{"symbol",symbol},{"icon",icon},{"radius",8.}};
}

QPointF portPosition(const QVariantMap &node, const QVariantMap &geometry, const QString &port)
{
    const auto position = node.value("position").toList();
    for (const auto &item : geometry.value("ports").toList()) {
        const auto p = item.toMap();
        if (p.value("id") == port) return {position.value(0).toDouble() + p.value("x").toDouble(),position.value(1).toDouble() + p.value("y").toDouble()};
    }
    throw Error("绘制引用的端口不存在：" + port);
}

QPainterPath wirePath(const QVariantMap &wire, const QHash<QString,QVariantMap> &nodes, const QHash<QString,QVariantMap> &geometry, QVector<QPainterPath> *segments)
{
    const auto source = wire.value("source").toList(), target = wire.value("target").toList();
    const auto a = portPosition(nodes.value(source.value(0).toString()),geometry.value(source.value(0).toString()),source.value(1).toString());
    const auto b = portPosition(nodes.value(target.value(0).toString()),geometry.value(target.value(0).toString()),target.value(1).toString());
    QVector<QPointF> points{a};
    for (const auto &item : wire.value("points").toList()) { const auto p = item.toList(); points.append({p.value(0).toDouble(),p.value(1).toDouble()}); }
    points.append(b);
    const auto normal = [&](const QVariantMap &g, const QString &port) {
        for (const auto &item : g.value("ports").toList()) { const auto p = item.toMap(); if (p.value("id") == port) return QPointF(p.value("nx").toDouble(),p.value("ny").toDouble()); }
        return QPointF(1,0);
    };
    const auto sourceNormal = normal(geometry.value(source[0].toString()),source[1].toString());
    const auto targetNormal = normal(geometry.value(target[0].toString()),target[1].toString());
    QPainterPath path(a);
    if (segments) segments->clear();
    for (qsizetype i = 0; i + 1 < points.size(); ++i) {
        const auto start = points[i], end = points[i + 1]; QPainterPath segment(start);
        if (wire.value("routing","curve") == "polyline") segment.lineTo(end);
        else {
            const double reach = std::clamp(QLineF(start,end).length() * .45,25.,160.);
            const auto first = i == 0 ? start + sourceNormal * reach : start + (end - points[i - 1]) / 6.;
            const auto second = i + 2 == points.size() ? end + targetNormal * reach : end - (points[i + 2] - start) / 6.;
            segment.cubicTo(first,second,end);
        }
        path.connectPath(segment); if (segments) segments->append(segment);
    }
    path.setCachingEnabled(true); return path;
}

QVariantList graphLayout(const QVariantMap &project, const Catalog &catalog)
{
    QVariantList result;
    QVector<QRectF> rectangles;
    QHash<QPair<int,int>,QVector<int>> grid;
    QSet<int> overlapping;
    for (const auto &item : project.value("nodes").toList()) {
        auto node = item.toMap(); if (!node.value("placed",true).toBool()) continue;
        const auto geometry = nodeGeometry(node,catalog); node["geometry"] = geometry;
        const auto xy = node.value("position").toList();
        const QRectF bounds(xy.value(0).toDouble() - 6.,xy.value(1).toDouble() - 6.,geometry.value("width").toDouble() + 12.,geometry.value("height").toDouble() + 12.);
        const int index = static_cast<int>(result.size()); QSet<int> checked;
        for (int x = static_cast<int>(std::floor(bounds.left() / 256)); x <= static_cast<int>(std::floor(bounds.right() / 256)); ++x)
            for (int y = static_cast<int>(std::floor(bounds.top() / 256)); y <= static_cast<int>(std::floor(bounds.bottom() / 256)); ++y) {
                auto &bucket = grid[{x,y}];
                for (const auto other : bucket) if (!checked.contains(other)) { checked.insert(other); if (rectangles[other].intersects(bounds)) { overlapping.insert(other); overlapping.insert(index); } }
                bucket.append(index);
            }
        rectangles.append(bounds); result.append(node);
    }
    for (const auto index : overlapping) { auto node = result[index].toMap(); node["overlap"] = true; result[index] = node; }
    return result;
}

} // namespace corig
