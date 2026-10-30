#pragma once

#include "value.h"

namespace corig {
// Portable builds keep their existing data directory. Installed builds use a
// writable per-user location independently of the program's install scope.
CORIG_API QString defaultDataDirectory();
}
