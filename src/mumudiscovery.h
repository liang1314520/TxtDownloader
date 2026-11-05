#pragma once

#include <QStringList>
#include <QVector>

struct MuMuInstance {
    QString name;
    QString version;
    QString toolPath;
    QString configPath;
};

namespace MuMuDiscovery {
bool isLocalPath(const QString &path);
QStringList installationRoots();
QVector<MuMuInstance> scanInstallations(const QStringList &roots);
}
