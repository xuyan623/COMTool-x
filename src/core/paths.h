#pragma once

#include "value.h"

namespace corig {
// Both portable and installed builds keep program files, plugins and user
// projects in one directory. The installer owns program files only.
CORIG_API QString defaultDataDirectory();
// Called before constructing the GUI application. Internal caches and child
// process temporary files stay beside the executable; failure never falls back.
CORIG_API void configureRuntimePaths();
// Creates project, plugins and cache beside the executable and explains the
// project folder once. Repeated calls are harmless.
CORIG_API void ensureDataDirectory(const QString &directory);
} // namespace corig
