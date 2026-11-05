#include "mumudiscovery.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QUuid>
#include <QXmlStreamReader>

#include <windows.h>
#include <tlhelp32.h>

namespace {
bool plainLocation(const QFileInfo &info, bool directory)
{
    if (!MuMuDiscovery::isLocalPath(info.filePath())
        || (directory ? !info.isDir() : !info.isFile()) || info.isSymLink() || info.isJunction())
        return false;
    QDir parent(directory ? info.absoluteFilePath() : info.absolutePath());
    for (;;) {
        const QFileInfo entry(parent.absolutePath());
        if (entry.isSymLink() || entry.isJunction())
            return false;
        if (parent.isRoot())
            return true;
        if (!parent.cdUp())
            return false;
    }
}
}

bool MuMuDiscovery::isLocalPath(const QString &path)
{
    const auto normalized = QDir::fromNativeSeparators(path);
    if (normalized.size() < 3 || normalized[1] != QLatin1Char(':') || normalized[2] != QLatin1Char('/')
        || normalized.contains(QChar::Null))
        return false;
    const auto drive = normalized.front().toUpper().unicode();
    if (drive < u'A' || drive > u'Z')
        return false;
    const auto components = normalized.sliced(3).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const auto &component : components) {
        if (component == QStringLiteral(".") || component == QStringLiteral("..")
            || component.contains(QLatin1Char(':')) || component.endsWith(QLatin1Char('.'))
            || component.endsWith(QLatin1Char(' ')))
            return false;
    }
    auto current = QDir::toNativeSeparators(normalized.first(3));
    const auto type = GetDriveTypeW(reinterpret_cast<LPCWSTR>(current.utf16()));
    if (type != DRIVE_FIXED && type != DRIVE_REMOVABLE && type != DRIVE_RAMDISK)
        return false;
    const auto rootAttributes = GetFileAttributesW(reinterpret_cast<LPCWSTR>(current.utf16()));
    if (rootAttributes == INVALID_FILE_ATTRIBUTES || (rootAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
        return false;
    for (qsizetype index = 0; index < components.size(); ++index) {
        if (!current.endsWith(QLatin1Char('\\')))
            current.append(QLatin1Char('\\'));
        current.append(components[index]);
        const auto attributes = GetFileAttributesW(reinterpret_cast<LPCWSTR>(current.utf16()));
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)
            || (index + 1 < components.size() && !(attributes & FILE_ATTRIBUTE_DIRECTORY)))
            return false;
    }
    return true;
}

QStringList MuMuDiscovery::installationRoots()
{
    QStringList roots;
    const auto appendRoot = [&roots](const QString &path) {
        const QFileInfo info(path);
        if (!path.isEmpty() && plainLocation(info, true)) {
            const auto canonical = info.canonicalFilePath();
            if (!roots.contains(canonical, Qt::CaseInsensitive))
                roots.append(canonical);
        }
    };
    for (const auto &hive : {QStringLiteral("HKEY_LOCAL_MACHINE"), QStringLiteral("HKEY_CURRENT_USER")}) {
        for (const auto &branch : {QStringLiteral("SOFTWARE/Microsoft/Windows/CurrentVersion/Uninstall"),
                                   QStringLiteral("SOFTWARE/WOW6432Node/Microsoft/Windows/CurrentVersion/Uninstall")}) {
            QSettings settings(hive + QLatin1Char('/') + branch, QSettings::NativeFormat);
            for (const auto &group : settings.childGroups()) {
                settings.beginGroup(group);
                if (settings.value(QStringLiteral("DisplayName")).toString().contains(QStringLiteral("MuMu"), Qt::CaseInsensitive))
                    appendRoot(settings.value(QStringLiteral("InstallLocation")).toString());
                settings.endGroup();
            }
        }
    }
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        if (Process32FirstW(snapshot, &entry)) {
            do {
                const auto name = QString::fromWCharArray(entry.szExeFile);
                if (name.compare(QStringLiteral("MuMuNxMain.exe"), Qt::CaseInsensitive) != 0
                    && name.compare(QStringLiteral("MuMuNxDevice.exe"), Qt::CaseInsensitive) != 0)
                    continue;
                const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
                if (!process)
                    continue;
                wchar_t path[32768];
                DWORD length = 32768;
                if (QueryFullProcessImageNameW(process, 0, path, &length)) {
                    const auto processPath = QString::fromWCharArray(path, static_cast<int>(length));
                    if (isLocalPath(processPath)) {
                        QDir root(QFileInfo(processPath).absolutePath());
                        const int levels = name.compare(QStringLiteral("MuMuNxMain.exe"), Qt::CaseInsensitive) == 0 ? 1 : 3;
                        bool valid = true;
                        for (int index = 0; index < levels; ++index)
                            valid = root.cdUp() && valid;
                        if (valid)
                            appendRoot(root.absolutePath());
                    }
                }
                CloseHandle(process);
            } while (Process32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);
    }
    return roots;
}

QVector<MuMuInstance> MuMuDiscovery::scanInstallations(const QStringList &roots)
{
    QVector<MuMuInstance> result;
    QSet<QString> seen;
    static const QRegularExpression versionPattern(QStringLiteral("\\A[0-9]+(?:\\.[0-9]+){1,2}\\z"));
    static const QRegularExpression instancePattern(QStringLiteral("\\AMuMuPlayer-([0-9]+(?:\\.[0-9]+){1,2})-[0-9]+\\z"));
    for (const auto &path : roots) {
        const QFileInfo rootInfo(path);
        if (!plainLocation(rootInfo, true))
            continue;
        const QDir root(rootInfo.canonicalFilePath());
        const QDir devices(root.filePath(QStringLiteral("nx_device")));
        const QDir machines(root.filePath(QStringLiteral("vms")));
        if (!plainLocation(QFileInfo(devices.absolutePath()), true)
            || !plainLocation(QFileInfo(machines.absolutePath()), true))
            continue;
        QMap<QString, QString> tools;
        int versionCount = 0;
        for (const auto &version : devices.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            if (++versionCount > 64)
                break;
            const QFileInfo tool(QDir(version.absoluteFilePath()).filePath(QStringLiteral("hypervisor/VBoxManage.exe")));
            if (versionPattern.match(version.fileName()).hasMatch() && plainLocation(version, true) && plainLocation(tool, false))
                tools.insert(version.fileName(), tool.canonicalFilePath());
        }
        int machineCount = 0;
        for (const auto &folder : machines.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            if (++machineCount > 256 || result.size() >= 256)
                break;
            const auto match = instancePattern.match(folder.fileName());
            if (!match.hasMatch() || !plainLocation(folder, true))
                continue;
            const auto version = match.captured(1);
            const QFileInfo config(QDir(folder.absoluteFilePath()).filePath(folder.fileName() + QStringLiteral(".nemu")));
            if (!tools.contains(version) || !plainLocation(config, false) || config.size() > 1024 * 1024)
                continue;
            const auto configPath = config.canonicalFilePath();
            const auto key = configPath.toCaseFolded();
            if (seen.contains(key))
                continue;
            QFile file(configPath);
            if (!file.open(QIODevice::ReadOnly))
                continue;
            const auto bytes = file.read(1024 * 1024 + 1);
            if (file.error() != QFileDevice::NoError || bytes.size() > 1024 * 1024)
                continue;
            QXmlStreamReader xml(bytes);
            QString name;
            QUuid uuid;
            int count = 0;
            while (!xml.atEnd()) {
                xml.readNext();
                if (xml.tokenType() == QXmlStreamReader::DTD) {
                    xml.raiseError(QStringLiteral("不接受带 DTD 的配置"));
                    break;
                }
                if (xml.isStartElement() && xml.name() == QLatin1String("Machine")) {
                    ++count;
                    uuid = QUuid(xml.attributes().value(QStringLiteral("uuid")).toString());
                    name = xml.attributes().value(QStringLiteral("name")).toString();
                }
            }
            if (xml.hasError() || count != 1 || uuid.isNull() || (!name.isEmpty() && name != folder.fileName()))
                continue;
            seen.insert(key);
            result.append({name.isEmpty() ? folder.fileName() : name, version, tools.value(version), configPath});
        }
    }
    return result;
}
