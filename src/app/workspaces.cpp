#include "workspaces.h"
#include "controller.h"
#include "core/value.h"
#include <QQmlEngine>
#include <QQmlComponent>
#include <QGuiApplication>
#include <QScreen>
#include <QDrag>
#include <QMimeData>
#include <QPainter>
#include <QJsonDocument>
#include <QCursor>
#include <windows.h>

namespace corig {
static const QMap<QString,QString> Titles{{"graph","功能设计"},{"layout","界面设计"},{"runtime","运行界面"},{"logs","日志"},{"library","插件库"}};
static const QMap<QString,QString> Components{{"graph","GraphPage"},{"layout","LayoutPage"},{"runtime","RuntimePage"},{"logs","LogPage"},{"library","LibraryPage"}};

Workspaces::Workspaces(QQmlEngine *engine, Controller &controller) : engine_(engine), controller_(controller), session_(newId("session")) {}

Workspaces::~Workspaces()
{
    for (const auto &page : pages_) if (page) { page->setParentItem(nullptr); delete page; }
    pages_.clear();
    for (const auto &host : hosts_) if (host.window) delete host.window;
}

QString Workspaces::createWindow(const QPoint &position, bool child)
{
    const auto id = newId("window");
    QQmlComponent component(engine_,QUrl("qrc:/qt/qml/CoRig/WorkspaceWindow.qml"));
    const auto object = component.createWithInitialProperties({{"hostId",id},{"childWindow",child}});
    const auto window = qobject_cast<QQuickWindow *>(object);
    if (!window) { delete object; throw Error("窗口创建失败：" + component.errorString()); }
    window->QObject::setParent(this);
    const auto frames = std::make_shared<Frames>();
    connect(window,&QQuickWindow::frameSwapped,window,[frames] {
        const auto now = std::chrono::steady_clock::now(); std::lock_guard guard(frames->mutex);
        if (frames->count) frames->intervals[(frames->count - 1) % frames->intervals.size()] = std::chrono::duration<double,std::milli>(now - frames->last).count();
        frames->last = now; ++frames->count;
    },Qt::DirectConnection);
    hosts_.insert(id,Host{id,{}, {},window,child,false,frames});
    if (child) {
        // Compact page windows share the project, but are independent native
        // windows. Tool/owned windows are forced above their owner by Windows;
        // only the explicit pin action should opt into always-on-top behavior.
        window->setFlags(Qt::Window | Qt::WindowTitleHint | Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint | Qt::WindowMinMaxButtonsHint);
    }
    if (!position.isNull()) {
        const auto screen = QGuiApplication::screenAt(position); const auto area = (screen ? screen : QGuiApplication::primaryScreen())->availableGeometry();
        window->setPosition(std::clamp(position.x(),area.left(),std::max(area.left(),area.right() - window->width())),std::clamp(position.y(),area.top(),std::max(area.top(),area.bottom() - window->height())));
    }
    window->show(); return id;
}

QQuickItem *Workspaces::ensurePage(const QString &key)
{
    if (!Components.contains(key)) throw Error("未知工作区：" + key);
    if (pages_.value(key)) return pages_.value(key);
    QQmlComponent component(engine_,QUrl("qrc:/qt/qml/CoRig/" + Components.value(key) + ".qml"));
    const auto object = component.create(); auto item = qobject_cast<QQuickItem *>(object);
    if (!item) { delete object; throw Error("页面创建失败：" + component.errorString()); }
    item->setParent(this); item->setVisible(false); pages_.insert(key,item); return item;
}

QString Workspaces::owningHost(const QString &page) const
{
    for (const auto &host : hosts_) if (host.tabs.contains(page)) return host.id;
    return {};
}

void Workspaces::synchronize()
{
    for (const auto &host : hosts_) if (host.window) {
        host.window->setProperty("childWindow",host.child); host.window->setProperty("pinned",host.pinned);
        QVariantList tabs;
        for (const auto &key : host.tabs) tabs.append(QVariantMap{{"key",key},{"title",Titles.value(key)}});
        host.window->setProperty("tabs",tabs); host.window->setProperty("activePage",host.active);
        auto content = host.window->findChild<QQuickItem *>("workspace.content");
        if (!content) throw Error("窗口缺少工作区容器");
        for (const auto &key : host.tabs) {
            auto item = pages_.value(key); item->setParentItem(content);
            item->setSize(content->size()); item->setVisible(key == host.active);
        }
    }
    emit changed();
}

QVariantMap Workspaces::open(const QString &key, const QString &preferredWindow)
{
    ensurePage(key);
    auto owner = owningHost(key);
    // The library is a unique project-owned browser, opened in a compact child
    // from every entry point. Reopening focuses that same browser instance.
    if (key == "library" && (owner.isEmpty() || !hosts_[owner].child)) {
        const auto child = createWindow({},true);
        if (owner.isEmpty()) { hosts_[child].tabs.append(key); owner = child; }
        else { movePage(key,child); owner = child; }
    }
    if (owner.isEmpty()) {
        owner = hosts_.contains(preferredWindow) ? preferredWindow : QString{};
        if (owner.isEmpty()) for (const auto &host : hosts_) if (!host.child) { owner = host.id; break; }
        if (owner.isEmpty()) owner = hosts_.isEmpty() ? createWindow() : hosts_.firstKey();
        hosts_[owner].tabs.append(key);
    }
    hosts_[owner].active = key; synchronize();
    hosts_[owner].window->show(); hosts_[owner].window->raise(); hosts_[owner].window->requestActivate();
    return {{"page",key},{"window",owner}};
}

void Workspaces::removeEmpty(const QString &host)
{
    if (!hosts_.contains(host) || !hosts_[host].tabs.isEmpty()) return;
    // The main shell remains available when its last tab is floating. Detaching
    // that tab must not turn the new compact child back into a full main window.
    if (!hosts_[host].child) return;
    const auto window = hosts_[host].window; hosts_.remove(host);
    if (window) { window->close(); window->deleteLater(); }
}

QVariantMap Workspaces::closePage(const QString &key, bool discard)
{
    const auto owner = owningHost(key);
    if (owner.isEmpty()) return {{"closed",false}};
    if (hosts_[owner].tabs.size() == 1) {
        const bool closed = closeWindow(owner,discard);
        if (closed) if (const auto native = window(owner)) native->close();
        return {{"closed",closed}};
    }
    auto &host = hosts_[owner]; const auto index = host.tabs.indexOf(key); host.tabs.removeAt(index);
    pages_[key]->setVisible(false); pages_[key]->setParentItem(nullptr);
    if (host.active == key) host.active = host.tabs[std::min(index,host.tabs.size() - 1)];
    synchronize(); return {{"closed",true}};
}

bool Workspaces::closeWindow(const QString &id, bool discard)
{
    if (!hosts_.contains(id)) return true;
    if (hosts_.size() == 1 && !controller_.confirmExit(id,discard)) return false;
    const auto host = hosts_.take(id);
    if (!host.child && !hosts_.isEmpty()) promoteMain();
    for (const auto &key : host.tabs) if (pages_.value(key)) { pages_[key]->setVisible(false); pages_[key]->setParentItem(nullptr); }
    if (host.window) host.window->deleteLater();
    emit changed(); return true;
}

QVariantMap Workspaces::movePage(const QString &key, const QString &window, int index)
{
    const auto owner = owningHost(key);
    if (owner.isEmpty() || !hosts_.contains(window)) throw Error("标签页或目标窗口不存在");
    if (key == "library" && window != owner) throw Error("插件库是独立小窗，不能移入工作区标签");
    if (hosts_[window].tabs.contains("library") && key != "library") throw Error("插件库窗口不接收工作区标签");
    auto &old = hosts_[owner]; old.tabs.removeAll(key);
    if (old.active == key) old.active = old.tabs.isEmpty() ? QString{} : old.tabs.first();
    auto &next = hosts_[window];
    next.tabs.insert(index < 0 ? next.tabs.size() : std::clamp<qsizetype>(index,0,next.tabs.size()),key); next.active = key;
    removeEmpty(owner); synchronize(); return {{"page",key},{"window",window}};
}

QVariantMap Workspaces::detach(const QString &key, const QPoint &position)
{
    if (key == "library") return open(key);
    if (owningHost(key).isEmpty()) throw Error("标签页尚未打开");
    const auto host = createWindow(position.isNull() ? QCursor::pos() - QPoint(100,20) : position,true);
    return movePage(key,host);
}

void Workspaces::dragPage(const QString &key)
{
    if (owningHost(key).isEmpty()) return;
    QDrag drag(this); auto mime = new QMimeData;
    mime->setData("application/x-corig-workspace",QJsonDocument::fromVariant(QVariantMap{{"session",session_},{"page",key}}).toJson(QJsonDocument::Compact)); drag.setMimeData(mime);
    QPixmap preview(170,34); preview.fill(QColor("#243446")); QPainter painter(&preview); painter.setPen(QColor("#E6EDF5")); painter.drawText(preview.rect().adjusted(12,0,-8,0),Qt::AlignVCenter,Titles.value(key)); painter.end();
    drag.setPixmap(preview); drag.setHotSpot({50,17});
    const auto result = drag.exec(Qt::MoveAction);
    // Escape cancels; releasing outside every tab strip detaches the page.
    if (result == Qt::IgnoreAction && !(GetAsyncKeyState(VK_ESCAPE) & 0x8000) && !(GetAsyncKeyState(VK_LBUTTON) & 0x8000) && !owningHost(key).isEmpty()) detach(key);
}

QVariantMap Workspaces::acceptDrop(const QString &payload, const QString &window, int index)
{
    const auto object = QJsonDocument::fromJson(payload.toUtf8()).toVariant().toMap();
    if (object.value("session") != session_) throw Error("标签只能移动到同一工程的窗口");
    return movePage(object.value("page").toString(),window,index);
}

void Workspaces::cycle(const QString &window, int direction)
{
    if (!hosts_.contains(window) || hosts_[window].tabs.isEmpty()) return;
    auto &host = hosts_[window]; const auto index = host.tabs.indexOf(host.active);
    host.active = host.tabs[(index + direction + host.tabs.size()) % host.tabs.size()]; synchronize();
}

QVariantList Workspaces::windows() const
{
    QVariantList result;
    for (const auto &host : hosts_) if (host.window) {
        QVariantList intervals; quint64 count;
        { std::lock_guard guard(host.frames->mutex); count = host.frames->count;
          const auto retained = std::min<quint64>(count ? count - 1 : 0,host.frames->intervals.size());
          for (auto i = count - (count > 0) - retained; i < count - (count > 0); ++i) intervals.append(host.frames->intervals[i % host.frames->intervals.size()]); }
        result.append(QVariantMap{{"id",host.id},{"active",host.active},{"tabs",host.tabs},{"child",host.child},{"pinned",host.pinned},
        {"focused",host.window->isActive()},
        {"visible",host.window->isVisible()},{"exposed",host.window->isExposed()},
        {"native_visible",QGuiApplication::platformName() == "windows" ? bool(IsWindowVisible(reinterpret_cast<HWND>(host.window->winId()))) : host.window->isVisible()},
        {"native_owned",GetWindow(reinterpret_cast<HWND>(host.window->winId()),GW_OWNER) != nullptr},
        {"native_topmost",bool(GetWindowLongPtr(reinterpret_cast<HWND>(host.window->winId()),GWL_EXSTYLE) & WS_EX_TOPMOST)},
        {"frame_count",QVariant::fromValue(count)},{"frame_intervals_ms",intervals},
        {"x",host.window->x()},{"y",host.window->y()},{"width",host.window->width()},{"height",host.window->height()},
        {"state",host.window->visibility() == QWindow::Minimized ? "minimized" : host.window->visibility() == QWindow::Maximized ? "maximized" : host.window->visibility() == QWindow::FullScreen ? "fullscreen" : "normal"}});
    }
    return result;
}
QVariantMap Workspaces::state() const { return {{"session",session_},{"windows",windows()}}; }
QList<QQuickWindow *> Workspaces::nativeWindows() const { QList<QQuickWindow *> result; for (const auto &host : hosts_) if (host.window) result.append(host.window); return result; }
QList<QQuickItem *> Workspaces::pageItems() const { QList<QQuickItem *> result; for (const auto &page : pages_) if (page) result.append(page); return result; }
QQuickItem *Workspaces::page(const QString &key) const { return pages_.value(key); }
QQuickWindow *Workspaces::window(const QString &id) const { return hosts_.value(id).window; }

QVariantMap Workspaces::pinWindow(const QString &id, bool enabled)
{
    if (!hosts_.contains(id) || !hosts_[id].window) throw Error("窗口不存在");
    auto &host = hosts_[id]; host.pinned = enabled;
    const auto rectangle = host.window->geometry();
    host.window->setFlag(Qt::WindowStaysOnTopHint,enabled);
    host.window->setGeometry(rectangle); host.window->show(); host.window->raise();
    synchronize(); return {{"window",id},{"pinned",enabled}};
}

void Workspaces::focusMain()
{
    for (const auto &host : hosts_) if (!host.child && host.window) { host.window->show(); host.window->raise(); host.window->requestActivate(); return; }
}

void Workspaces::promoteMain()
{
    auto &main = hosts_.first(); main.child = false;
    if (main.window) {
        main.window->setTransientParent(nullptr); main.window->setFlags(Qt::Window | (main.pinned ? Qt::WindowStaysOnTopHint : Qt::WindowFlags{}));
        main.window->setProperty("childWindow",false); main.window->show();
    }
    synchronize();
}

} // namespace corig
