#include "property_session.h"
#include "controller.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <cmath>

namespace corig {
PropertySession::PropertySession(Controller &owner, QString scope, QString target)
    : QObject(&owner), owner_(owner), scope_(std::move(scope)), target_(std::move(target))
{
    connect(&owner,&Controller::projectChanged,this,&PropertySession::changed);
    connect(&owner,&Controller::runtimeChanged,this,&PropertySession::changed);
    connect(&owner,&Controller::catalogChanged,this,&PropertySession::changed);
    connect(&owner,&Controller::preferencesChanged,this,&PropertySession::changed);
    connect(&owner,&Controller::busyChanged,this,&PropertySession::changed);
    connect(&owner,&Controller::jobFinished,this,[this](const QString &id) {
        if (id != job_) return;
        const auto result = owner_.job(id); job_.clear();
        if (result.value("state") == "complete") { changes_.clear(); texts_.clear(); base_.clear(); error_.clear(); }
        else error_ = result.value("error").toString();
        emit changed();
    });
    connect(&owner,&Controller::viewChanged,this,[this](const QString &key) {
        const auto current = info(); if (key == current.value("node").toString()+"/"+current.value("view").toString()) emit changed();
    });
}

QVariantMap PropertySession::info() const { return owner_.propertyInfo(scope_,target_); }
QVariantMap PropertySession::schema() const { return info().value("schema").toMap(); }
QVariantMap PropertySession::values() const { return info().value("values").toMap(); }
QVariantMap PropertySession::draft() const
{
    auto result = values(); for (auto i = changes_.cbegin(); i != changes_.cend(); ++i) result[i.key()] = i.value(); return result;
}
QVariantMap PropertySession::config() const
{
    const auto current = info(); const auto node = current.value("node").toString();
    if (node.isEmpty()) return values();
    auto design = owner_.nodeDesign(node); const auto binding = current.value("item").toMap().value("binding").toMap();
    if (design.isEmpty() || (!binding.isEmpty() && binding.value("plugin") != design.value("plugin"))) return binding.value("config").toMap();
    return owner_.nodeState(node).value("config",design.value("config")).toMap();
}
QVariantMap PropertySession::properties() const { return scope_ == "ui" || scope_ == "workspace" ? values() : QVariantMap{}; }
QVariantMap PropertySession::state() const
{
    const auto current = info(); const auto node = current.value("node").toString(), view = current.value("view").toString();
    return view.isEmpty() ? owner_.nodeState(node) : owner_.viewState(node,view);
}
bool PropertySession::editable() const
{
    return !pending() && !owner_.busy() && (scope_ == "node" ? !owner_.readOnly() || owner_.runtime().value("active").toBool() : !owner_.readOnly());
}

QVariant PropertySession::stage(const QVariantMap &changes)
{
    try {
    if (pending()) throw Error("请等待当前属性提交完成");
    const auto fields = schema(), current = values();
    for (auto i = changes.cbegin(); i != changes.cend(); ++i) if (!fields.contains(i.key())) throw Error("属性未声明："+i.key());
    for (auto i = changes.cbegin(); i != changes.cend(); ++i) {
        texts_.remove(i.key());
        if (!base_.contains(i.key())) base_[i.key()] = current.value(i.key());
        if (i.value() == current.value(i.key())) { changes_.remove(i.key()); base_.remove(i.key()); }
        else changes_[i.key()] = i.value();
    }
    error_.clear(); emit changed(); return QVariantMap{{"dirty",dirty()},{"draft",draft()}};
    } catch (const std::exception &exception) { error_ = QString::fromUtf8(exception.what()); emit changed(); return QVariantMap{{"error",error_}}; }
}

QVariant PropertySession::stageText(const QString &field, const QString &text)
{
    // Invalid/incomplete text is a real draft too. Parsing belongs to commit,
    // so leaving a JSON editor can apply or discard exactly what was typed.
    if (pending()) return QVariantMap{{"error","请等待当前属性提交完成"}};
    if (!schema().contains(field)) return QVariantMap{{"error","属性未声明："+field}};
    if (!base_.contains(field)) base_[field] = values().value(field);
    texts_[field] = text; error_.clear(); emit changed(); return QVariantMap{{"dirty",true}};
}

QVariant PropertySession::commit(const QVariantMap &changes, bool confirmed, const QString &revision)
{
    // A confirmation preview is not a decision on the staged draft. Returning
    // it unchanged lets the caller show the affected wires and retry with the
    // exact previewed revision.
    return owner_.invoke("property.commit",{{"scope",scope_},{"target",target_},{"changes",changes},{"confirmed",confirmed},{"revision",revision}});
}

QVariant PropertySession::apply(bool confirmed, const QString &revision)
{
    if (pending()) throw Error("请等待当前属性提交完成");
    if (!dirty()) return QVariantMap{{"updated",false}};
    try {
        auto candidate = changes_; const auto fields = schema();
        for (auto i = texts_.cbegin(); i != texts_.cend(); ++i) {
            const auto type = fields.value(i.key()).toMap().value("type").toString(); const auto text = i.value().toString();
            if (type == "number" || type == "integer") {
                bool ok = false; const auto number = text.toDouble(&ok);
                if (!ok || !std::isfinite(number)) throw Error(i.key()+" 需要有效数值");
                candidate[i.key()] = number;
            } else if (type == "json" || type == "object" || type == "array") {
                QJsonParseError error; const auto parsed = QJsonDocument::fromJson("["+text.toUtf8()+"]",&error);
                if (error.error != QJsonParseError::NoError || !parsed.isArray() || parsed.array().size() != 1) throw Error(i.key()+" 需要有效 JSON："+error.errorString());
                candidate[i.key()] = parsed.array().first().toVariant();
            } else candidate[i.key()] = text;
        }
        const auto current = values();
        for (auto i = base_.cbegin(); i != base_.cend(); ++i) if (current.value(i.key()) != i.value()) throw Error("属性已被其他操作修改，请检查或放弃草稿："+i.key());
        QVariant result;
        if (scope_ == "node") result = owner_.command("node.configure",{{"node",target_},{"changes",candidate},{"confirmed",confirmed},{"revision",revision}});
        else if (scope_ == "ui") result = owner_.command("ui.item.patch",{{"id",target_},{"properties",candidate}});
        else result = owner_.command("workspace.configure",{{"page",target_},{"changes",candidate}});
        const auto response = result.toMap();
        if (response.value("requires_confirmation").toBool()) return result;
        job_ = response.value("job").toString();
        if (job_.isEmpty()) { changes_.clear(); texts_.clear(); base_.clear(); }
        error_.clear(); emit changed(); return result;
    } catch (const std::exception &exception) { error_ = QString::fromUtf8(exception.what()); emit changed(); throw; }
}

void PropertySession::discard()
{
    if (pending()) return;
    changes_.clear(); texts_.clear(); base_.clear(); error_.clear(); emit changed();
}

QVariant PropertySession::action(const QString &name, const QVariantMap &arguments)
{
    const auto node = info().value("node").toString();
    if (node.isEmpty()) return QVariantMap{{"error","此界面对象没有计算节点，请使用 invoke 调用编辑器命令"}};
    return owner_.invoke("node.action",{{"node",node},{"action",name},{"args",arguments}});
}

QVariant PropertySession::configure(const QVariantMap &changes)
{
    const auto node = info().value("node").toString();
    if (node.isEmpty()) return QVariantMap{{"error","此视图没有计算节点"}};
    return owner_.invoke("node.configure",{{"node",node},{"changes",changes}});
}

QVariant PropertySession::invoke(const QString &operation, const QVariantMap &arguments)
{
    // This intentionally crosses the public command interface so user pages
    // inherit the same validation, draft guards and asynchronous job semantics.
    return owner_.invoke(operation,arguments);
}
}
