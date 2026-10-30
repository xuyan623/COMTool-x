#include "paths.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QStandardPaths>

namespace corig {
QString defaultDataDirectory()
{
    const auto directory = QCoreApplication::applicationDirPath();
    if (QFileInfo::exists(directory + "/portable.ini") || QFileInfo::exists(directory + "/settings.ini")) return directory;
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
}
}
