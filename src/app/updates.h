#pragma once

// Installation maintenance runs in the actual Qt IFW maintenance tool. Checks
// report its real repository result; installation remains an interactive flow.
#include "core/value.h"
#include <functional>

namespace corig {
QString maintenanceTool();
QVariantMap checkUpdates(const std::function<bool()> &cancelled);
}
