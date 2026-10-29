// External UI operations address the live Qt object tree. Generated paths
// include the page and visual ancestry; no desktop cursor or OS input is used.
#include "controller.h"
#include "workspaces.h"
#include <QCoreApplication>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QJSValue>
#include <QMetaEnum>
#include <QMetaProperty>
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <limits>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace cx {
namespace {

// QML-generated class suffixes vary between processes. The underlying Qt
// class and sibling ordinal provide a repeatable fallback for unnamed items.
QString itemType(const QObject *object)
{
    auto meta = object->metaObject();
    while (meta->superClass() && QByteArray(meta->className()).contains("_QML")) meta = meta->superClass();
    return QString::fromLatin1(meta->className());
}

// A QML id is local to its component and may repeat in delegates. The
// caller adds the sibling ordinal before composing the complete visual path.
QString localIdentity(QQuickItem *item)
{
    if (!item->objectName().isEmpty()) return item->objectName();
    if (const auto context = qmlContext(item)) {
        const auto name = context->nameForObject(item);
        if (!name.isEmpty()) return name;
    }
    return itemType(item);
}

struct Control {
    QPointer<QObject> object;
    QString handle, path, page, node, view, item;
};

// Traverse QObject ownership and visual children because Loader and popup
// content need not share the same ownership tree. The path follows visuals.
QList<Control> discoverControls(Workspaces *workspaces)
{
    QList<Control> result;
    QSet<QObject *> visited;
    std::function<void(QObject *)> collect = [&](QObject *object) {
        if (!object || visited.contains(object)) return;
        visited.insert(object);
        auto item = qobject_cast<QQuickItem *>(object);
        // Include read-only labels and custom painted items as well as
        // standard inputs; user views need no extra automation annotations.
        if (!object->objectName().isEmpty() || item) {
            Control entry; entry.object = object;
            // The generated handle belongs to the rendered QObject, so a
            // sibling reorder cannot silently address a different control.
            entry.handle = object->property("_comtoolxAutomationId").toString();
            if (entry.handle.isEmpty()) { entry.handle = "auto:" + newId("control"); object->setProperty("_comtoolxAutomationId",entry.handle); }
            QStringList segments;
            auto visual = item;
            if (!visual) {
                for (auto owner = object->parent(); owner && !visual; owner = owner->parent()) visual = qobject_cast<QQuickItem *>(owner);
                segments.append(QString::fromLatin1(QUrl::toPercentEncoding(object->objectName())));
            }
            for (auto current = visual; current; current = current->parentItem()) {
                QString name = current->objectName();
                if (name.startsWith("page.")) { entry.page = name.mid(5); break; }
                if (entry.node.isEmpty() && current->metaObject()->indexOfProperty("nodeId") >= 0) {
                    entry.node = current->property("nodeId").toString(); entry.view = current->property("viewKey").toString();
                }
                if (entry.item.isEmpty() && name.startsWith("ui.")) entry.item = name.mid(3);
                name = localIdentity(current);
                int ordinal = 0;
                if (const auto parent = current->parentItem()) for (auto sibling : parent->childItems()) {
                    if (sibling == current) break;
                    if (localIdentity(sibling) == name) ++ordinal;
                }
                name += "[" + QString::number(ordinal) + "]";
                segments.prepend(QString::fromLatin1(QUrl::toPercentEncoding(name)));
            }
            if (entry.page.isEmpty() && item && item->window()) entry.page = item->window()->objectName();
            if (entry.page.isEmpty()) entry.page = "application";
            if (segments.isEmpty()) segments.append(QString::fromLatin1(QUrl::toPercentEncoding(object->objectName())));
            entry.path = "auto:" + entry.page + ":" + segments.join('/');
            result.append(entry);
        }
        // Resolving a QML ID can materialize context/attached objects. Copy
        // ownership children before recursion so additions cannot invalidate
        // the QObject::children() container currently being traversed.
        const auto ownedChildren = object->children();
        for (auto child : ownedChildren) collect(child);
        if (item) for (auto child : item->childItems()) collect(child);
    };
    if (workspaces) {
        for (auto page : workspaces->pageItems()) collect(page);
        for (auto window : workspaces->nativeWindows()) collect(window);
    }
    // Old explicit IDs keep resolving to the usable visible instance when
    // the same view is rendered in the designer, runtime and plugin library.
    std::stable_sort(result.begin(),result.end(),[](const Control &a,const Control &b) {
        const auto rank = [](const Control &entry) {
            return entry.object->property("visible").toBool() * 2 + entry.object->property("enabled").toBool();
        };
        return rank(a) > rank(b);
    });
    return result;
}

// Descriptions contain values and supported operations so automation never
// needs private Qt pointers or guesses based on a widget's screen position.
QVariantMap describeControl(const Control &control, const QString &id)
{
    auto object = control.object.data();
    QVariantMap result{{"id",id},{"handle",control.handle},{"path",control.path},{"name",object->objectName()},{"type",itemType(object)},
                       {"page",control.page},{"node",control.node},{"view",control.view},{"ui_item",control.item}};
    const auto item = qobject_cast<QQuickItem *>(object);
    if (item) {
        result["visible"] = item->isVisible(); result["enabled"] = item->isEnabled();
        const auto bounds = item->mapRectToScene(item->boundingRect());
        result["bounds"] = QVariantList{bounds.x(),bounds.y(),bounds.width(),bounds.height()};
        result["window"] = item->window() ? item->window()->property("hostId") : QVariant{};
    }
    QStringList writable;
    for (const auto name : {"text","value","checked","currentIndex","editText","placeholderText","activeFocus","focus","inputMethodComposing",
                            "readOnly","from","to","stepSize","count","currentText","currentValue","displayText","selectedText","pressed","down","hovered"}) {
        const auto index = object->metaObject()->indexOfProperty(name);
        if (index < 0) continue;
        auto value = object->property(name);
        if (value.metaType() == QMetaType::fromType<QJSValue>()) value = value.value<QJSValue>().toVariant();
        result[name] = value;
        if (QStringList{"text","value","checked","currentIndex","editText"}.contains(QString::fromLatin1(name)) && object->metaObject()->property(index).isWritable()) writable.append(QString::fromLatin1(name));
    }
    QStringList actions;
    if (item) actions = {"focus","key","pointer","click"};
    if (!writable.isEmpty()) actions.append("set");
    if (object->metaObject()->indexOfMethod("accepted()") >= 0 || object->metaObject()->indexOfMethod("editingFinished()") >= 0) actions.append("commit");
    result["writable"] = writable; result["actions"] = actions;
    return result;
}

// Enum names follow Qt's public spelling (Return, ControlModifier, LeftButton).
// Numeric flags are accepted for clients that already use Qt constants.
int enumFlags(const QVariant &value, const char *type, int fallback = 0)
{
    if (!value.isValid()) return fallback;
    if (value.metaType().id() != QMetaType::QString && value.metaType().id() != QMetaType::QStringList) {
        const auto number = finiteNumber(value,QString::fromLatin1(type));
        if (number < 0. || number > std::numeric_limits<int>::max() || number != std::floor(number)) throw Error("Qt 枚举需要有效的非负整数");
        return int(number);
    }
    const auto meta = Qt::staticMetaObject.enumerator(Qt::staticMetaObject.indexOfEnumerator(type));
    const auto names = value.metaType().id() == QMetaType::QStringList ? value.toStringList() : value.toString().split('|');
    int result = 0;
    for (auto name : names) {
        auto bytes = name.trimmed().toLatin1();
        if (QByteArray(type) == "Key" && !bytes.startsWith("Key_")) bytes.prepend("Key_");
        bool ok = false; const auto flag = meta.keyToValue(bytes.constData(),&ok);
        if (!ok) throw Error("未知 Qt 枚举值：" + name);
        result |= flag;
    }
    return result;
}

// Internal Qt events operate only in the selected application's window.
// They do not move the system pointer, send OS keys, or require desktop focus.
void clickItem(QQuickItem *item)
{
    if (!item || !item->window()) throw Error("目标没有可交互的窗口");
    const auto point = item->mapToScene(item->boundingRect().center());
    const auto global = item->window()->mapToGlobal(point);
    QMouseEvent press(QEvent::MouseButtonPress,point,global,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease,point,global,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
    QCoreApplication::sendEvent(item->window(),&press); QCoreApplication::sendEvent(item->window(),&release);
}

// Text input and commit use Qt's normal key handling, including validation,
// IME-independent Unicode insertion and accepted/editingFinished ordering.
void keyItem(QQuickItem *item, int key, Qt::KeyboardModifiers modifiers, const QString &text)
{
    if (!item || !item->window()) throw Error("对象不能接收键盘输入");
    item->forceActiveFocus(Qt::OtherFocusReason);
    QKeyEvent press(QEvent::KeyPress,key,modifiers,text), release(QEvent::KeyRelease,key,modifiers,text);
    QCoreApplication::sendEvent(item->window(),&press); QCoreApplication::sendEvent(item->window(),&release);
}

} // namespace

QVariant Controller::controls(const QString &op, const QVariantMap &args)
{
    auto objects = discoverControls(workspaces_.get());
    const auto page = args.value("page").toString();
    if (!page.isEmpty()) objects.erase(std::remove_if(objects.begin(),objects.end(),[&](const Control &entry) { return entry.page != page; }),objects.end());
    if (op == "controls.list") {
        QVariantList result; QSet<QString> names;
        for (const auto &entry : objects) {
            const auto name = entry.object->objectName();
            const auto id = name.isEmpty() || names.contains(name) ? entry.handle : name;
            if (!name.isEmpty()) names.insert(name);
            result.append(describeControl(entry,id));
        }
        return result;
    }
    const auto id = args.value("id").toString();
    const auto found = std::find_if(objects.begin(),objects.end(),[&](const Control &entry) { return entry.handle == id || entry.path == id || (!entry.object->objectName().isEmpty() && entry.object->objectName() == id); });
    if (found == objects.end()) throw Error("控件不存在，请重新查询 controls.list：" + id);
    const auto entry = *found; QPointer<QObject> object = entry.object;
    const auto description = describeControl(entry,id);
    if (op == "controls.get") return description;
    if (!object->property("visible").toBool() || !object->property("enabled").toBool()) throw Error("控件当前不可操作：" + id);
    auto item = qobject_cast<QQuickItem *>(object.data());
    const auto previousJobs = QSet<QString>(jobs_.keyBegin(),jobs_.keyEnd());
    if (op == "controls.set") {
        const auto name = args.value("property","text").toString();
        if (!description.value("writable").toStringList().contains(name)) throw Error("该控件没有可写属性：" + name);
        if ((name == "text" || name == "editText") && object->property("readOnly").toBool()) throw Error("控件内容为只读");
        const auto property = object->metaObject()->property(object->metaObject()->indexOfProperty(name.toUtf8()));
        auto value = args.value("value");
        if ((property.metaType().flags() & QMetaType::IsPointer) || (!value.convert(property.metaType()) && property.metaType().id() != QMetaType::QVariant)) throw Error("控件属性类型不匹配");
        if ((name == "value" || name == "currentIndex") && !value.isNull()) finiteNumber(value,name);
        if (name == "currentIndex" && (value.toInt() < -1 || value.toInt() >= object->property("count").toInt())) throw Error("选项索引越界");
        if (name == "checked" && object->property("checked") != value && object->metaObject()->indexOfMethod("click()") >= 0) QMetaObject::invokeMethod(object,"click");
        else {
            if (!property.write(object,value)) throw Error("控件属性无法设置");
            if (!object) return QVariantMap{{"updated",true},{"removed",true}};
            // User-edit signals are distinct from property change signals in
            // Qt Quick. Emit the same commit signal the real widget uses.
            if (name == "currentIndex" && object->metaObject()->indexOfMethod("activated(int)") >= 0) QMetaObject::invokeMethod(object,"activated",Q_ARG(int,value.toInt()));
            else if (name == "value" && object->metaObject()->indexOfMethod("moved()") >= 0) QMetaObject::invokeMethod(object,"moved");
            else if (name == "value" && object->metaObject()->indexOfMethod("valueModified()") >= 0) QMetaObject::invokeMethod(object,"valueModified");
        }
    } else if (op == "controls.commit") {
        if (!description.value("actions").toStringList().contains("commit")) throw Error("该控件没有提交动作");
        if (object->inherits("QQuickTextInput") || object->inherits("QQuickComboBox")) keyItem(item,Qt::Key_Return,Qt::NoModifier,"\r");
        else {
            const auto method = object->metaObject()->indexOfMethod("accepted()") >= 0 ? "accepted" : "editingFinished";
            if (!QMetaObject::invokeMethod(object,method)) throw Error("该控件没有提交动作");
        }
    } else if (op == "controls.click") {
        if (object->metaObject()->indexOfMethod("click()") >= 0) QMetaObject::invokeMethod(object,"click");
        else clickItem(item);
    } else if (op == "controls.focus") {
        if (!item) throw Error("对象不能接收键盘焦点");
        item->forceActiveFocus(Qt::OtherFocusReason);
    } else if (op == "controls.key") {
        if (!item || !item->window()) throw Error("对象不能接收键盘输入");
        const auto text = args.value("text").toString();
        if (!args.contains("key") && text.isEmpty()) throw Error("需要 key 或 text");
        const auto key = enumFlags(args.value("key"),"Key");
        const auto modifiers = Qt::KeyboardModifiers(enumFlags(args.value("modifiers"),"KeyboardModifier"));
        keyItem(item,key,modifiers,text);
    } else if (op == "controls.pointer") {
        if (!item || !item->window()) throw Error("对象不能接收指针输入");
        const auto type = args.value("type","click").toString();
        if (!QStringList{"click","press","move","release","double_click","wheel"}.contains(type)) throw Error("未知指针操作");
        const QPointF local(args.contains("x") ? finiteNumber(args.value("x"),"x") : item->width()/2.,args.contains("y") ? finiteNumber(args.value("y"),"y") : item->height()/2.);
        const auto point = item->mapToScene(local);
        const auto global = item->window()->mapToGlobal(point);
        const auto button = Qt::MouseButton(enumFlags(args.value("button"),"MouseButton",Qt::LeftButton));
        const auto modifiers = Qt::KeyboardModifiers(enumFlags(args.value("modifiers"),"KeyboardModifier"));
        if (type == "wheel") {
            const QPoint delta(int(finiteNumber(args.value("delta_x",0),"delta_x")),int(finiteNumber(args.value("delta_y",0),"delta_y")));
            QWheelEvent event(point,global,{},delta,Qt::NoButton,modifiers,Qt::NoScrollPhase,false);
            QCoreApplication::sendEvent(item->window(),&event);
        } else {
            const auto buttons = Qt::MouseButtons(enumFlags(args.value("buttons"),"MouseButton",type == "release" ? Qt::NoButton : button));
            const auto eventType = type == "move" ? QEvent::MouseMove : type == "release" ? QEvent::MouseButtonRelease : type == "double_click" ? QEvent::MouseButtonDblClick : QEvent::MouseButtonPress;
            QMouseEvent event(eventType,point,global,type == "move" ? Qt::NoButton : button,buttons,modifiers);
            QCoreApplication::sendEvent(item->window(),&event);
            if (type == "click" || type == "double_click") {
                QMouseEvent release(QEvent::MouseButtonRelease,point,global,button,Qt::NoButton,modifiers);
                QCoreApplication::sendEvent(item->window(),&release);
            }
        }
    } else throw Error("未知控件操作：" + op);
    QStringList jobs;
    for (auto it = jobs_.keyBegin(); it != jobs_.keyEnd(); ++it) if (!previousJobs.contains(*it)) jobs.append(*it);
    return QVariantMap{{"updated",true},{"jobs",jobs},{"control",object ? describeControl(entry,id) : QVariantMap{{"removed",true}}}};
}

// Editor presentation is not part of the saved functional design. Reading or
// changing it remains available in read-only mode and during computation.
QVariant Controller::editorView(const QString &op, const QVariantMap &args)
{
    const auto page = args.value("page").toString();
    if (page != "graph" && page != "layout") throw Error("编辑页面需要 graph 或 layout");
    if (!workspaces_) throw Error("工作区尚未建立");
    if (!workspaces_->page(page)) workspaces_->open(page);
    const auto item = workspaces_->page(page);
    auto options = args; options.remove("page");
    for (const auto name : {"zoom","pan_x","pan_y","left_width","right_width"}) if (options.contains(name)) {
        const double value = finiteNumber(options.value(name),QString::fromLatin1(name));
        if (QByteArray(name) == "zoom" && (value < .1 || value > 3.)) throw Error("缩放需要在 0.1～3 之间");
        if ((QByteArray(name) == "left_width" || QByteArray(name) == "right_width") && (value < 0. || value > 420.)) throw Error("侧栏宽度需要在 0～420 之间");
        if (std::abs(value) > 1e7) throw Error("画布平移超出范围");
    }
    if (page != "graph" && options.contains("animation")) throw Error("数据活动动画仅属于功能设计");
    QVariant result;
    const bool ok = op == "editor.state" ? QMetaObject::invokeMethod(item,"automationState",Q_RETURN_ARG(QVariant,result)) :
        QMetaObject::invokeMethod(item,"configureAutomation",Q_RETURN_ARG(QVariant,result),Q_ARG(QVariant,QVariant(options)));
    if (!ok) throw Error("编辑页面没有提供状态接口");
    if (result.metaType() == QMetaType::fromType<QJSValue>()) result = result.value<QJSValue>().toVariant();
    auto state = result.toMap(); state["page"] = page;
    return state;
}

// All fields are checked before touching the live native window. Window
// state and geometry use Qt directly and never modify machine-wide settings.
QVariant Controller::configureWindow(const QVariantMap &args)
{
    if (!workspaces_) throw Error("工作区尚未建立");
    const auto id = args.value("window").toString(); const auto window = workspaces_->window(id);
    if (!window) throw Error("窗口不存在：" + id);
    QRect geometry = window->geometry();
    for (const auto name : {"x","y","width","height"}) if (args.contains(name)) {
        const double value = finiteNumber(args.value(name),QString::fromLatin1(name));
        const bool size = QByteArray(name) == "width" || QByteArray(name) == "height";
        if (size ? value < 1 || value > 16384 : std::abs(value) > 100000) throw Error("窗口位置或尺寸越界");
        if (QByteArray(name) == "x") geometry.moveLeft(qRound(value));
        if (QByteArray(name) == "y") geometry.moveTop(qRound(value));
        if (QByteArray(name) == "width") geometry.setWidth(qRound(value));
        if (QByteArray(name) == "height") geometry.setHeight(qRound(value));
    }
    const auto state = args.value("state").toString();
    if (!state.isEmpty() && !QStringList{"normal","minimized","maximized","fullscreen"}.contains(state)) throw Error("未知窗口状态");
    if (args.contains("x") || args.contains("y") || args.contains("width") || args.contains("height")) window->setGeometry(geometry);
    if (state == "normal") window->showNormal();
    if (state == "minimized") window->showMinimized();
    if (state == "maximized") window->showMaximized();
    if (state == "fullscreen") window->showFullScreen();
#ifdef Q_OS_WIN
    // A process launched with SW_HIDE can leave the HWND hidden even when Qt
    // already reports a normal visible window. Honor an explicit restore at
    // the native window as well; offscreen test handles are never passed here.
    if (QGuiApplication::platformName() == "windows" && (state == "normal" || state == "maximized" || state == "fullscreen")) {
        const auto handle = reinterpret_cast<HWND>(window->winId());
        if (IsWindow(handle) && !IsWindowVisible(handle)) ShowWindow(handle,state == "maximized" ? SW_SHOWMAXIMIZED : SW_SHOW);
    }
#endif
    if (args.value("focus").toBool()) { window->raise(); window->requestActivate(); }
    for (const auto &entry : workspaces_->windows()) if (entry.toMap().value("id") == id) return entry;
    throw Error("窗口已经关闭");
}

} // namespace cx
