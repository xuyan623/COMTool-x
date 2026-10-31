#include "resource_policy.h"
#include <QReadWriteLock>
#include <cmath>

namespace corig {
namespace {
const QVariantMap Defaults{{"archive_mib",128},{"expanded_mib",512},{"package_files",4096},
    {"ports",128},{"python_message_mib",16},{"python_timeout_ms",1000},
    {"callback_timeout_ms",1000},{"event_queue_mib",32},{"event_queue_count",65536},{"resource_mib",128}};
QReadWriteLock policyLock;
QVariantMap policy = Defaults;
}

QVariantMap resourceLimits()
{
    QReadLocker guard(&policyLock);
    return policy;
}

QVariantMap configureResourceLimits(const QVariantMap &changes, bool reset)
{
    QWriteLocker guard(&policyLock);
    auto candidate = reset ? Defaults : policy;
    for (auto i = changes.cbegin(); i != changes.cend(); ++i) {
        if (!Defaults.contains(i.key())) throw Error("未知资源额度：" + i.key());
        const auto number = finiteNumber(i.value(),i.key());
        // Keep arithmetic representable in Qt's containers and millisecond
        // timers even when the user raises a normal development budget.
        const double maximum = i.key().endsWith("_mib") ? 65536. : 1000000000.;
        if (number < 1. || number > maximum || number != std::floor(number)) throw Error(i.key()+" 需要有效的正整数额度");
        candidate[i.key()] = static_cast<qint64>(number);
    }
    policy = candidate;
    return policy;
}

qint64 resourceLimit(const QString &name)
{
    QReadLocker guard(&policyLock);
    if (!policy.contains(name)) throw Error("未知资源额度：" + name);
    return policy.value(name).toLongLong();
}

void checkResourceRequirements(const QVariantMap &requirements, const QString &owner)
{
    const auto current = resourceLimits();
    for (auto i = requirements.cbegin(); i != requirements.cend(); ++i) {
        if (!current.contains(i.key())) throw Error(owner+" 声明了未知资源需求："+i.key());
        const auto needed = finiteNumber(i.value(),i.key());
        if (needed < 1 || needed != std::floor(needed)) throw Error(owner+" 的资源需求需要正整数："+i.key());
        if (needed > current.value(i.key()).toLongLong())
            throw Error(QString("%1 需要 %2=%3，当前额度为 %4；请在资源设置中调整")
                .arg(owner,i.key(),QString::number(needed),current.value(i.key()).toString()));
    }
}
}
