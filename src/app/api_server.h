#pragma once

// The local endpoint accepts authenticated loopback requests only. Commands
// execute on the owning GUI thread and return jobs for background operations.
#include <QTcpServer>
#include <QVariantMap>

namespace cx {
class Controller;
class ApiServer final : public QTcpServer {
public:
    explicit ApiServer(Controller &controller);
    ~ApiServer() override;
private:
    void publish();
    Controller &controller_;
    QString token_, discovery_, alias_;
    QVariantMap record_;
};
}
