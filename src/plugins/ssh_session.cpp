#include <winsock2.h>
#include <ws2tcpip.h>
#include "ssh_session.h"
#include <libssh2.h>
#include <QCryptographicHash>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

namespace cx {

struct SshSession::State {
    SOCKET socket = INVALID_SOCKET;
    LIBSSH2_SESSION *session = nullptr;
    LIBSSH2_CHANNEL *channel = nullptr;
    QString fingerprint;
    bool active = false;

    QString error(const QString &context) const
    {
        char *message = nullptr; int length = 0;
        if (session) libssh2_session_last_error(session,&message,&length,0);
        return context + (message ? ": " + QString::fromUtf8(message,length) : ": " + QString::number(WSAGetLastError()));
    }

    void wait(QDeadlineTimer &deadline) const
    {
        if (deadline.hasExpired()) throw Error("SSH 操作超时");
        fd_set reading, writing; FD_ZERO(&reading); FD_ZERO(&writing);
        const auto direction = session ? libssh2_session_block_directions(session) : LIBSSH2_SESSION_BLOCK_INBOUND | LIBSSH2_SESSION_BLOCK_OUTBOUND;
        if (direction & LIBSSH2_SESSION_BLOCK_INBOUND) FD_SET(socket,&reading);
        if (direction & LIBSSH2_SESSION_BLOCK_OUTBOUND) FD_SET(socket,&writing);
        timeval timeout{0,50000};
        if (::select(0,&reading,&writing,nullptr,&timeout) == SOCKET_ERROR) throw Error(error("SSH 等待失败"));
    }

    template<class Operation> void complete(Operation operation, const QString &context)
    {
        QDeadlineTimer deadline(5000);
        int result;
        while ((result = operation()) == LIBSSH2_ERROR_EAGAIN) wait(deadline);
        if (result < 0) throw Error(error(context));
    }
};

SshSession::SshSession() : state_(std::make_unique<State>())
{
    static const bool initialized = [] { WSADATA data; return WSAStartup(MAKEWORD(2,2),&data) == 0 && libssh2_init(0) == 0; }();
    if (!initialized) throw Error("SSH 网络库初始化失败");
}
SshSession::~SshSession() { close(); }

void SshSession::open(const QVariantMap &config, const QByteArray &privateKey, int columns, int rows)
{
    close();
    const auto host = config.value("host").toString().toUtf8(), port = QByteArray::number(config.value("port").toInt());
    addrinfo hints{}; hints.ai_socktype = SOCK_STREAM; hints.ai_family = AF_UNSPEC;
    addrinfo *addresses = nullptr;
    const auto resolved = getaddrinfo(host.constData(),port.constData(),&hints,&addresses);
    if (resolved || !addresses) throw Error("SSH 主机名无法解析");
    std::unique_ptr<addrinfo,decltype(&freeaddrinfo)> cleanup(addresses,freeaddrinfo);
    try {
        for (auto address = addresses; address; address = address->ai_next) {
            state_->socket = ::socket(address->ai_family,address->ai_socktype,address->ai_protocol);
            if (state_->socket == INVALID_SOCKET) continue;
            u_long nonblocking = 1; ioctlsocket(state_->socket,FIONBIO,&nonblocking);
            const auto connected = ::connect(state_->socket,address->ai_addr,static_cast<int>(address->ai_addrlen));
            if (connected == 0) break;
            if (WSAGetLastError() == WSAEWOULDBLOCK) {
                fd_set writing; FD_ZERO(&writing); FD_SET(state_->socket,&writing); timeval timeout{5,0};
                if (::select(0,nullptr,&writing,nullptr,&timeout) > 0) {
                    int error = 0, length = sizeof(error); getsockopt(state_->socket,SOL_SOCKET,SO_ERROR,reinterpret_cast<char *>(&error),&length);
                    if (error == 0) break;
                }
            }
            closesocket(state_->socket); state_->socket = INVALID_SOCKET;
        }
        if (state_->socket == INVALID_SOCKET) throw Error("SSH TCP 连接失败");
        state_->session = libssh2_session_init();
        if (!state_->session) throw Error("SSH 会话创建失败");
        libssh2_session_set_blocking(state_->session,0);
        state_->complete([&] { return libssh2_session_handshake(state_->session,state_->socket); },"SSH 握手失败");
        size_t keyLength = 0; int keyType = 0;
        const char *key = libssh2_session_hostkey(state_->session,&keyLength,&keyType);
        if (!key) throw Error("SSH 主机没有提供有效密钥");
        state_->fingerprint = "SHA256:" + QString::fromLatin1(QCryptographicHash::hash(QByteArray(key,static_cast<qsizetype>(keyLength)),QCryptographicHash::Sha256).toBase64(QByteArray::OmitTrailingEquals));
        auto known = libssh2_knownhost_init(state_->session);
        if (!known) throw Error("SSH 主机密钥检查初始化失败");
        const auto knownPath = QDir::home().filePath(".ssh/known_hosts").toUtf8();
        libssh2_knownhost_readfile(known,knownPath.constData(),LIBSSH2_KNOWNHOST_FILE_OPENSSH);
        int keyFlag = 0;
        if (keyType == LIBSSH2_HOSTKEY_TYPE_RSA) keyFlag = LIBSSH2_KNOWNHOST_KEY_SSHRSA;
        else if (keyType == LIBSSH2_HOSTKEY_TYPE_DSS) keyFlag = LIBSSH2_KNOWNHOST_KEY_SSHDSS;
        else if (keyType == LIBSSH2_HOSTKEY_TYPE_ECDSA_256) keyFlag = LIBSSH2_KNOWNHOST_KEY_ECDSA_256;
        else if (keyType == LIBSSH2_HOSTKEY_TYPE_ECDSA_384) keyFlag = LIBSSH2_KNOWNHOST_KEY_ECDSA_384;
        else if (keyType == LIBSSH2_HOSTKEY_TYPE_ECDSA_521) keyFlag = LIBSSH2_KNOWNHOST_KEY_ECDSA_521;
        else if (keyType == LIBSSH2_HOSTKEY_TYPE_ED25519) keyFlag = LIBSSH2_KNOWNHOST_KEY_ED25519;
        libssh2_knownhost *match = nullptr;
        const auto check = libssh2_knownhost_checkp(known,host.constData(),config.value("port").toInt(),key,keyLength,LIBSSH2_KNOWNHOST_TYPE_PLAIN | LIBSSH2_KNOWNHOST_KEYENC_RAW | keyFlag,&match);
        libssh2_knownhost_free(known);
        if (check == LIBSSH2_KNOWNHOST_CHECK_MISMATCH) throw Error("SSH 主机密钥与 known_hosts 不一致：" + state_->fingerprint);
        if (check != LIBSSH2_KNOWNHOST_CHECK_MATCH && !config.value("allow_unknown_host").toBool()) throw Error("SSH 主机尚未信任，请核对指纹并设置允许未知主机：" + state_->fingerprint);
        const auto user = config.value("user").toString().toUtf8(), password = config.value("password").toString().toUtf8();
        if (!privateKey.isEmpty()) state_->complete([&] { return libssh2_userauth_publickey_frommemory(state_->session,user.constData(),user.size(),nullptr,0,privateKey.constData(),privateKey.size(),password.constData()); },"SSH 密钥认证失败");
        else state_->complete([&] { return libssh2_userauth_password_ex(state_->session,user.constData(),static_cast<unsigned int>(user.size()),password.constData(),static_cast<unsigned int>(password.size()),nullptr); },"SSH 密码认证失败");
        QDeadlineTimer deadline(5000);
        while (!(state_->channel = libssh2_channel_open_session(state_->session))) {
            if (libssh2_session_last_errno(state_->session) != LIBSSH2_ERROR_EAGAIN) throw Error(state_->error("SSH 通道打开失败"));
            state_->wait(deadline);
        }
        state_->complete([&] { return libssh2_channel_request_pty_ex(state_->channel,"xterm-256color",14,nullptr,0,columns,rows,0,0); },"SSH PTY 创建失败");
        state_->complete([&] { return libssh2_channel_shell(state_->channel); },"SSH shell 启动失败");
        state_->active = true;
    } catch (...) { close(); throw; }
}

void SshSession::close()
{
    state_->active = false;
    if (state_->session) { libssh2_session_set_blocking(state_->session,1); libssh2_session_set_timeout(state_->session,200); }
    if (state_->channel) { libssh2_channel_close(state_->channel); libssh2_channel_free(state_->channel); state_->channel = nullptr; }
    if (state_->session) { libssh2_session_disconnect(state_->session,"COMTool-x session closed"); libssh2_session_free(state_->session); state_->session = nullptr; }
    if (state_->socket != INVALID_SOCKET) { closesocket(state_->socket); state_->socket = INVALID_SOCKET; }
}

qsizetype SshSession::write(const QByteArray &bytes)
{
    if (!state_->active) throw Error("SSH 尚未连接");
    QDeadlineTimer deadline(5000); qsizetype sent = 0;
    while (sent < bytes.size()) {
        const auto count = libssh2_channel_write(state_->channel,bytes.constData() + sent,static_cast<size_t>(bytes.size() - sent));
        if (count == LIBSSH2_ERROR_EAGAIN) { state_->wait(deadline); continue; }
        if (count < 0) throw Error(state_->error("SSH 发送失败"));
        if (count == 0) { state_->wait(deadline); continue; }
        sent += count;
    }
    return sent;
}

QByteArray SshSession::readAvailable()
{
    QByteArray result; char buffer[16384];
    if (!state_->active) return result;
    for (int stream = 0; stream < 2; ++stream) for (;;) {
        const auto count = libssh2_channel_read_ex(state_->channel,stream,buffer,sizeof(buffer));
        if (count == LIBSSH2_ERROR_EAGAIN || count == 0) break;
        if (count < 0) throw Error(state_->error("SSH 接收失败"));
        result.append(buffer,count);
        if (result.size() >= 1024 * 1024) break;
    }
    if (libssh2_channel_eof(state_->channel)) state_->active = false;
    return result;
}

void SshSession::resize(int columns, int rows)
{
    if (state_->active) state_->complete([&] { return libssh2_channel_request_pty_size(state_->channel,columns,rows); },"SSH 终端尺寸更新失败");
}
bool SshSession::connected() const { return state_->active; }
qintptr SshSession::descriptor() const { return static_cast<qintptr>(state_->socket); }
QString SshSession::fingerprint() const { return state_->fingerprint; }

} // namespace cx
