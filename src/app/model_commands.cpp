#include "model_commands.h"
#include "quick/graph_geometry.h"
#include <QFileInfo>
#include <QUrl>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <algorithm>

namespace corig {

// Design assets are embedded; captured data stays external and only its local
// path is normalized. Both node creation and configuration use this policy.
static void prepareFileProperties(QVariantMap &project, QVariantMap &config, const QVariantMap &schema)
{
    for (auto i = schema.cbegin(); i != schema.cend(); ++i) {
        const auto format = i.value().toMap().value("format").toString();
        const auto value = config.value(i.key()).toString();
        if (format == "file") config[i.key()] = importResource(project,value);
        else if (format == "external_file" && !value.isEmpty()) {
            const QUrl url(value);
            config[i.key()] = QFileInfo(url.isLocalFile() ? url.toLocalFile() : value).absoluteFilePath();
        }
    }
}

static qsizetype findItem(const QVariantList &items, const QString &id)
{
    for (qsizetype i = 0; i < items.size(); ++i) if (items[i].toMap().value("id") == id) return i;
    throw Error("对象不存在：" + id);
}

static QStringList views(const QVariantMap &definition)
{
    const auto declared = definition.value("views");
    return declared.metaType().id() == QMetaType::QVariantMap ? declared.toMap().keys() : declared.toStringList();
}

QVariantMap uiPropertySchema(const QVariantMap &item, const Catalog &catalog, const QVariantMap &project)
{
    QVariantMap result{
        {"grid_size",QVariantMap{{"type","integer"},{"default",16},{"min",1},{"max",128}}},
        {"x",QVariantMap{{"type","number"},{"default",24.}}}, {"y",QVariantMap{{"type","number"},{"default",24.}}},
        {"width",QVariantMap{{"type","number"},{"default",320.},{"min",40.},{"max",16384.}}}, {"height",QVariantMap{{"type","number"},{"default",200.},{"min",32.},{"max",16384.}}},
        {"title",QVariantMap{{"type","string"},{"default",""}}}, {"stretch",QVariantMap{{"type","integer"},{"default",1},{"min",0},{"max",1000}}},
        {"min_width",QVariantMap{{"type","integer"},{"default",80},{"min",0}}}, {"min_height",QVariantMap{{"type","integer"},{"default",60},{"min",0}}},
        {"max_width",QVariantMap{{"type","integer"},{"default",16777215},{"min",1}}}, {"max_height",QVariantMap{{"type","integer"},{"default",16777215},{"min",1}}},
        {"row",QVariantMap{{"type","integer"},{"default",-1},{"min",-1}}}, {"column",QVariantMap{{"type","integer"},{"default",-1},{"min",-1}}},
        {"rowspan",QVariantMap{{"type","integer"},{"default",1},{"min",1}}}, {"colspan",QVariantMap{{"type","integer"},{"default",1},{"min",1}}},
        {"font_size",QVariantMap{{"type","number"},{"default",13},{"min",6},{"max",96}}}, {"font_family",QVariantMap{{"type","string"},{"default","Segoe UI"}}},
        {"color",QVariantMap{{"type","string"},{"default","#E6EDF5"}}}, {"background",QVariantMap{{"type","string"},{"default","#182330"}}},
        {"label",QVariantMap{{"type","string"},{"default",""}}}, {"gap",QVariantMap{{"type","integer"},{"default",8},{"min",0},{"max",100}}},
        {"padding",QVariantMap{{"type","integer"},{"default",8},{"min",0},{"max",100}}}
    };
    for (auto i = result.begin(); i != result.end(); ++i) { auto field = i.value().toMap(); field["section"] = "base"; i.value() = field; }
    if (item.value("plugin") == "layout.grid") result.insert("columns",QVariantMap{{"type","integer"},{"default",2},{"min",1},{"max",64}});
    if (item.value("id") != project.value("ui").toMap().value("root")) result.remove("grid_size");
    if (item.value("plugin") == "layout.tabs") result.insert("current_index",QVariantMap{{"type","integer"},{"default",0},{"min",0}});
    // Plugins own business appearance fields. Geometry and hierarchy retain
    // their editor meaning; authors can read/write those common fields too.
    QVariantMap extra;
    const auto container = item.value("plugin").toString();
    const bool builtinContainer = QStringList{"layout.canvas","layout.row","layout.column","layout.grid","layout.tabs"}.contains(container);
    if (!container.isEmpty() && !builtinContainer) extra = catalog.definition(container).value("properties").toMap();
    else if (container.isEmpty()) {
        auto binding = item.value("binding").toMap();
        for (const auto &entry : project.value("nodes").toList()) if (entry.toMap().value("id") == item.value("node")) {
            const auto live = entry.toMap(); if (binding.isEmpty() || binding.value("plugin") == live.value("plugin")) binding = live;
        }
        if (!binding.value("plugin").toString().isEmpty()) {
            const auto spec = catalog.definition(binding.value("plugin").toString());
            extra = spec.value("view_properties").toMap();
            const auto local = spec.value("views").toMap().value(item.value("view").toString()).toMap().value("properties").toMap();
            for (auto i = local.cbegin(); i != local.cend(); ++i) extra[i.key()] = i.value();
        }
    }
    static const QSet<QString> structural{"grid_size","x","y","width","height","stretch","min_width","min_height","max_width","max_height","row","column","rowspan","colspan","padding","gap","current_index"};
    for (auto i = extra.cbegin(); i != extra.cend(); ++i) {
        if (structural.contains(i.key()) && result.contains(i.key())) continue;
        result[i.key()] = i.value();
    }
    return result;
}

QVariantMap checkedUiProperties(const QVariantMap &item, const Catalog &catalog, const QVariantMap &project,
                               const QVariantMap &changes, const QVariantMap &previous)
{
    const auto schema = uiPropertySchema(item,catalog,project); const auto plugin = item.value("plugin").toString();
    if (!plugin.isEmpty()) {
        if (QStringList{"layout.canvas","layout.row","layout.column","layout.grid","layout.tabs"}.contains(plugin)) return checkedProperties(schema,changes,previous);
        return catalog.checkedPresentation(plugin,"container",schema,changes,previous);
    }
    auto binding = item.value("binding").toMap();
    for (const auto &entry : project.value("nodes").toList()) if (entry.toMap().value("id") == item.value("node")) {
        const auto live = entry.toMap(); if (binding.isEmpty() || binding.value("plugin") == live.value("plugin")) binding = live;
    }
    const auto id = binding.value("plugin").toString();
    return id.isEmpty() ? checkedProperties(schema,changes,previous) : catalog.checkedPresentation(id,"view",schema,changes,previous);
}

QVariantMap nodeConfigurationPlan(const QVariantMap &project, const Catalog &catalog, const QString &id, const QVariantMap &changes)
{
    auto nodes = project.value("nodes").toList(); const auto at = findItem(nodes,id); auto node = nodes[at].toMap();
    const auto plugin = node.value("plugin").toString();
    const auto config = catalog.checkedConfig(plugin,changes,node.value("config").toMap());
    node["config"] = config; nodes[at] = node;
    QVariantList affected, nextPorts;
    for (const auto &port : catalog.ports(plugin,config)) nextPorts.append(port.describe());
    for (const auto &entry : project.value("wires").toList()) {
        const auto wire = entry.toMap(); const auto a = wire.value("source").toList(), b = wire.value("target").toList();
        if (a.value(0) != id && b.value(0) != id) continue;
        Port from, to;
        const auto source = nodes[findItem(nodes,a.value(0).toString())].toMap(), target = nodes[findItem(nodes,b.value(0).toString())].toMap();
        for (const auto &port : catalog.ports(source.value("plugin").toString(),source.value("config").toMap())) if (port.id == a.value(1)) from = port;
        for (const auto &port : catalog.ports(target.value("plugin").toString(),target.value("config").toMap())) if (port.id == b.value(1)) to = port;
        if (from.direction != "out" || to.direction != "in" || (from.type != to.type && from.type != "any" && to.type != "any")) affected.append(wire);
    }
    const QVariantMap basis{{"nodes",project.value("nodes")},{"wires",project.value("wires")},{"config",config},{"ports",nextPorts},{"package",catalog.definition(plugin).value("package_hash")}};
    const auto revision = QString::fromLatin1(QCryptographicHash::hash(QJsonDocument::fromVariant(basis).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex());
    return {{"node",id},{"config",config},{"ports",nextPorts},{"affected_wires",affected},{"requires_confirmation",!affected.isEmpty()},{"revision",revision}};
}

EditResult editProject(QVariantMap project, const Catalog &catalog, const QString &operation, const QVariantMap &args)
{
    if (operation.startsWith("editor.") || operation == "sync.apply") return editEditorProject(project,catalog,operation,args);
    if (operation == "graph.node.remove" || operation == "ui.item.remove") {
        auto removal = args; removal["page"] = operation == "graph.node.remove" ? "graph" : "layout"; removal["confirmed"] = true;
        return editEditorProject(project,catalog,"editor.remove",removal);
    }
    EditResult edit; edit.project = project;
    auto nodes = project.value("nodes").toList(), wires = project.value("wires").toList();
    if (operation == "batch") {
        QVariantList results;
        for (const auto &entry : args.value("commands").toList()) {
            const auto command = entry.toMap();
            if (command.value("op") == "batch") throw Error("批量编辑不允许嵌套");
            const auto changed = editProject(edit.project,catalog,command.value("op").toString(),command.value("args").toMap());
            edit.project = changed.project; edit.engineChanged |= changed.engineChanged; edit.selected = changed.selected; results.append(changed.result);
        }
        edit.result = results; return edit;
    }
    if (operation == "workspace.configure" || operation == "workspace.remove") {
        const auto plugin = args.value("page").toString();
        if (catalog.kind(plugin) != "workspace") throw Error("插件不是工作区："+plugin);
        auto entries = project.value("workspaces").toMap();
        if (operation == "workspace.remove") entries.remove(plugin);
        else entries[plugin] = catalog.checkedConfig(plugin,args.value("changes").toMap(),entries.value(plugin).toMap());
        project["workspaces"] = entries; edit.result = QVariantMap{{"page",plugin},{"properties",entries.value(plugin)}};
    } else if (operation == "graph.node.add") {
        const auto plugin = args.value("plugin").toString();
        if (catalog.kind(plugin) != "node") throw Error("该插件不是计算节点："+plugin);
        auto config = catalog.checkedConfig(plugin,args.value("config").toMap());
        const auto schema = catalog.definition(plugin).value("properties").toMap();
        prepareFileProperties(project,config,schema);
        const auto id = args.value("id",newId("node")).toString();
        QVariantMap node{{"id",id},{"plugin",plugin},{"title",args.value("title",catalog.definition(plugin).value("name"))},
            {"config",config},{"position",args.value("position",QVariantList{80.,80.})},{"orientation",args.value("orientation",0)},{"placed",args.value("placed",true)}};
        nodes.append(node); edit.result = node; edit.selected = id; edit.engineChanged = true;
    } else if (operation == "graph.node.remove") {
        QStringList ids = args.value("ids").toStringList(); if (ids.isEmpty()) ids.append(args.value("id").toString());
        for (const auto &id : ids) findItem(nodes,id);
        nodes.erase(std::remove_if(nodes.begin(),nodes.end(),[&](const QVariant &v) { return ids.contains(v.toMap().value("id").toString()); }),nodes.end());
        wires.erase(std::remove_if(wires.begin(),wires.end(),[&](const QVariant &v) { const auto w = v.toMap(); return ids.contains(w.value("source").toList().value(0).toString()) || ids.contains(w.value("target").toList().value(0).toString()); }),wires.end());
        edit.result = QVariantMap{{"removed",ids}}; edit.engineChanged = true;
    } else if (operation == "graph.node.move" || operation == "graph.node.rotate" || operation == "graph.node.patch" || operation == "node.configure") {
        const auto id = args.value("id",args.value("node")).toString(); const auto index = findItem(nodes,id); auto node = nodes[index].toMap();
        if (operation == "graph.node.move") node["position"] = args.value("position");
        else if (operation == "graph.node.rotate") {
            const auto old = nodeGeometry(node,catalog); const auto position = node.value("position").toList();
            const auto angle = (node.value("orientation",0).toInt() + args.value("degrees",90).toInt() + 360) % 360; node["orientation"] = angle;
            const auto next = nodeGeometry(node,catalog);
            node["position"] = QVariantList{position[0].toDouble() + (old.value("width").toDouble() - next.value("width").toDouble()) / 2.,position[1].toDouble() + (old.value("height").toDouble() - next.value("height").toDouble()) / 2.};
        } else if (operation == "node.configure") {
            const auto plan = nodeConfigurationPlan(project,catalog,id,args.value("changes").toMap());
            const auto affected = plan.value("affected_wires").toList();
            if (!affected.isEmpty()) {
                if (!args.value("confirmed").toBool() || args.value("revision") != plan.value("revision")) throw Error("请先确认 node.configure.plan 列出的受影响导线");
                QSet<QString> removed; for (const auto &wire : affected) removed.insert(wire.toMap().value("id").toString());
                wires.erase(std::remove_if(wires.begin(),wires.end(),[&](const QVariant &wire) { return removed.contains(wire.toMap().value("id").toString()); }),wires.end());
            }
            auto config = catalog.checkedConfig(node.value("plugin").toString(),args.value("changes").toMap(),node.value("config").toMap());
            const auto schema = catalog.definition(node.value("plugin").toString()).value("properties").toMap();
            prepareFileProperties(project,config,schema);
            node["config"] = config; edit.engineChanged = true;
        } else {
            const auto changes = args.value("changes").toMap();
            for (auto i = changes.cbegin(); i != changes.cend(); ++i) {
                if (i.key() != "title" && i.key() != "position" && i.key() != "orientation" && i.key() != "size") throw Error("不支持的节点显示属性：" + i.key());
                node[i.key()] = i.value();
            }
        }
        nodes[index] = node; edit.result = node; edit.selected = id;
    } else if (operation == "graph.wire.add") {
        QVariantMap wire{{"id",args.value("id",newId("wire"))},{"source",args.value("source")},{"target",args.value("target")},
            {"routing",args.value("routing","curve")},{"color",args.value("color","#5E9DCB")},{"width",args.value("width",1.6)},{"points",args.value("points",QVariantList{})}};
        wires.append(wire); edit.result = wire; edit.selected = wire.value("id").toString(); edit.engineChanged = true;
    } else if (operation == "graph.wire.remove") {
        const auto index = findItem(wires,args.value("id").toString()); wires.removeAt(index); edit.result = QVariantMap{{"removed",args.value("id")}}; edit.engineChanged = true;
    } else if (operation == "graph.wire.patch") {
        const auto index = findItem(wires,args.value("id").toString()); auto wire = wires[index].toMap(); const auto changes = args.value("changes").toMap();
        for (auto i = changes.cbegin(); i != changes.cend(); ++i) {
            if (!QStringList{"points","routing","color","width"}.contains(i.key())) throw Error("不支持的导线显示属性：" + i.key());
            wire[i.key()] = i.value();
        }
        if (wire.value("routing") != "curve" && wire.value("routing") != "polyline") throw Error("路径需要 curve 或 polyline");
        const auto width = finiteNumber(wire.value("width"),"线宽"); if (width < .5 || width > 10.) throw Error("线宽需要在 0.5～10 之间");
        wires[index] = wire; edit.result = wire; edit.selected = wire.value("id").toString();
    } else if (operation.startsWith("ui.")) {
        auto ui = project.value("ui").toMap(); auto items = ui.value("items").toList(); const auto root = ui.value("root").toString();
        const auto appendTo = [&](const QString &parent, const QString &child) {
            const auto index = findItem(items,parent); auto p = items[index].toMap();
            if (p.value("plugin").toString().isEmpty() || catalog.kind(p.value("plugin").toString()) != "container") throw Error("目标不是界面容器");
            auto children = p.value("children").toList(); children.append(child); p["children"] = children; items[index] = p;
        };
        if (operation == "ui.update") {
            QVariantList added;
            for (const auto &entry : nodes) {
                const auto node = entry.toMap();
                for (const auto &view : views(catalog.definition(node.value("plugin").toString()))) {
                    bool exists = false;
                    for (const auto &item : items) if (item.toMap().value("node") == node.value("id") && item.toMap().value("view") == view) exists = true;
                    if (exists) continue;
                    bool pendingExists = false;
                    for (const auto &item : items) if (item.toMap().value("id") == "pending") pendingExists = true;
                    if (!pendingExists) { items.append(QVariantMap{{"id","pending"},{"plugin","layout.column"},{"pending",true},{"children",QVariantList{}},{"properties",QVariantMap{{"title","待放置视图"}}}}); appendTo(root,"pending"); }
                    const auto id = newId("view");
                    items.append(QVariantMap{{"id",id},{"node",node.value("id")},{"view",view},{"binding",node},{"properties",QVariantMap{{"title",node.value("title")}}}}); appendTo("pending",id); added.append(id);
                }
            }
            edit.result = QVariantMap{{"added",added}};
        } else if (operation == "ui.container.add" || operation == "ui.view.add") {
            const auto id = args.value("id",newId("ui")).toString(); QVariantMap item{{"id",id},{"properties",args.value("properties",QVariantMap{})}};
            if (operation == "ui.container.add") {
                const auto type = args.value("plugin","layout.column").toString();
                if (catalog.kind(type) != "container") throw Error("插件不是界面容器："+type);
                item["plugin"] = type; item["children"] = QVariantList{};
            } else {
                const auto index = findItem(nodes,args.value("node").toString()); const auto node = nodes[index].toMap();
                if (!views(catalog.definition(node.value("plugin").toString())).contains(args.value("view").toString())) throw Error("节点没有声明该视图");
                item["node"] = args.value("node"); item["view"] = args.value("view");
                item["binding"] = node;
            }
            item["properties"] = checkedUiProperties(item,catalog,project,item.value("properties").toMap());
            items.append(item); appendTo(args.value("parent",root).toString(),id); edit.selected = id; edit.result = item;
        } else if (operation == "ui.view.bind") {
            // Rebinding changes the presentation source while retaining item
            // identity, placement, appearance and its parent container.
            const auto index = findItem(items,args.value("id").toString()); auto item = items[index].toMap();
            if (item.contains("plugin")) throw Error("容器不能绑定功能视图");
            const auto node = nodes[findItem(nodes,args.value("node").toString())].toMap();
            if (!views(catalog.definition(node.value("plugin").toString())).contains(args.value("view").toString())) throw Error("节点没有声明该视图");
            item["node"] = node.value("id"); item["view"] = args.value("view"); item["binding"] = node;
            items[index] = item; edit.result = item;
        } else if (operation == "ui.item.move") {
            const auto id = args.value("id").toString(), parent = args.value("parent").toString();
            if (id == root || id == "pending") throw Error("根容器和待放置区不能移动");
            findItem(items,id); findItem(items,parent);
            for (auto &entry : items) {
                auto p = entry.toMap(); auto children = p.value("children").toList();
                if (children.removeAll(id)) { p["children"] = children; entry = p; }
            }
            const auto index = findItem(items,parent); auto target = items[index].toMap();
            if (target.value("plugin").toString().isEmpty() || catalog.kind(target.value("plugin").toString()) != "container") throw Error("目标不是容器");
            auto children = target.value("children").toList(); const auto at = args.value("index",children.size()).toLongLong();
            children.insert(std::clamp<qsizetype>(at,0,children.size()),id); target["children"] = children; items[index] = target;
            edit.result = QVariantMap{{"moved",id},{"parent",parent}};
        } else if (operation == "ui.item.remove") {
            const auto id = args.value("id").toString(); if (id == root || id == "pending") throw Error("根容器和待放置区不能移除");
            QSet<QString> removed;
            std::function<void(QString)> collect = [&](const QString &key) { const auto index = findItem(items,key); removed.insert(key); for (const auto &child : items[index].toMap().value("children").toList()) collect(child.toString()); };
            collect(id);
            items.erase(std::remove_if(items.begin(),items.end(),[&](const QVariant &v) { return removed.contains(v.toMap().value("id").toString()); }),items.end());
            for (auto &entry : items) { auto item = entry.toMap(); if (item.contains("children")) { auto children = item.value("children").toList(); children.removeAll(id); item["children"] = children; entry = item; } }
            edit.result = QVariantMap{{"removed",id}};
        } else if (operation == "ui.item.patch") {
            const auto index = findItem(items,args.value("id").toString()); auto item = items[index].toMap();
            item["properties"] = checkedUiProperties(item,catalog,project,args.value("properties").toMap(),item.value("properties").toMap()); items[index] = item; edit.result = item;
        } else throw Error("未知界面设计操作：" + operation);
        ui["items"] = items; project["ui"] = ui;
    } else throw Error("未知设计操作：" + operation);
    project["nodes"] = nodes; project["wires"] = wires;
    alignUiToGrid(project); validateProject(project,catalog); edit.project = project;
    return edit;
}

} // namespace corig
