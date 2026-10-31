#pragma once

// Application-wide budgets also govern standalone SDK readers. Values are
// snapshots under a read/write lock; plugins declare needs, users set budgets.
#include "value.h"

namespace corig {
CORIG_API QVariantMap resourceLimits();
CORIG_API QVariantMap configureResourceLimits(const QVariantMap &changes, bool reset = false);
CORIG_API qint64 resourceLimit(const QString &name);
CORIG_API void checkResourceRequirements(const QVariantMap &requirements, const QString &owner);
}
