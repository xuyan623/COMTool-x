#include "python_host.h"
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcessEnvironment>

static void initializePythonResource()
{
    Q_INIT_RESOURCE(python_worker);
}

namespace cx {

PythonHost::PythonHost(std::function<bool()> interrupted) : interrupted_(std::move(interrupted)) {}
PythonHost::~PythonHost() { close(); }

void PythonHost::selectInterpreter(const QString &path)
{
    if (!path.isEmpty() && (!QFileInfo(path).isAbsolute() || !QFileInfo(path).isFile())) throw Error("请选择实际存在的本地 python.exe");
    std::lock_guard guard(settingsMutex_); interpreter_ = path.isEmpty() ? QString{} : QFileInfo(path).absoluteFilePath();
}

QString PythonHost::interpreter() const
{
    std::lock_guard guard(settingsMutex_); return interpreter_;
}

void PythonHost::close()
{
    if (process_ && process_->state() != QProcess::NotRunning) {
        process_->write("{\"op\":\"shutdown\"}\n");
        if (!process_->waitForFinished(250)) { process_->kill(); process_->waitForFinished(1000); }
    }
    process_.reset(); loaded_.clear(); processInterpreter_.clear();
}

QVariantMap PythonHost::readReply(int timeout)
{
    QElapsedTimer elapsed; elapsed.start();
    while (elapsed.elapsed() < timeout) {
        stderrTail_ += process_->readAllStandardError(); stderrTail_ = stderrTail_.right(16384);
        if (process_->bytesAvailable() > 16 * 1024 * 1024) { close(); throw Error("Python 输出超过 16 MiB"); }
        if (process_->canReadLine()) {
            const auto line = process_->readLine(16 * 1024 * 1024 + 1);
            QJsonParseError error;
            const auto object = QJsonDocument::fromJson(line,&error);
            if (error.error != QJsonParseError::NoError || !object.isObject()) { close(); throw Error("Python 插件破坏了进程通信格式"); }
            return fromJsonValue(object.toVariant()).toMap();
        }
        if (interrupted_ && interrupted_()) { close(); throw Error("Python 执行已取消"); }
        if (process_->state() == QProcess::NotRunning) { const auto detail = QString::fromUtf8(stderrTail_); close(); throw Error("本地 Python 意外退出：" + detail); }
        process_->waitForReadyRead(20);
    }
    close(); throw Error("Python 执行超过 " + QString::number(timeout) + " ms，工作进程已终止");
}

void PythonHost::start()
{
    const auto path = interpreter();
    if (path.isEmpty()) throw Error("请在设置中选择本地 Python 环境；官方功能无需 Python");
    if (process_ && process_->state() != QProcess::NotRunning && processInterpreter_ == path) return;
    close();
    static const bool initialized = [] { initializePythonResource(); return true; }(); Q_UNUSED(initialized);
    QFile source(":/worker.py");
    if (!source.open(QIODevice::ReadOnly)) throw Error("Python 工作进程代码不可用");
    process_ = std::make_unique<QProcess>();
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.remove("PYTHONHOME"); environment.remove("PYTHONPATH");
    environment.insert("PYTHONUTF8","1"); environment.insert("PYTHONDONTWRITEBYTECODE","1");
    process_->setProcessEnvironment(environment);
    process_->setProgram(path); process_->setArguments({"-u","-B","-c",QString::fromUtf8(source.readAll())});
    process_->setProcessChannelMode(QProcess::SeparateChannels); stderrTail_.clear();
    process_->start();
    if (!process_->waitForStarted(5000)) { const auto error = process_->errorString(); close(); throw Error("本地 Python 无法启动：" + error); }
    processInterpreter_ = path;
    const auto handshake = readReply(5000);
    if (!handshake.value("ready").toBool() || handshake.value("protocol").toInt() != 3) { close(); throw Error(handshake.value("error","Python 工作进程协议不匹配").toString()); }
}

QVariantMap PythonHost::call(const QVariantMap &request)
{
    const auto operation = request.value("op").toString(), node = request.value("node").toString();
    if (operation == "drop" && !process_) { loaded_.remove(node); return {{"ok",true},{"dropped",node}}; }
    start();
    auto message = request;
    const auto hash = QCryptographicHash::hash(QJsonDocument::fromVariant(toJsonValue(QVariantMap{{"code",message.value("code")},{"resources",message.value("resources")},{"dependencies",message.value("dependencies")}})).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256);
    if (operation != "probe" && operation != "drop" && loaded_.value(node) == hash) { message.remove("code"); message.remove("resources"); message.remove("dependencies"); }
    auto bytes = QJsonDocument::fromVariant(toJsonValue(message)).toJson(QJsonDocument::Compact); bytes += '\n';
    if (bytes.size() > 16 * 1024 * 1024) throw Error("Python 请求超过 16 MiB");
    if (process_->write(bytes) != bytes.size()) { close(); throw Error("Python 请求发送失败"); }
    const auto response = readReply(std::clamp(message.value("timeout_ms",1000).toInt(),10,60000));
    if (!response.value("ok").toBool()) throw Error(response.value("logs").toString() + response.value("error").toString());
    if (operation == "drop") loaded_.remove(node);
    else if (operation != "probe") loaded_.insert(node,hash);
    return response;
}

} // namespace cx
