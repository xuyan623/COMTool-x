// A native client keeps automation usable on machines without Python. It reads
// current instance credentials locally and never prints the secret token.
#include "core/value.h"
#include "core/paths.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QTcpSocket>
#include <QThread>
#include <QUrl>
#include <cstdio>
#include <algorithm>

static QVariantMap readJson(const QString &path)
{
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).toVariant().toMap();
}

static QVariantMap request(const QVariantMap &record, const QString &operation, const QVariantMap &arguments, int timeout)
{
    // Connection records identify the product as well as the command revision.
    if (record.value("application") != "CoRig" || record.value("api_version").toInt() != 3) throw corig::Error("连接信息不是当前 CoRig 实例");
    const QUrl url(record.value("url").toString());
    if (url.scheme() != "http" || url.host() != "127.0.0.1" || url.port() < 1 || record.value("token").toString().isEmpty()) throw corig::Error("连接信息无效");
    QTcpSocket socket; socket.connectToHost(QHostAddress::LocalHost,static_cast<quint16>(url.port()));
    if (!socket.waitForConnected(timeout)) throw corig::Error("窗口无法连接");
    const bool probe = operation == "@schema";
    const auto body = QJsonDocument::fromVariant(QVariantMap{{"op",operation},{"args",arguments}}).toJson(QJsonDocument::Compact);
    socket.write((probe ? "GET /v3/schema" : "POST /v3/command") + QByteArray(" HTTP/1.1\r\nHost: 127.0.0.1\r\nAuthorization: Bearer ") + record.value("token").toString().toUtf8() + "\r\nConnection: close\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(probe ? 0 : body.size()) + "\r\n\r\n" + (probe ? QByteArray{} : body));
    QByteArray response; QElapsedTimer elapsed; elapsed.start();
    while (elapsed.elapsed() < timeout) {
        response += socket.readAll();
        if (response.size() > 256 * 1024 * 1024) throw corig::Error("响应超过 256 MiB");
        if (socket.state() == QAbstractSocket::UnconnectedState) break;
        socket.waitForReadyRead(std::min(100,timeout - static_cast<int>(elapsed.elapsed())));
    }
    response += socket.readAll(); const auto header = response.indexOf("\r\n\r\n");
    if (header < 0) throw corig::Error("接口未返回完整响应");
    QJsonParseError error; const auto document = QJsonDocument::fromJson(response.mid(header + 4),&error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) throw corig::Error("接口响应无效");
    const auto result = document.toVariant().toMap();
    if (probe && result.value("ok").toBool() && result.value("result").toMap().value("application") != "CoRig") throw corig::Error("目标窗口不是 CoRig");
    return result;
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc,argv); QCoreApplication::setOrganizationName("CoRig"); QCoreApplication::setApplicationName("CoRig"); const auto args = application.arguments();
    QVariantMap output; int exitCode = 0;
    try {
        if (args.size() < 3 || args[1] != "api") throw corig::Error("用法：CoRig-cli api 操作 --json '{...}' [--wait] [--data-dir 目录]");
        QString directory = corig::defaultDataDirectory(), discovery; QVariantMap arguments; int timeout = 30000; bool wait = false;
        for (int i = 3; i < args.size(); ++i) {
            const auto option = args[i];
            if (option == "--wait") { wait = true; continue; }
            if (++i >= args.size()) throw corig::Error("参数缺少值：" + option);
            const auto value = args[i];
            if (option == "--data-dir") directory = value;
            else if (option == "--discovery") discovery = value;
            else if (option == "--timeout") { bool valid; timeout = value.toInt(&valid); if (!valid || timeout < 100 || timeout > 3600000) throw corig::Error("超时需要 100～3600000 ms"); }
            else if (option == "--json" || option == "--json-file") {
                QByteArray bytes = value.toUtf8(); if (option == "--json-file") { QFile file(value); if (!file.open(QIODevice::ReadOnly)) throw corig::Error("参数文件无法读取"); bytes = file.readAll(); }
                QJsonParseError error; const auto document = QJsonDocument::fromJson(bytes,&error); if (error.error != QJsonParseError::NoError || !document.isObject()) throw corig::Error("参数需要 JSON 对象"); arguments = document.toVariant().toMap();
            } else throw corig::Error("未知参数：" + option);
        }
        QVariantMap record;
        if (!discovery.isEmpty()) record = readJson(discovery);
        else {
            const auto root = QDir(directory).filePath(".sessions"); const auto preferred = readJson(root + "/api.json");
            QList<QVariantMap> candidates; if (!preferred.isEmpty()) candidates.append(preferred);
            for (const auto &entry : QDir(root + "/instances").entryList({"api-*.json"},QDir::Files,QDir::Time)) candidates.append(readJson(root + "/instances/" + entry));
            for (const auto &candidate : candidates) try { if (request(candidate,"@schema",{},400).value("ok").toBool()) { record = candidate; break; } } catch (const corig::Error &) { }
        }
        if (record.isEmpty()) throw corig::Error("未找到可连接的 CoRig 窗口，请启动软件或指定 --data-dir");
        output = request(record,args[2],arguments,timeout);
        if (wait && output.value("ok").toBool()) {
            const auto submitted = output.value("result").toMap();
            const auto primary = submitted.value("job").toString();
            auto jobIds = submitted.value("jobs").toStringList();
            if (!primary.isEmpty()) jobIds.prepend(primary);
            QVariantMap completedJobs;
            QElapsedTimer elapsed; elapsed.start();
            for (const auto &jobId : jobIds) {
                bool complete = false;
                while (elapsed.elapsed() < timeout) {
                    const auto response = request(record,"job.get",{{"id",jobId}},std::min(timeout,10000));
                    if (!response.value("ok").toBool()) { output = response; break; }
                    const auto result = response.value("result").toMap(); const auto state = result.value("state").toString();
                    if (state == "complete") { completedJobs[jobId] = result.value("result"); complete = true; break; }
                    if (state == "failed" || state == "missing") { output = {{"ok",false},{"error",result.value("error","后台任务不存在或已失败")}}; break; }
                    QThread::msleep(25);
                }
                if (!output.value("ok").toBool()) break;
                if (!complete) throw corig::Error("等待超时；后台任务仍可通过 job.get 查询");
            }
            if (!jobIds.isEmpty() && output.value("ok").toBool()) {
                // A control may start several checked node actions. Preserve
                // the control result and report every actual completion.
                if (!primary.isEmpty()) output["result"] = completedJobs.value(primary);
                else { auto result = submitted; result["jobs_complete"] = true; result["job_results"] = completedJobs; output["result"] = result; }
            }
        }
        if (!output.value("ok").toBool()) exitCode = 1;
    } catch (const std::exception &error) { output = {{"ok",false},{"error",QString::fromUtf8(error.what())}}; exitCode = 1; }
    const auto bytes = QJsonDocument::fromVariant(output).toJson(QJsonDocument::Indented); std::fwrite(bytes.constData(),1,bytes.size(),stdout); return exitCode;
}
