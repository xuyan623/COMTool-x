#include "updates.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QXmlStreamReader>

namespace corig {
QString maintenanceTool()
{
    const auto path = QCoreApplication::applicationDirPath() + "/CoRigMaintenanceTool.exe";
    return QFileInfo(path).isFile() ? path : QString{};
}

QVariantMap checkUpdates(const std::function<bool()> &cancelled)
{
    const auto tool = maintenanceTool();
    if (tool.isEmpty()) throw Error("请使用安装版 CoRig 进行安装式更新");
    QProcess process; process.setProgram(tool);
    process.setArguments({"--cache-path", QCoreApplication::applicationDirPath() + "/cache/installer", "check-updates"});
    process.start();
    if (!process.waitForStarted(5000)) throw Error("无法启动更新检查：" + process.errorString());
    QElapsedTimer deadline; deadline.start();
    while (!process.waitForFinished(100)) {
        if (cancelled() || deadline.elapsed() > 60000) {
            process.kill(); process.waitForFinished(3000);
            throw Error(cancelled() ? "更新检查已取消" : "更新检查超时，请检查网络与更新源");
        }
    }
    const auto output = process.readAllStandardOutput(); const auto errors = process.readAllStandardError();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        throw Error("更新检查失败：" + QString::fromUtf8(errors.isEmpty() ? output.right(2000) : errors.right(2000)).trimmed());
    QVariantList updates;
    // IFW prints an <updates> document for check-updates; preceding log lines
    // are permitted, while a malformed nonempty update document is rejected.
    const auto start = output.indexOf("<updates");
    if (start >= 0) {
        QXmlStreamReader xml(output.mid(start));
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement() && xml.name() == u"update") {
                QVariantMap entry;
                for (const auto &attribute : xml.attributes()) entry.insert(attribute.name().toString(),attribute.value().toString());
                updates.append(entry);
            }
        }
        if (xml.hasError()) throw Error("更新检查返回的内容无法解析");
    }
    return {{"available",!updates.isEmpty()},{"updates",updates},{"checked_at",QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}};
}
}
