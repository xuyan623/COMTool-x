#pragma once

// Tabs move the actual page item between native windows; a second copy never
// creates a second graph/session. Only window presentation belongs here.
#include <QObject>
#include <QVariant>
#include <QPointer>
#include <QQuickWindow>
#include <QQuickItem>
#include <array>
#include <chrono>
#include <mutex>
#include <memory>

class QQmlEngine;
namespace corig {
class Controller;

class Workspaces final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList windows READ windows NOTIFY changed)
    Q_PROPERTY(QVariantList available READ available NOTIFY changed)
public:
    Workspaces(QQmlEngine *engine, Controller &controller);
    ~Workspaces() override;
    QVariantList windows() const;
    QVariantList available() const;
    QVariantMap state() const;
    QList<QQuickWindow *> nativeWindows() const;
    QList<QQuickItem *> pageItems() const;
    QQuickItem *page(const QString &key) const;
    QQuickWindow *window(const QString &id) const;
    Q_INVOKABLE QVariantMap open(const QString &key, const QString &preferredWindow = {});
    Q_INVOKABLE QVariantMap closePage(const QString &key, bool discard = false);
    Q_INVOKABLE bool closeWindow(const QString &id, bool discard = false);
    Q_INVOKABLE QVariantMap movePage(const QString &key, const QString &window, int index = -1);
    Q_INVOKABLE QVariantMap detach(const QString &key, const QPoint &position = {});
    Q_INVOKABLE void dragPage(const QString &key);
    Q_INVOKABLE QVariantMap acceptDrop(const QString &payload, const QString &window, int index);
    Q_INVOKABLE void cycle(const QString &window, int direction);
    Q_INVOKABLE QVariantMap pinWindow(const QString &window, bool enabled);
    Q_INVOKABLE void focusMain();
signals:
    void changed();
private:
    // Frame telemetry is bounded and recorded at the renderer's actual swap
    // signal. It never schedules work or changes engine/display priorities.
    struct Frames { std::mutex mutex; std::array<double,512> intervals{}; quint64 count = 0; std::chrono::steady_clock::time_point last{}; };
    struct Host { QString id, active; QStringList tabs; QPointer<QQuickWindow> window; bool child = false, pinned = false; std::shared_ptr<Frames> frames; };
    QString createWindow(const QPoint &position = {}, bool child = false);
    void promoteMain();
    QQuickItem *ensurePage(const QString &key);
    void synchronize();
    void removeEmpty(const QString &host);
    void refreshPages();
    QString owningHost(const QString &page) const;
    QQmlEngine *engine_;
    Controller &controller_;
    QMap<QString,Host> hosts_;
    QMap<QString,QPointer<QQuickItem>> pages_;
    QMap<QString,QString> pageHashes_;
    QString session_;
};
}
