#include "io_hub.h"
#include "ssh_session.h"
#include "protocol_node.h"
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QTcpSocket>
#include <QTcpServer>
#include <QUdpSocket>
#include <QHostInfo>
#include <QNetworkDatagram>
#include <QSocketNotifier>
#include <QTimer>
#include <QPointer>
#include <QElapsedTimer>
#include <future>
#include <windows.h>

namespace corig {

struct Device {
    QString id, kind;
    QVariantMap config, state;
    std::unique_ptr<QSerialPort> serial;
    std::unique_ptr<QTcpSocket> tcp;
    std::unique_ptr<QTcpServer> server;
    QList<QPointer<QTcpSocket>> clients;
    std::unique_ptr<QUdpSocket> udp;
    std::unique_ptr<SshSession> ssh;
    std::unique_ptr<QSocketNotifier> notifier;
    std::unique_ptr<QTimer> reconnect;
    QHostAddress peer;
    quint16 peerPort = 0;
    QByteArray privateKey;
    int columns = 80, rows = 24;
    bool wanted = false, loopback = false;
    quint64 received = 0, sent = 0;
};

class IoWorker final : public QObject {
public:
    std::function<void(const QString &,const Values &)> receive;
    std::function<void(const QString &,const QVariantMap &)> observe;
    QHash<QString,std::shared_ptr<Device>> devices;

    QVariantMap describe(const std::shared_ptr<Device> &device)
    {
        auto &state = device->state;
        state["rx_bytes"] = device->received; state["tx_bytes"] = device->sent;
        QVariantList clients;
        for (const auto &client : device->clients) if (client && client->state() == QAbstractSocket::ConnectedState)
            clients.append(QVariantMap{{"id",QString::number(client->socketDescriptor())},{"address",client->peerAddress().toString()},{"port",client->peerPort()}});
        state["clients"] = clients;
        if (device->ssh) state["host_fingerprint"] = device->ssh->fingerprint();
        if (observe) observe(device->id,state);
        return state;
    }

    void incoming(const std::shared_ptr<Device> &device, const QByteArray &bytes)
    {
        if (bytes.isEmpty()) return;
        device->received += bytes.size();
        if (receive) receive(device->id,{{"rx",bytes}});
        describe(device);
    }

    void failed(const std::shared_ptr<Device> &device, const QString &message)
    {
        device->state["connected"] = false; device->state["last_error"] = message;
        if (device->notifier) device->notifier->setEnabled(false);
        if (device->wanted && device->config.value("auto_reconnect").toBool() && device->reconnect)
            device->reconnect->start(std::max(100,static_cast<int>(device->config.value("reconnect_interval",1.).toDouble() * 1000)));
        describe(device);
    }

    void release(const std::shared_ptr<Device> &device)
    {
        const bool wanted = device->wanted; device->wanted = false;
        if (device->reconnect) device->reconnect->stop();
        device->notifier.reset(); device->ssh.reset();
        const auto clients = std::exchange(device->clients,{});
        for (const auto &client : clients) if (client) client->abort();
        device->server.reset(); device->tcp.reset(); device->udp.reset(); device->serial.reset();
        device->loopback = false; device->state["connected"] = false;
        device->wanted = wanted;
    }

    QHostAddress address(const QString &host)
    {
        QHostAddress result;
        if (result.setAddress(host)) return result;
        const auto found = QHostInfo::fromName(host);
        if (found.error() != QHostInfo::NoError || found.addresses().isEmpty()) throw Error("主机名无法解析：" + host);
        return found.addresses().first();
    }

    void open(const std::shared_ptr<Device> &device)
    {
        release(device);
        const auto config = device->config;
        const std::weak_ptr<Device> weak = device;
        if (device->kind == "io.serial") {
            if (config.value("port") == "loop://") {
                // A real software loopback transport is available without a
                // physical serial device; it never claims hardware flow control.
                device->loopback = true; device->state["transport"] = "software_loopback";
            } else {
                device->serial = std::make_unique<QSerialPort>();
                auto port = device->serial.get(); port->setPortName(config.value("port").toString());
                const QHash<QString,QSerialPort::Parity> parity{{"N",QSerialPort::NoParity},{"E",QSerialPort::EvenParity},{"O",QSerialPort::OddParity},{"M",QSerialPort::MarkParity},{"S",QSerialPort::SpaceParity}};
                const auto stop = config.value("stopbits").toDouble();
                if (!port->setBaudRate(config.value("baudrate").toInt()) || !port->setDataBits(static_cast<QSerialPort::DataBits>(config.value("bytesize").toInt()))
                    || !port->setParity(parity.value(config.value("parity").toString(),QSerialPort::NoParity))
                    || !port->setStopBits(stop == 1. ? QSerialPort::OneStop : stop == 1.5 ? QSerialPort::OneAndHalfStop : QSerialPort::TwoStop)) throw Error("串口参数不受支持");
                const auto flow = config.value("flow_control").toString();
                port->setFlowControl(flow == "RTS/CTS" ? QSerialPort::HardwareControl : flow == "XON/XOFF" ? QSerialPort::SoftwareControl : QSerialPort::NoFlowControl);
                if (!port->open(QIODevice::ReadWrite)) throw Error("串口打开失败：" + port->errorString());
                if (flow == "DSR/DTR") {
                    DCB settings{}; settings.DCBlength = sizeof(settings);
                    const auto handle = reinterpret_cast<HANDLE>(port->handle());
                    if (!GetCommState(handle,&settings)) throw Error("无法读取串口硬件流控设置");
                    settings.fOutxDsrFlow = TRUE; settings.fDtrControl = DTR_CONTROL_HANDSHAKE;
                    if (!SetCommState(handle,&settings)) throw Error("驱动不支持 DSR/DTR 硬件流控");
                }
                if (flow != "RTS/CTS") port->setRequestToSend(config.value("rts").toBool());
                if (flow != "DSR/DTR") port->setDataTerminalReady(config.value("dtr").toBool());
                connect(port,&QSerialPort::readyRead,this,[this,weak] { if (const auto device = weak.lock()) incoming(device,device->serial->readAll()); });
                connect(port,&QSerialPort::errorOccurred,this,[this,weak](QSerialPort::SerialPortError error) {
                    if (const auto device = weak.lock(); device && (error == QSerialPort::ResourceError || error == QSerialPort::DeviceNotFoundError)) failed(device,device->serial->errorString());
                });
                device->state["transport"] = "serial";
            }
        } else if (device->kind == "io.network") {
            const auto host = config.value("host").toString(); const auto port = static_cast<quint16>(config.value("port").toUInt());
            const bool server = config.value("mode") == "server";
            if (config.value("protocol") == "tcp") {
                if (server) {
                    device->server = std::make_unique<QTcpServer>();
                    if (!device->server->listen(address(host),port)) throw Error("TCP 监听失败：" + device->server->errorString());
                    device->state["local_port"] = device->server->serverPort();
                    connect(device->server.get(),&QTcpServer::newConnection,this,[this,weak] {
                        if (const auto device = weak.lock()) {
                            while (device->server->hasPendingConnections()) {
                                auto client = device->server->nextPendingConnection();
                                if (device->clients.size() >= 128) { client->abort(); client->deleteLater(); continue; }
                                client->setReadBufferSize(8 * 1024 * 1024); device->clients.append(client);
                                connect(client,&QTcpSocket::readyRead,this,[this,weak,client] { if (const auto device = weak.lock()) incoming(device,client->readAll()); });
                                connect(client,&QTcpSocket::disconnected,this,[this,weak,client] { if (const auto device = weak.lock()) { device->clients.removeAll(client); describe(device); } client->deleteLater(); });
                            }
                            describe(device);
                        }
                    });
                } else {
                    device->tcp = std::make_unique<QTcpSocket>();
                    device->tcp->setReadBufferSize(8 * 1024 * 1024);
                    device->tcp->connectToHost(host,port);
                    if (!device->tcp->waitForConnected(5000)) throw Error("TCP 连接失败：" + device->tcp->errorString());
                    connect(device->tcp.get(),&QTcpSocket::readyRead,this,[this,weak] { if (const auto device = weak.lock()) incoming(device,device->tcp->readAll()); });
                    connect(device->tcp.get(),&QTcpSocket::disconnected,this,[this,weak] { if (const auto device = weak.lock()) failed(device,"TCP 连接已断开"); });
                }
            } else {
                device->udp = std::make_unique<QUdpSocket>();
                const auto bindPort = server ? port : static_cast<quint16>(config.value("bind_port").toUInt());
                if (!device->udp->bind(server ? address(host) : QHostAddress::AnyIPv4,bindPort)) throw Error("UDP 绑定失败：" + device->udp->errorString());
                device->state["local_port"] = device->udp->localPort();
                connect(device->udp.get(),&QUdpSocket::readyRead,this,[this,weak] {
                    if (const auto device = weak.lock()) while (device->udp->hasPendingDatagrams()) {
                        const auto packet = device->udp->receiveDatagram(); device->peer = packet.senderAddress(); device->peerPort = static_cast<quint16>(packet.senderPort());
                        device->state["last_peer"] = QVariantMap{{"address",device->peer.toString()},{"port",device->peerPort}};
                        incoming(device,packet.data());
                    }
                });
            }
        } else if (device->kind == "io.ssh") {
            device->ssh = std::make_unique<SshSession>();
            device->ssh->open(config,device->privateKey,device->columns,device->rows);
            device->notifier = std::make_unique<QSocketNotifier>(device->ssh->descriptor(),QSocketNotifier::Read);
            connect(device->notifier.get(),&QSocketNotifier::activated,this,[this,weak] {
                if (const auto device = weak.lock()) try {
                    incoming(device,device->ssh->readAvailable());
                    if (!device->ssh->connected()) failed(device,"SSH 连接已结束");
                } catch (const Error &error) { failed(device,error.message); }
            });
            // The handshake may already have buffered the first shell output
            // inside libssh2, leaving no unread socket bytes to notify Qt.
            incoming(device,device->ssh->readAvailable());
        } else throw Error("未知通信类型：" + device->kind);
        device->state["connected"] = true; device->state["last_error"] = QString{}; describe(device);
    }

    qint64 writeDevice(QIODevice *device, const QByteArray &bytes)
    {
        if (device->write(bytes) != bytes.size()) throw Error("发送失败：" + device->errorString());
        QElapsedTimer elapsed; elapsed.start();
        for (;;) {
            const auto socket = qobject_cast<QAbstractSocket *>(device); const auto serial = qobject_cast<QSerialPort *>(device);
            const auto pending = socket ? socket->bytesToWrite() : serial ? serial->bytesToWrite() : 0;
            if (pending == 0) break;
            if (elapsed.elapsed() > 5000) throw Error("发送超时：" + device->errorString());
            if (!device->waitForBytesWritten(250)) {
                if ((socket && socket->bytesToWrite() > 0 && socket->error() != QAbstractSocket::SocketTimeoutError)
                    || (serial && serial->bytesToWrite() > 0 && serial->error() != QSerialPort::TimeoutError)) throw Error("发送未完成：" + device->errorString());
            }
        }
        return bytes.size();
    }

    QVariantMap action(const QString &id, const QString &operation, const QVariantMap &args)
    {
        if (operation == "list_ports") {
            QVariantList ports;
            for (const auto &port : QSerialPortInfo::availablePorts()) ports.append(QVariantMap{{"port",port.portName()},{"description",port.description()},{"manufacturer",port.manufacturer()},{"serial_number",port.serialNumber()},{"system_location",port.systemLocation()}});
            return {{"ports",ports}};
        }
        auto device = devices.value(id);
        if (!device) { device = std::make_shared<Device>(); device->id = id; device->state = {{"connected",false},{"rx_bytes",0},{"tx_bytes",0}}; devices.insert(id,device); }
        if (operation == "connect") {
            device->wanted = false; release(device);
            device->kind = args.value("kind").toString(); device->config = args.value("config").toMap(); device->privateKey = args.value("private_key").toByteArray();
            device->wanted = true;
            if (!device->reconnect) {
                device->reconnect = std::make_unique<QTimer>(); device->reconnect->setSingleShot(true);
                const std::weak_ptr<Device> weak = device;
                connect(device->reconnect.get(),&QTimer::timeout,this,[this,weak] { if (const auto device = weak.lock(); device && device->wanted) try { open(device); } catch (const Error &error) { failed(device,error.message); } });
            }
            try { open(device); } catch (const Error &error) { failed(device,error.message); throw; }
        } else if (operation == "disconnect") { device->wanted = false; release(device); }
        else if (operation == "clear_counts") device->received = device->sent = 0;
        else if (operation == "status") return describe(device);
        else if (operation == "resize") {
            device->columns = args.value("columns",80).toInt(); device->rows = args.value("rows",24).toInt();
            if (device->columns < 2 || device->columns > 500 || device->rows < 2 || device->rows > 300) throw Error("SSH 终端尺寸超出范围");
            if (device->ssh) device->ssh->resize(device->columns,device->rows);
        } else if (operation == "rts" || operation == "dtr") {
            if (!device->serial || !device->serial->isOpen()) throw Error("串口尚未连接");
            const bool ok = operation == "rts" ? device->serial->setRequestToSend(args.value("value").toBool()) : device->serial->setDataTerminalReady(args.value("value").toBool());
            if (!ok) throw Error("控制线设置失败：" + device->serial->errorString());
        } else if (operation == "send") {
            if (!device->state.value("connected").toBool()) throw Error("设备尚未连接");
            const auto bytes = args.value("data").toByteArray(); quint64 sent = 0;
            if (device->loopback) { sent = bytes.size(); incoming(device,bytes); }
            else if (device->serial) sent = writeDevice(device->serial.get(),bytes);
            else if (device->tcp) sent = writeDevice(device->tcp.get(),bytes);
            else if (device->server) {
                const auto selected = device->config.value("target_client").toString(); int clients = 0;
                for (const auto &client : device->clients) if (client && client->state() == QAbstractSocket::ConnectedState && (selected.isEmpty() || selected == QString::number(client->socketDescriptor()))) { sent += writeDevice(client,bytes); ++clients; }
                if (!clients) throw Error("没有可发送的 TCP 客户端");
            } else if (device->udp) {
                const bool reply = device->config.value("mode") == "server" && !device->peer.isNull();
                const auto sentBytes = device->udp->writeDatagram(bytes,reply ? device->peer : address(device->config.value("host").toString()),reply ? device->peerPort : static_cast<quint16>(device->config.value("port").toUInt()));
                if (sentBytes != bytes.size()) throw Error("UDP 发送失败：" + device->udp->errorString()); sent = sentBytes;
            } else if (device->ssh) sent = device->ssh->write(bytes);
            device->sent += sent; device->state["last_sent_bytes"] = sent;
        } else throw Error("未知通信动作：" + operation);
        return describe(device);
    }

    void shutdown()
    {
        receive = {}; observe = {};
        for (const auto &device : devices) { device->wanted = false; release(device); }
        devices.clear();
    }
};

IoHub::IoHub()
{
    worker_ = new IoWorker(); worker_->moveToThread(&thread_);
    QObject::connect(&thread_,&QThread::finished,worker_,&QObject::deleteLater);
    thread_.setObjectName("device-io"); thread_.start();
}
IoHub::~IoHub() { shutdown(); }

void IoHub::setCallbacks(std::function<void(const QString &,const Values &)> receive, std::function<void(const QString &,const QVariantMap &)> observe)
{
    QMetaObject::invokeMethod(worker_,[this,receive = std::move(receive),observe = std::move(observe)] { worker_->receive = receive; worker_->observe = observe; },Qt::BlockingQueuedConnection);
}

QVariantMap IoHub::transact(const QString &node, const QString &action, const QVariantMap &arguments)
{
    if (stopping_) throw Error("通信执行器正在关闭");
    struct Outcome { QVariantMap value; QString error; };
    auto promise = std::make_shared<std::promise<Outcome>>(); auto future = promise->get_future();
    QMetaObject::invokeMethod(worker_,[this,node,action,arguments,promise] {
        try { promise->set_value({worker_->action(node,action,arguments),{}}); }
        catch (const std::exception &error) { promise->set_value({{},QString::fromUtf8(error.what())}); }
    },Qt::QueuedConnection);
    const auto result = future.get();
    if (!result.error.isEmpty()) throw Error(result.error);
    return result.value;
}

void IoHub::shutdown()
{
    if (stopping_.exchange(true)) return;
    QMetaObject::invokeMethod(worker_,[this] { worker_->shutdown(); },Qt::BlockingQueuedConnection);
    thread_.quit(); thread_.wait(); worker_ = nullptr;
}

} // namespace corig
