// Object placement, independent copies and explicit synchronization share the
// saved design model. No gesture can silently create a running/placed object.
#include "model_commands.h"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QSet>
#include <algorithm>

namespace corig {
namespace {
qsizetype indexOf(const QVariantList &items, const QString &id)
{
    for (qsizetype i = 0; i < items.size(); ++i) if (items[i].toMap().value("id") == id) return i;
    throw Error("对象不存在：" + id);
}

QVariantMap record(const QVariantList &items, const QString &id)
{
    for (const auto &item : items) if (item.toMap().value("id") == id) return item.toMap();
    return {};
}

QStringList declaredViews(const QVariantMap &definition)
{
    const auto value = definition.value("views");
    return value.metaType().id() == QMetaType::QVariantMap ? value.toMap().keys() : value.toStringList();
}

// Every UI object belongs either to the canvas/container tree or to its pending
// branch. Removing the pending branch from rendering never deletes its records.
void ensurePending(QVariantList &items, const QString &root)
{
    if (!record(items,"pending").isEmpty()) return;
    items.append(QVariantMap{{"id","pending"},{"plugin","layout.column"},{"pending",true},{"children",QVariantList{}},{"properties",QVariantMap{{"title","待放置"}}}});
    const auto at = indexOf(items,root); auto parent = items[at].toMap(); auto children = parent.value("children").toList();
    children.append("pending"); parent["children"] = children; items[at] = parent;
}

void moveUi(QVariantList &items, const QString &id, const QString &parent, int before = -1)
{
    const auto source = record(items,id), target = record(items,parent);
    if (source.isEmpty() || target.isEmpty() || !target.value("plugin").toString().startsWith("layout.")) throw Error("放置目标必须是现有容器");
    QSet<QString> descendants;
    std::function<void(QString)> collect = [&](const QString &key) { if (descendants.contains(key)) return; descendants.insert(key); for (const auto &child : record(items,key).value("children").toList()) collect(child.toString()); };
    collect(id); if (descendants.contains(parent)) throw Error("容器不能放进自身或自己的子容器");
    for (auto &entry : items) { auto p = entry.toMap(); auto children = p.value("children").toList(); if (children.removeAll(id)) { p["children"] = children; entry = p; } }
    const auto at = indexOf(items,parent); auto p = items[at].toMap(); auto children = p.value("children").toList();
    children.insert(before < 0 ? children.size() : std::clamp<qsizetype>(before,0,children.size()),id); p["children"] = children; items[at] = p;
}

QSet<QString> uiSubtree(const QVariantList &items, const QStringList &ids)
{
    QSet<QString> result;
    std::function<void(QString)> visit = [&](const QString &id) { if (result.contains(id)) return; const auto item = record(items,id); if (item.isEmpty()) throw Error("界面对象不存在：" + id); result.insert(id); for (const auto &child : item.value("children").toList()) visit(child.toString()); };
    for (const auto &id : ids) visit(id); return result;
}

void removeUi(QVariantList &items, const QSet<QString> &ids)
{
    items.erase(std::remove_if(items.begin(),items.end(),[&](const QVariant &v) { return ids.contains(v.toMap().value("id").toString()); }),items.end());
    for (auto &entry : items) { auto item = entry.toMap(); auto children = item.value("children").toList(); children.erase(std::remove_if(children.begin(),children.end(),[&](const QVariant &v) { return ids.contains(v.toString()); }),children.end()); if (item.contains("children")) item["children"] = children; entry = item; }
}

QVariantMap nodeSnapshot(QVariantMap node)
{
    node.remove("position"); node.remove("orientation"); node.remove("placed"); return node;
}

QVariantMap boundNode(const QVariantMap &item, const QVariantList &nodes)
{
    auto node = record(nodes,item.value("node").toString());
    const auto snapshot = item.value("binding").toMap();
    return node.isEmpty() || (!snapshot.isEmpty() && snapshot.value("plugin") != node.value("plugin")) ? snapshot : nodeSnapshot(node);
}

QString revision(const QVariantMap &project)
{
    return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument::fromVariant(project).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex());
}

void removeNodes(QVariantList &nodes, QVariantList &wires, const QSet<QString> &ids)
{
    nodes.erase(std::remove_if(nodes.begin(),nodes.end(),[&](const QVariant &v) { return ids.contains(v.toMap().value("id").toString()); }),nodes.end());
    wires.erase(std::remove_if(wires.begin(),wires.end(),[&](const QVariant &v) { const auto w = v.toMap(); return ids.contains(w.value("source").toList().value(0).toString()) || ids.contains(w.value("target").toList().value(0).toString()); }),wires.end());
}

// A replacement retains only endpoints that exist with the same contract. The
// final project validator also rejects cycles and multiple occupied endpoints.
QStringList incompatibleWires(const QVariantList &wires, const QVariantList &nodes, const Catalog &catalog)
{
    QStringList invalid;
    for (const auto &entry : wires) {
        const auto w = entry.toMap(), a = record(nodes,w.value("source").toList().value(0).toString()), b = record(nodes,w.value("target").toList().value(0).toString());
        Port from, to;
        for (const auto &p : catalog.ports(a.value("plugin").toString(),a.value("config").toMap())) if (p.id == w.value("source").toList().value(1)) from = p;
        for (const auto &p : catalog.ports(b.value("plugin").toString(),b.value("config").toMap())) if (p.id == w.value("target").toList().value(1)) to = p;
        if (from.direction != "out" || to.direction != "in" || (from.type != to.type && from.type != "any" && to.type != "any")) invalid.append(w.value("id").toString());
    }
    return invalid;
}
}

QVariantMap synchronizationPlan(const QVariantMap &project, const Catalog &catalog, const QString &direction)
{
    if (direction != "graph_to_ui" && direction != "ui_to_graph") throw Error("同步方向无效");
    const auto nodes = project.value("nodes").toList(), items = project.value("ui").toMap().value("items").toList();
    QVariantList changes; QSet<QString> desired;
    const auto add = [&](const QString &key, const QString &action, const QString &label, const QString &node, const QString &view, const QString &target) {
        changes.append(QVariantMap{{"key",key},{"action",action},{"label",label},{"node",node},{"view",view},{"target",target}});
    };
    if (direction == "graph_to_ui") {
        for (const auto &entry : nodes) {
            const auto node = entry.toMap(); const auto id = node.value("id").toString();
            for (const auto &view : declaredViews(catalog.definition(node.value("plugin").toString()))) {
                const auto key = id + "/" + view; desired.insert(key); QVariantMap target;
                for (const auto &v : items) if (v.toMap().value("node") == id && v.toMap().value("view") == view) { target = v.toMap(); break; }
                if (target.isEmpty()) add("add:" + key,"add","新增到界面待放置区：" + node.value("title").toString() + " / " + view,id,view,{});
                else if (target.value("binding").toMap() != nodeSnapshot(node)) add("update:" + target.value("id").toString(),"update","更新绑定，保留摆放与外观：" + node.value("title").toString(),id,view,target.value("id").toString());
            }
        }
        for (const auto &entry : items) { const auto item = entry.toMap(); if (item.contains("node") && !desired.contains(item.value("node").toString() + "/" + item.value("view").toString())) add("remove:" + item.value("id").toString(),"remove","删除目标页多余视图：" + item.value("properties").toMap().value("title",item.value("id")).toString(),{}, {},item.value("id").toString()); }
    } else {
        for (const auto &entry : items) {
            const auto item = entry.toMap(); if (!item.contains("node")) continue;
            const auto id = item.value("node").toString(); if (desired.contains(id)) continue;
            desired.insert(id); const auto source = boundNode(item,nodes); if (source.isEmpty()) throw Error("视图缺少可同步的节点声明：" + item.value("id").toString());
            if (record(nodes,id).isEmpty()) add("add:" + id,"add","新增到功能设计待放置区：" + source.value("title").toString(),id,{},item.value("id").toString());
            else if (record(nodes,id).value("plugin") != source.value("plugin")) {
                auto trial = nodes; auto replacement = source; const auto old = record(nodes,id); replacement["position"] = old.value("position"); replacement["orientation"] = old.value("orientation",0); replacement["placed"] = old.value("placed",true); trial[indexOf(trial,id)] = replacement;
                const auto invalid = incompatibleWires(project.value("wires").toList(),trial,catalog);
                add("update:" + id,"update",QString("替换功能节点：%1（解除 %2 条不兼容连线）").arg(source.value("title").toString()).arg(invalid.size()),id,{},item.value("id").toString());
            }
        }
        for (const auto &entry : nodes) { const auto node = entry.toMap(); const auto id = node.value("id").toString(); if (!declaredViews(catalog.definition(node.value("plugin").toString())).isEmpty() && !desired.contains(id)) {
            int count = 0; for (const auto &w : project.value("wires").toList()) if (w.toMap().value("source").toList().value(0) == id || w.toMap().value("target").toList().value(0) == id) ++count;
            add("remove:" + id,"remove",QString("删除目标页多余节点：%1（解除 %2 条连线）").arg(node.value("title").toString()).arg(count),id,{},id);
        } }
    }
    return {{"direction",direction},{"revision",revision(project)},{"changes",changes}};
}

QVariantMap copyEditorObjects(const QVariantMap &project, const QString &page, const QStringList &ids)
{
    const auto nodes = project.value("nodes").toList(), allItems = project.value("ui").toMap().value("items").toList();
    QVariantList copiedNodes, copiedItems, wires; QSet<QString> nodeIds;
    if (page == "graph") {
        for (const auto &id : ids) { const auto n = record(nodes,id); if (n.isEmpty()) throw Error("节点不存在：" + id); copiedNodes.append(n); nodeIds.insert(id); }
        for (const auto &entry : allItems) if (nodeIds.contains(entry.toMap().value("node").toString())) copiedItems.append(entry);
        for (const auto &entry : project.value("wires").toList()) if (nodeIds.contains(entry.toMap().value("source").toList().value(0).toString()) && nodeIds.contains(entry.toMap().value("target").toList().value(0).toString())) wires.append(entry);
    } else {
        const auto subtree = uiSubtree(allItems,ids);
        for (const auto &entry : allItems) if (subtree.contains(entry.toMap().value("id").toString())) {
            auto item = entry.toMap(); if (item.value("id") == "root" || item.value("id") == "pending") throw Error("根画布和待放置区不能复制");
            copiedItems.append(item); if (item.contains("node")) { const auto node = boundNode(item,nodes); const auto id = node.value("id").toString(); if (!nodeIds.contains(id)) { copiedNodes.append(node); nodeIds.insert(id); } }
        }
    }
    return {{"format","corig-objects"},{"page",page},{"roots",ids},{"nodes",copiedNodes},{"items",copiedItems},{"wires",wires}};
}

QVariantMap editorImpact(const QVariantMap &project, const Catalog &catalog, const QVariantMap &args)
{
    const auto ids = args.value("ids").toStringList(); const auto page = args.value("page").toString(); const auto op = args.value("operation").toString();
    const auto nodes = project.value("nodes").toList(), items = project.value("ui").toMap().value("items").toList(); QVariantList effects;
    if (op == "replace" && page == "graph") {
        auto trial = nodes; const auto plugin = args.value("plugin").toString(); catalog.definition(plugin);
        for (const auto &id : ids) { const auto at = indexOf(trial,id); auto node = trial[at].toMap(); node["plugin"] = plugin; node["config"] = catalog.checkedConfig(plugin,{}); node["title"] = catalog.definition(plugin).value("name"); trial[at] = node; }
        for (const auto &wire : incompatibleWires(project.value("wires").toList(),trial,catalog)) effects.append("解除不兼容连线：" + wire);
    } else if (page == "graph") {
        for (const auto &id : ids) { const auto node = record(nodes,id); if (node.isEmpty()) throw Error("节点不存在"); effects.append((op == "withdraw" ? "撤下节点：" : "删除节点：") + node.value("title").toString()); }
        for (const auto &entry : project.value("wires").toList()) if (ids.contains(entry.toMap().value("source").toList().value(0).toString()) || ids.contains(entry.toMap().value("target").toList().value(0).toString())) effects.append("解除连线：" + entry.toMap().value("id").toString());
        if (op == "remove") for (const auto &entry : items) if (ids.contains(entry.toMap().value("node").toString())) effects.append("对应界面对象保留，等待双向同步：" + entry.toMap().value("id").toString());
    } else {
        const auto subtree = uiSubtree(items,ids);
        for (const auto &id : subtree) effects.append((op == "withdraw" ? "撤下界面对象：" : op == "replace" ? "替换界面对象：" : "删除界面对象：") + id);
        if (op == "remove") effects.append("对应功能节点保留；同步时可勾选删除，相关连线会列入清单。");
    }
    if (effects.isEmpty()) effects.append("保留位置、尺寸、图层和兼容连接。");
    return {{"effects",effects},{"revision",revision(project)}};
}

EditResult editEditorProject(QVariantMap project, const Catalog &catalog, const QString &op, const QVariantMap &args)
{
    auto nodes = project.value("nodes").toList(), wires = project.value("wires").toList(); auto ui = project.value("ui").toMap(); auto items = ui.value("items").toList(); const auto root = ui.value("root").toString();
    ensurePending(items,root);
    const auto page = args.value("page","graph").toString(); if (page != "graph" && page != "layout") throw Error("编辑页面无效");
    auto ids = args.value("ids").toStringList(); if (ids.isEmpty() && args.contains("id")) ids.append(args.value("id").toString());
    EditResult edit; bool engine = false;
    if ((op == "editor.remove" || op == "editor.replace") && !args.value("confirmed").toBool()) throw Error("请先查看影响清单并确认");
    if (args.contains("revision") && args.value("revision").toString() != revision(project)) throw Error("设计已变化，请重新检查清单");
    if (op == "editor.add") {
        const auto plugin = args.value("plugin").toString(); const auto id = args.value("id",newId(page == "graph" ? "node" : "ui")).toString();
        if (page == "graph") {
            if (plugin.startsWith("layout.")) throw Error("容器只用于界面设计");
            nodes.append(QVariantMap{{"id",id},{"plugin",plugin},{"title",catalog.definition(plugin).value("name")},{"config",catalog.checkedConfig(plugin,{})},{"position",QVariantList{80.,80.}},{"orientation",0},{"placed",false}}); engine = true;
        } else {
            QVariantMap item{{"id",id},{"properties",QVariantMap{{"x",32.},{"y",32.},{"width",320.},{"height",180.}}}};
            if (plugin.startsWith("layout.")) { if (!QStringList{"layout.row","layout.column","layout.grid","layout.tabs"}.contains(plugin)) throw Error("未知容器"); item["plugin"] = plugin; item["children"] = QVariantList{}; item["properties"] = QVariantMap{{"title",""},{"x",32.},{"y",32.},{"width",480.},{"height",300.}}; }
            else { const auto spec = catalog.definition(plugin); const auto views = declaredViews(spec); if (views.isEmpty()) throw Error("该插件没有界面视图，请在功能设计添加"); const auto node = newId("node"); item["node"] = node; item["view"] = views.first(); item["binding"] = QVariantMap{{"id",node},{"plugin",plugin},{"title",spec.value("name")},{"config",catalog.checkedConfig(plugin,{})}}; item["properties"] = QVariantMap{{"title",spec.value("name")},{"x",32.},{"y",32.},{"width",320.},{"height",220.}}; }
            items.append(item); moveUi(items,id,"pending");
        }
        edit.selected = id; edit.result = QVariantMap{{"id",id},{"pending",true}};
    } else if (op == "editor.place" || op == "editor.withdraw") {
        const bool placing = op == "editor.place";
        for (const auto &id : ids) {
            if (page == "graph") { const auto at = indexOf(nodes,id); auto node = nodes[at].toMap(); node["placed"] = placing; if (placing && args.contains("position")) node["position"] = args.value("position"); nodes[at] = node; engine = true;
                if (!placing) wires.erase(std::remove_if(wires.begin(),wires.end(),[&](const QVariant &v) { return v.toMap().value("source").toList().value(0) == id || v.toMap().value("target").toList().value(0) == id; }),wires.end());
            } else { if (id == root || id == "pending") throw Error("不能撤下根画布或待放置区"); moveUi(items,id,placing ? args.value("parent",root).toString() : "pending",args.value("index",-1).toInt()); if (placing && args.contains("position")) { const auto at = indexOf(items,id); auto item = items[at].toMap(); auto props = item.value("properties").toMap(); const auto pos = args.value("position").toList(); props["x"] = pos.value(0); props["y"] = pos.value(1); item["properties"] = props; items[at] = item; } }
        }
        edit.selected = ids.value(0); edit.result = QVariantMap{{"ids",ids},{"placed",placing}};
    } else if (op == "editor.remove") {
        if (page == "graph") {
            // Preserve snapshots so deleting a graph object does not leave the
            // other page unable to show, remove or synchronize its own object.
            for (auto &entry : items) { auto item = entry.toMap(); if (ids.contains(item.value("node").toString())) { item["binding"] = nodeSnapshot(record(nodes,item.value("node").toString())); entry = item; } }
            removeNodes(nodes,wires,QSet<QString>(ids.begin(),ids.end())); engine = true;
        } else { const auto subtree = uiSubtree(items,ids); if (subtree.contains(root) || subtree.contains("pending")) throw Error("根画布和待放置区不能删除"); removeUi(items,subtree); }
        edit.result = QVariantMap{{"removed",ids}};
    } else if (op == "editor.reorder") {
        const auto id = ids.value(0); const int before = args.value("index").toInt();
        if (page == "graph") { const auto at = indexOf(nodes,id); const auto item = nodes.takeAt(at); nodes.insert(std::clamp<qsizetype>(before,0,nodes.size()),item); }
        else { QString parent; for (const auto &entry : items) if (entry.toMap().value("children").toList().contains(id)) parent = entry.toMap().value("id").toString(); moveUi(items,id,parent,before); }
        edit.selected = id;
    } else if (op == "editor.replace") {
        const auto plugin = args.value("plugin").toString();
        for (const auto &id : ids) {
            if (page == "graph") { const auto at = indexOf(nodes,id); auto node = nodes[at].toMap(); node["plugin"] = plugin; node["config"] = catalog.checkedConfig(plugin,{}); node["title"] = catalog.definition(plugin).value("name"); nodes[at] = node; engine = true; }
            else { const auto at = indexOf(items,id); auto item = items[at].toMap(); if (item.contains("children")) { if (!plugin.startsWith("layout.")) throw Error("含子对象的容器只能替换为其他容器"); item["plugin"] = plugin; }
                else { const auto spec = catalog.definition(plugin); const auto views = declaredViews(spec); if (views.isEmpty()) throw Error("替换插件需要界面视图"); auto node = item.value("binding").toMap(); node["id"] = item.value("node"); node["plugin"] = plugin; node["title"] = spec.value("name"); node["config"] = catalog.checkedConfig(plugin,{}); item["binding"] = node; item["view"] = views.first(); }
                items[at] = item;
            }
        }
        const auto invalid = incompatibleWires(wires,nodes,catalog); wires.erase(std::remove_if(wires.begin(),wires.end(),[&](const QVariant &v) { return invalid.contains(v.toMap().value("id").toString()); }),wires.end()); edit.selected = ids.value(0);
    } else if (op == "editor.paste") {
        const auto payload = args.value("payload").toMap(); if (payload.value("format") != "corig-objects") throw Error("剪贴板没有可粘贴对象");
        if (payload.value("page") != page) throw Error("请在原页面粘贴；跨页传递请使用同步清单");
        QHash<QString,QString> mapping; QVariantList newNodeIds, newItemIds;
        for (const auto &entry : payload.value("nodes").toList()) { auto node = entry.toMap(); const auto old = node.value("id").toString(), id = newId("node"); mapping[old] = id; node["id"] = id; node["placed"] = page == "graph"; auto pos = node.value("position",QVariantList{80.,80.}).toList(); node["position"] = QVariantList{pos.value(0).toDouble()+32.,pos.value(1).toDouble()+32.}; nodes.append(node); newNodeIds.append(id); }
        for (const auto &entry : payload.value("items").toList()) mapping[entry.toMap().value("id").toString()] = newId("ui");
        for (const auto &entry : payload.value("items").toList()) { auto item = entry.toMap(); const auto old = item.value("id").toString(); item["id"] = mapping.value(old); if (item.contains("node")) { item["node"] = mapping.value(item.value("node").toString()); auto binding = item.value("binding").toMap(); binding["id"] = item.value("node"); item["binding"] = binding; } if (item.contains("children")) { QVariantList children; for (const auto &child : item.value("children").toList()) if (mapping.contains(child.toString())) children.append(mapping.value(child.toString())); item["children"] = children; } auto props = item.value("properties").toMap(); props["x"] = props.value("x",32.).toDouble()+24.; props["y"] = props.value("y",32.).toDouble()+24.; item["properties"] = props; items.append(item); newItemIds.append(mapping.value(old)); }
        QSet<QString> childIds; for (const auto &entry : payload.value("items").toList()) for (const auto &child : entry.toMap().value("children").toList()) childIds.insert(child.toString());
        for (const auto &entry : payload.value("items").toList()) { const auto old = entry.toMap().value("id").toString(); if (!childIds.contains(old)) moveUi(items,mapping.value(old),page == "layout" ? root : "pending"); }
        if (page == "graph") for (const auto &entry : payload.value("wires").toList()) { auto w = entry.toMap(); auto a = w.value("source").toList(), b = w.value("target").toList(); a[0] = mapping.value(a[0].toString()); b[0] = mapping.value(b[0].toString()); w["source"] = a; w["target"] = b; w["id"] = newId("wire"); w["points"] = QVariantList{}; wires.append(w); }
        edit.selected = (page == "graph" ? newNodeIds : newItemIds).value(0).toString(); edit.result = QVariantMap{{"nodes",newNodeIds},{"items",newItemIds}}; engine = !newNodeIds.isEmpty();
    } else if (op == "sync.apply") {
        const auto plan = synchronizationPlan(project,catalog,args.value("direction").toString()); const auto keys = args.value("keys").toStringList();
        for (const auto &entry : plan.value("changes").toList()) { const auto change = entry.toMap(); if (!keys.contains(change.value("key").toString())) continue; const auto id = change.value("node").toString(), target = change.value("target").toString(), action = change.value("action").toString();
            if (args.value("direction") == "graph_to_ui") {
                if (action == "remove") removeUi(items,QSet<QString>{target});
                else if (action == "add") { const auto node = record(nodes,id); const auto itemId = newId("view"); items.append(QVariantMap{{"id",itemId},{"node",id},{"view",change.value("view")},{"binding",nodeSnapshot(node)},{"properties",QVariantMap{{"title",node.value("title")},{"x",32.},{"y",32.},{"width",320.},{"height",240.}}}}); moveUi(items,itemId,"pending"); }
                else { const auto at = indexOf(items,target); auto item = items[at].toMap(); item["binding"] = nodeSnapshot(record(nodes,id)); items[at] = item; }
            } else { engine = true; if (action == "remove") removeNodes(nodes,wires,QSet<QString>{id}); else { auto node = boundNode(record(items,target),nodes); node["id"] = id;
                if (action == "update") { const auto old = record(nodes,id); node["placed"] = old.value("placed",true); node["position"] = old.value("position"); node["orientation"] = old.value("orientation",0); nodes[indexOf(nodes,id)] = node; const auto invalid = incompatibleWires(wires,nodes,catalog); wires.erase(std::remove_if(wires.begin(),wires.end(),[&](const QVariant &w) { return invalid.contains(w.toMap().value("id").toString()); }),wires.end()); }
                else { node["placed"] = false; node["position"] = QVariantList{80.,80.}; node["orientation"] = 0; nodes.append(node); }
            } }
        }
        edit.result = QVariantMap{{"applied",keys}};
    } else throw Error("未知对象编辑操作：" + op);
    ui["items"] = items; project["ui"] = ui; project["nodes"] = nodes; project["wires"] = wires;
    alignUiToGrid(project); validateProject(project,catalog); edit.project = project; edit.engineChanged = engine; return edit;
}
} // namespace corig
