#pragma once

// All devices live on a dedicated Qt event thread. Calls return only after the
// requested native operation completes; incoming data is delivered independently.
#include "core/node.h"
#include <QThread>
#include <atomic>

namespace corig {
class IoWorker;
class IoHub {
public:
    IoHub();
    ~IoHub();
    void setCallbacks(std::function<void(const QString &,const Values &)> receive, std::function<void(const QString &,const QVariantMap &)> observe);
    QVariantMap transact(const QString &node, const QString &action, const QVariantMap &arguments);
    void shutdown();
private:
    IoWorker *worker_ = nullptr;
    QThread thread_;
    std::atomic_bool stopping_{false};
};
}
