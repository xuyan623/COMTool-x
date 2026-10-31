#include "python_host.h"
#include "resource_policy.h"
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QProcessEnvironment>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <future>
#include <thread>
#include <chrono>

static void initializePythonResource() { Q_INIT_RESOURCE(python_worker); }

namespace corig {
struct PythonHost::Worker {
    struct Request { QVariantMap message; std::promise<QVariantMap> result; };
    PythonHost &owner;
    QString package, interpreter;
    quint64 generation;
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<std::shared_ptr<Request>> requests;
    std::atomic_bool stopping{false};
    std::thread thread;

    Worker(PythonHost &host, QString key, QString executable, quint64 identity)
        : owner(host), package(std::move(key)), interpreter(std::move(executable)), generation(identity), thread([this] { run(); }) {}
    ~Worker() {
        stopping = true; changed.notify_all();
        if (thread.joinable()) thread.join();
    }
    QVariantMap submit(const QVariantMap &message) {
        auto request = std::make_shared<Request>(); request->message = message;
        auto result = request->result.get_future();
        { std::lock_guard guard(mutex); if (stopping) throw Error("Python 工作进程正在退出"); requests.push_back(request); }
        changed.notify_one(); return result.get();
    }

    void run() {
        // QProcess is created, read and destroyed exclusively on this thread.
        // A transport failure terminates only this package and its live tasks.
        std::unique_ptr<QProcess> process;
        QHash<QString,QByteArray> loaded;
        QByteArray stderrTail;
        quint64 sequence = 0;
        const auto stop = [&] {
            if (process && process->state() != QProcess::NotRunning) {
                process->write("{\"op\":\"shutdown\"}\n");
                if (!process->waitForFinished(250)) { process->kill(); process->waitForFinished(1000); }
            }
            process.reset(); loaded.clear();
        };
        const auto read = [&](int timeout, bool interruptible, quint64 expected) -> QVariantMap {
            QElapsedTimer elapsed; elapsed.start();
            while (elapsed.elapsed() < timeout) {
                stderrTail += process->readAllStandardError(); stderrTail = stderrTail.right(16384);
                const auto limit = resourceLimit("python_message_mib") * 1024 * 1024;
                if (process->bytesAvailable() > limit) throw Error("Python 输出超过 python_message_mib 额度");
                while (process->canReadLine()) {
                    const auto line = process->readLine(limit+1);
                    QJsonParseError error; const auto document = QJsonDocument::fromJson(line,&error);
                    if (error.error != QJsonParseError::NoError || !document.isObject()) throw Error("Python 插件破坏了进程通信格式");
                    const auto message = fromJsonValue(document.toVariant()).toMap();
                    if (message.contains("event")) { owner.event(package,generation,message); continue; }
                    if (expected && message.value("request").toULongLong() != expected) throw Error("Python 请求响应编号不一致");
                    return message;
                }
                if (stopping || (interruptible && owner.interrupted_ && owner.interrupted_())) throw Error("Python 执行已取消");
                if (process->state() == QProcess::NotRunning) throw Error("本地 Python 意外退出："+QString::fromUtf8(stderrTail));
                process->waitForReadyRead(10);
            }
            throw Error("Python 执行超过 "+QString::number(timeout)+" ms，工作进程已终止");
        };
        while (!stopping) {
            std::shared_ptr<Request> request;
            { std::unique_lock guard(mutex); if (requests.empty()) changed.wait_for(guard,std::chrono::milliseconds(10));
              if (!requests.empty()) { request = requests.front(); requests.pop_front(); } }
            if (!request) {
                // Progress and unsolicited producer output must be consumed even
                // when the engine has no synchronous request for this package.
                if (process) try {
                    process->waitForReadyRead(1);
                    stderrTail += process->readAllStandardError(); stderrTail = stderrTail.right(16384);
                    while (process->canReadLine()) {
                        const auto limit = resourceLimit("python_message_mib")*1024*1024;
                        QJsonParseError error; const auto document = QJsonDocument::fromJson(process->readLine(limit+1),&error);
                        if (error.error != QJsonParseError::NoError || !document.isObject()) throw Error("Python 后台消息格式无效");
                        const auto message = fromJsonValue(document.toVariant()).toMap();
                        if (!message.contains("event")) throw Error("Python 出现未请求的响应");
                        owner.event(package,generation,message);
                    }
                    if (process->bytesAvailable() > resourceLimit("python_message_mib")*1024*1024) throw Error("Python 后台消息超过额度");
                    if (process->state() == QProcess::NotRunning) throw Error("Python 后台工作进程意外退出");
                } catch (const std::exception &error) { owner.event(package,generation,{{"event","worker_failed"},{"error",QString::fromUtf8(error.what())}}); stop(); }
                continue;
            }
            try {
                auto message = request->message;
                const auto operation = message.value("op").toString();
                if (!process && operation == "drop") { request->result.set_value({{"ok",true}}); continue; }
                if (!process) {
                    static const bool initialized = [] { initializePythonResource(); return true; }(); Q_UNUSED(initialized);
                    QFile source(":/worker.py"); if (!source.open(QIODevice::ReadOnly)) throw Error("Python 工作进程代码不可用");
                    process = std::make_unique<QProcess>(); auto environment = QProcessEnvironment::systemEnvironment();
                    environment.remove("PYTHONHOME"); environment.remove("PYTHONPATH");
                    environment.insert("PYTHONUTF8","1"); environment.insert("PYTHONDONTWRITEBYTECODE","1");
                    environment.insert("CORIG_PYTHON_MESSAGE_BYTES",QString::number(resourceLimit("python_message_mib")*1024*1024));
                    process->setProcessEnvironment(environment); process->setProgram(interpreter);
                    process->setArguments({"-u","-B","-c",QString::fromUtf8(source.readAll())});
                    process->setProcessChannelMode(QProcess::SeparateChannels); stderrTail.clear(); process->start();
                    if (!process->waitForStarted(5000)) throw Error("本地 Python 无法启动："+process->errorString());
                    const auto handshake = read(5000,false,0);
                    if (!handshake.value("ready").toBool() || handshake.value("protocol").toInt() != 3) throw Error(handshake.value("error","Python 工作进程协议不匹配").toString());
                }
                const auto node = message.value("node").toString();
                const auto namespaceKey = (operation == "metadata" ? QString("metadata/") : QString("runtime/"))+node;
                const auto identity = QCryptographicHash::hash(QJsonDocument::fromVariant(toJsonValue(QVariantMap{
                    {"code",message.value("code")},{"resources",message.value("resources")},
                    {"directory",message.value("directory")},{"code_file",message.value("code_file")},
                    {"dependencies",message.value("dependencies")}})).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256);
                const bool loading = message.contains("code") || message.contains("code_file");
                if (loading && loaded.value(namespaceKey) == identity) for (const auto &key : {"code","resources","directory","code_file","dependencies"}) message.remove(key);
                message["request"] = ++sequence;
                const auto bytes = QJsonDocument::fromVariant(toJsonValue(message)).toJson(QJsonDocument::Compact)+'\n';
                if (bytes.size() > resourceLimit("python_message_mib")*1024*1024) throw Error("Python 请求超过 python_message_mib 额度");
                if (process->write(bytes) != bytes.size()) throw Error("Python 请求发送失败");
                const auto timeout = std::clamp(message.value("timeout_ms",resourceLimit("python_timeout_ms")).toLongLong(),1ll,resourceLimit("python_timeout_ms"));
                const auto response = read(static_cast<int>(timeout),!message.value("metadata").toBool(),sequence);
                // A rejected business operation is not a broken worker. Keep
                // its other node instances and background services alive.
                if (!response.value("ok").toBool()) {
                    // A namespace that cannot be dropped may still own an
                    // uncooperative thread. Retire the package before reuse.
                    if (operation == "drop") throw Error(response.value("logs").toString()+response.value("error").toString());
                    request->result.set_exception(std::make_exception_ptr(Error(response.value("logs").toString()+response.value("error").toString())));
                    continue;
                }
                if (operation == "drop") loaded.remove(namespaceKey); else if (loading) loaded.insert(namespaceKey,identity);
                request->result.set_value(response);
            } catch (...) {
                const auto failure = std::current_exception();
                try { std::rethrow_exception(failure); } catch (const std::exception &error) {
                    owner.event(package,generation,{{"event","worker_failed"},{"error",QString::fromUtf8(error.what())}});
                }
                stop(); request->result.set_exception(failure);
            }
        }
        stop();
        std::deque<std::shared_ptr<Request>> abandoned;
        { std::lock_guard guard(mutex); abandoned.swap(requests); }
        for (const auto &request : abandoned) request->result.set_exception(std::make_exception_ptr(Error("Python 工作进程已结束")));
    }
};

PythonHost::PythonHost(std::function<bool()> interrupted, Events events)
    : interrupted_(std::move(interrupted)), events_(std::move(events)) {}
PythonHost::~PythonHost() { close(); }

void PythonHost::selectInterpreter(const QString &path)
{
    if (!path.isEmpty() && (!QFileInfo(path).isAbsolute() || !QFileInfo(path).isFile())) throw Error("请选择实际存在的本地 python.exe");
    close(); std::lock_guard guard(mutex_); interpreter_ = path.isEmpty() ? QString{} : QFileInfo(path).absoluteFilePath();
}

QString PythonHost::interpreter() const { std::lock_guard guard(mutex_); return interpreter_; }

QVariantMap PythonHost::call(const QVariantMap &request)
{
    const auto package = request.value("package","scripts").toString();
    std::shared_ptr<Worker> worker;
    { std::lock_guard guard(mutex_);
      if (interpreter_.isEmpty()) throw Error("请在设置中选择本地 Python 环境；官方功能无需 Python");
      if (!workers_.contains(package)) workers_.insert(package,std::make_shared<Worker>(*this,package,interpreter_,++nextWorker_));
      worker = workers_.value(package); }
    return worker->submit(request);
}

void PythonHost::event(const QString &package, quint64 generation, const QVariantMap &message)
{
    {
        std::lock_guard guard(mutex_);
        // Retired workers may finish while a new worker for the same package
        // already exists. Their output must not affect the new generation.
        if (!workers_.contains(package) || workers_.value(package)->generation != generation) return;
        const auto id = message.value("task").toString();
        if (!id.isEmpty()) { auto &snapshot = tasks_[id]; for (auto i = message.cbegin(); i != message.cend(); ++i) snapshot[i.key()] = i.value(); taskPackages_[id] = package; taskGenerations_[id] = generation; }
        if (message.value("event") == "worker_failed") for (auto i = tasks_.begin(); i != tasks_.end(); ++i)
            if (taskPackages_.value(i.key()) == package && taskGenerations_.value(i.key()) == generation && (i.value().value("state") == "running" || i.value().value("state") == "pending"))
                { i.value()["state"] = "failed"; i.value()["error"] = message.value("error"); }
        if (tasks_.size() > 512) for (auto i = tasks_.begin(); i != tasks_.end() && tasks_.size() > 384;)
            if (i.value().value("state") != "running" && i.value().value("state") != "pending") { taskPackages_.remove(i.key()); taskGenerations_.remove(i.key()); i = tasks_.erase(i); } else ++i;
    }
    if (events_) events_(message);
}

QVariantMap PythonHost::task(const QString &id) const
{
    std::lock_guard guard(mutex_);
    return tasks_.value(id,{{"task",id},{"state","missing"}});
}

QVariantMap PythonHost::cancelTask(const QString &id)
{
    QString package; std::shared_ptr<Worker> worker; quint64 generation = 0;
    { std::lock_guard guard(mutex_);
      if (!taskPackages_.contains(id)) throw Error("后台任务不存在："+id);
      if (tasks_.value(id).value("state") != "running" && tasks_.value(id).value("state") != "pending") return tasks_.value(id);
      package = taskPackages_.value(id); generation = taskGenerations_.value(id); worker = workers_.value(package);
      if (!worker || worker->generation != generation) throw Error("此任务的 Python 工作进程已经结束"); }
    worker->submit({{"package",package},{"op","task.cancel"},{"task",id},{"metadata",true}});
    QElapsedTimer elapsed; elapsed.start();
    while (elapsed.elapsed() < resourceLimit("callback_timeout_ms")) {
        const auto state = task(id).value("state").toString();
        if (state == "cancelled" || state == "complete" || state == "failed") return task(id);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    // A non-cooperative Python thread cannot be killed alone. Terminate only
    // its package process; sibling packages continue running independently.
    { std::lock_guard guard(mutex_);
      if (workers_.value(package) == worker) workers_.remove(package);
      for (auto i = tasks_.begin(); i != tasks_.end(); ++i)
          if (taskPackages_.value(i.key()) == package && taskGenerations_.value(i.key()) == generation && (i.value().value("state") == "running" || i.value().value("state") == "pending"))
              { i.value()["state"] = "failed"; i.value()["error"] = "同包任务未响应取消，已结束其工作进程"; }
      tasks_[id]["state"] = "cancelled"; tasks_[id]["error"] = "任务已取消，其工作进程已结束"; }
    if (worker) { worker->stopping = true; worker->changed.notify_all(); worker.reset(); }
    return task(id);
}

void PythonHost::close()
{
    QMap<QString,std::shared_ptr<Worker>> workers;
    { std::lock_guard guard(mutex_); workers.swap(workers_);
      for (auto i = tasks_.begin(); i != tasks_.end(); ++i) if (i.value().value("state") == "running" || i.value().value("state") == "pending")
          { i.value()["state"] = "cancelled"; i.value()["error"] = "运行会话已结束"; } }
    for (const auto &worker : workers) { worker->stopping = true; worker->changed.notify_all(); }
    // Join outside the pool lock: final task events may need that same lock.
    workers.clear();
}
}
