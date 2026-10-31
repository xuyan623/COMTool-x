#pragma once

// GUI and headless commands evaluate pure hooks against the same checked
// package resources. Hooks receive data, never live editor/engine objects.
#include "plugin_package.h"
namespace corig {
CORIG_API QVariant pluginScript(const PluginPackage &package, const QString &file,
                               const QString &entry, const QVariantList &arguments);
}
