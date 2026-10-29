#pragma once

// A local interpreter is chosen explicitly. The native program never installs
// Python/packages or searches PATH again after the user selected an executable.
#include "value.h"
#include <QProcess>
#include <functional>
#include <memory>
#include <mutex>

namespace corig {

class PythonHost {
public:
    explicit PythonHost(std::function<bool()> interrupted);
    ~PythonHost();
    void selectInterpreter(const QString &path);
    QString interpreter() const;
    QVariantMap call(const QVariantMap &request);
    void close();
private:
    void start();
    QVariantMap readReply(int timeout);
    std::function<bool()> interrupted_;
    mutable std::mutex settingsMutex_;
    QString interpreter_, processInterpreter_;
    std::unique_ptr<QProcess> process_;
    QHash<QString,QByteArray> loaded_;
    QByteArray stderrTail_;
};

} // namespace corig
