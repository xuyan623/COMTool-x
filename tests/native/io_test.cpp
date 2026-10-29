// These tests use actual sockets and the production software loopback. Physical
// serial drivers and remote SSH authentication need separate hardware checks.
#include <QtTest>
#include <QMutex>
#include <QMutexLocker>
#include "plugins/io_hub.h"

class IoTest : public QObject {
    Q_OBJECT
private slots:
    void softwareLoopback()
    {
        cx::IoHub hub; QByteArray received; QMutex guard;
        hub.setCallbacks([&](const QString &, const cx::Values &values) { QMutexLocker lock(&guard); received += values.value("rx").toByteArray(); },{});
        cx::Catalog catalog;
        const auto config = catalog.checkedConfig("io.serial",{{"port","loop://"}});
        QVERIFY(hub.transact("serial","connect",{{"kind","io.serial"},{"config",config}}).value("connected").toBool());
        const QByteArray bytes("\0abc\xff\r\n",7);
        const auto state = hub.transact("serial","send",{{"data",bytes}});
        QCOMPARE(state.value("tx_bytes").toULongLong(),7u); QCOMPARE(state.value("rx_bytes").toULongLong(),7u);
        { QMutexLocker lock(&guard); QCOMPARE(received,bytes); }
        QVERIFY(!hub.transact("serial","disconnect",{}).value("connected").toBool());
        QVERIFY_THROWS_EXCEPTION(cx::Error,hub.transact("serial","send",{{"data",bytes}}));
    }

    void tcpAndUdp()
    {
        for (const auto &protocol : {QString("tcp"),QString("udp")}) {
            cx::IoHub hub; QMutex guard; QHash<QString,QByteArray> received;
            hub.setCallbacks([&](const QString &id, const cx::Values &values) { QMutexLocker lock(&guard); received[id] += values.value("rx").toByteArray(); },{});
            cx::Catalog catalog;
            auto server = catalog.checkedConfig("io.network",{{"protocol",protocol},{"mode","server"},{"host","127.0.0.1"}});
            // Let the OS choose a free test port; user-facing configuration
            // still requires an explicit destination/listen port.
            server["port"] = 0;
            const auto bound = hub.transact("server","connect",{{"kind","io.network"},{"config",server}});
            const auto port = bound.value("local_port").toInt(); QVERIFY(port > 0);
            const auto client = catalog.checkedConfig("io.network",{{"protocol",protocol},{"mode","client"},{"host","127.0.0.1"},{"port",port}});
            QVERIFY(hub.transact("client","connect",{{"kind","io.network"},{"config",client}}).value("connected").toBool());
            const QByteArray payload("真实收发\0\xff",14);
            hub.transact("client","send",{{"data",payload}});
            QTRY_VERIFY_WITH_TIMEOUT(([&] { QMutexLocker lock(&guard); return received.value("server") == payload; })(),2000);
            hub.transact("server","send",{{"data",QByteArray("reply")}});
            QTRY_VERIFY_WITH_TIMEOUT(([&] { QMutexLocker lock(&guard); return received.value("client") == "reply"; })(),2000);
            hub.shutdown();
        }
    }

    void sshFailureIsReported()
    {
        cx::IoHub hub; cx::Catalog catalog;
        auto config = catalog.checkedConfig("io.ssh",{{"host","127.0.0.1"},{"port",1}});
        QVERIFY_THROWS_EXCEPTION(cx::Error,hub.transact("ssh","connect",{{"kind","io.ssh"},{"config",config}}));
        const auto state = hub.transact("ssh","status",{}); QVERIFY(!state.value("connected").toBool()); QVERIFY(!state.value("last_error").toString().isEmpty());
    }
};
QTEST_GUILESS_MAIN(IoTest)
#include "io_test.moc"
