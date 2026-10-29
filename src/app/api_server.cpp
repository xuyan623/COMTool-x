#include "api_server.h"
#include "controller.h"
#include "core/value.h"
#include <QTcpSocket>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QLockFile>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QTimer>

namespace corig {

static void saveRecord(const QString &path, const QVariantMap &value)
{
    QSaveFile file(path); const auto bytes = QJsonDocument::fromVariant(value).toJson(QJsonDocument::Indented);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) throw Error("无法发布本地连接信息：" + file.errorString());
}

static bool authenticated(const QByteArray &received, const QByteArray &expected)
{
    if (received.size() != expected.size()) return false;
    unsigned char difference = 0;
    for (qsizetype i = 0; i < received.size(); ++i) difference |= static_cast<unsigned char>(received[i] ^ expected[i]);
    return difference == 0;
}

ApiServer::ApiServer(Controller &controller) : controller_(controller)
{
    QByteArray secret(32,Qt::Uninitialized);
    for (auto &byte : secret) byte = static_cast<char>(QRandomGenerator::system()->generate() & 0xff);
    token_ = QString::fromLatin1(secret.toHex());
    if (!listen(QHostAddress::LocalHost,0)) throw Error("本地接口启动失败：" + errorString());
    connect(this,&QTcpServer::newConnection,this,[this] {
        while (hasPendingConnections()) {
            auto socket = nextPendingConnection(); socket->setReadBufferSize(8 * 1024 * 1024 + 16384);
            auto buffer = std::make_shared<QByteArray>(); auto done = std::make_shared<bool>(false);
            const auto respond = [socket,done](int status, const QVariant &value) {
                if (std::exchange(*done,true)) return;
                const auto body = QJsonDocument::fromVariant(toJsonValue(value)).toJson(QJsonDocument::Compact);
                socket->write("HTTP/1.1 " + QByteArray::number(status) + (status == 200 ? " OK\r\n" : " Error\r\n") + "Content-Type: application/json; charset=utf-8\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body);
                socket->disconnectFromHost();
            };
            connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
            QTimer::singleShot(10000,socket,[socket,done] { if (!*done) socket->abort(); });
            connect(socket,&QTcpSocket::readyRead,this,[this,socket,buffer,done,respond] {
                if (*done) { socket->readAll(); return; }
                *buffer += socket->readAll(); const auto boundary = buffer->indexOf("\r\n\r\n");
                if (boundary < 0) { if (buffer->size() > 16384) respond(431,QVariantMap{{"ok",false},{"error","请求头过大"}}); return; }
                if (boundary > 16384) { respond(431,QVariantMap{{"ok",false},{"error","请求头过大"}}); return; }
                const auto lines = buffer->left(boundary).split('\n'); const auto request = lines.value(0).trimmed().split(' ');
                QMap<QByteArray,QByteArray> headers;
                for (const auto &line : lines.mid(1)) { const auto at = line.indexOf(':'); if (at > 0) headers.insert(line.left(at).trimmed().toLower(),line.mid(at + 1).trimmed()); }
                if (!authenticated(headers.value("authorization"),("Bearer " + token_).toUtf8())) { respond(401,QVariantMap{{"ok",false},{"error","认证失败，请重新读取本次连接信息"}}); return; }
                if (headers.contains("transfer-encoding")) { respond(400,QVariantMap{{"ok",false},{"error","不接受分块请求"}}); return; }
                if (request.value(0) == "GET" && request.value(1) == "/v3/schema") { respond(200,QVariantMap{{"ok",true},{"result",controller_.schema()}}); return; }
                if (request.value(0) != "POST" || request.value(1) != "/v3/command") { respond(404,QVariantMap{{"ok",false},{"error","接口不存在"}}); return; }
                bool valid = false; const auto length = headers.value("content-length").toLongLong(&valid);
                if (!valid || length < 0 || length > 8 * 1024 * 1024) { respond(413,QVariantMap{{"ok",false},{"error","请求体需要有效长度且不超过 8 MiB"}}); return; }
                if (buffer->size() < boundary + 4 + length) return;
                QJsonParseError parse; const auto document = QJsonDocument::fromJson(buffer->mid(boundary + 4,length),&parse);
                if (parse.error != QJsonParseError::NoError || !document.isObject()) { respond(400,QVariantMap{{"ok",false},{"error","请求需要 JSON 对象"}}); return; }
                const auto body = document.toVariant().toMap();
                try { respond(200,QVariantMap{{"ok",true},{"result",controller_.command(body.value("op").toString(),body.value("args").toMap())}}); }
                catch (const std::exception &error) { respond(400,QVariantMap{{"ok",false},{"error",QString::fromUtf8(error.what())}}); }
            });
        }
    });
    publish();
}

void ApiServer::publish()
{
    const auto directory = controller_.dataDirectory() + "/.sessions"; QDir().mkpath(directory + "/instances");
    discovery_ = directory + "/instances/api-" + QString::number(QCoreApplication::applicationPid()) + "-" + QString::number(serverPort()) + ".json";
    alias_ = directory + "/api.json";
    record_ = {{"application","CoRig"},{"api_version",3},{"url","http://127.0.0.1:" + QString::number(serverPort())},{"token",token_},
        {"pid",QCoreApplication::applicationPid()},{"started_at",QDateTime::currentMSecsSinceEpoch() / 1000.}, {"discovery",discovery_}};
    QLockFile lock(directory + "/.api-discovery.lock"); if (!lock.tryLock(3000)) throw Error("本地连接信息正在更新");
    saveRecord(discovery_,record_); saveRecord(alias_,record_);
}

ApiServer::~ApiServer()
{
    close();
    // Remove only this process's published records. Other GUI instances keep
    // their independent discovery files; the CLI selects a surviving instance.
    QLockFile lock(QFileInfo(alias_).absolutePath() + "/.api-discovery.lock");
    if (lock.tryLock(1000)) {
        QFile alias(alias_); if (alias.open(QIODevice::ReadOnly)) { const auto current = QJsonDocument::fromJson(alias.readAll()).toVariant().toMap(); alias.close(); if (current.value("token") == token_) QFile::remove(alias_); }
        QFile::remove(discovery_);
    }
}

}
