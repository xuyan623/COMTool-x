#pragma once

// An SSH connection owns its socket, host verification, authentication and PTY.
// It is used only by the IO thread; no GUI object or Python interpreter is involved.
#include "core/value.h"
#include <memory>

namespace cx {
class SshSession {
public:
    SshSession();
    ~SshSession();
    void open(const QVariantMap &config, const QByteArray &privateKey, int columns, int rows);
    void close();
    qsizetype write(const QByteArray &bytes);
    QByteArray readAvailable();
    void resize(int columns, int rows);
    bool connected() const;
    qintptr descriptor() const;
    QString fingerprint() const;
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
