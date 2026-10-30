#pragma once

// The SDK DLL owns runtime types; plugins import the same definitions instead
// of statically duplicating nodes, exceptions or Qt meta-objects.
#include <QtCore/qglobal.h>
#if defined(CORIG_CORE_BUILD)
#define CORIG_API Q_DECL_EXPORT
#else
#define CORIG_API Q_DECL_IMPORT
#endif
