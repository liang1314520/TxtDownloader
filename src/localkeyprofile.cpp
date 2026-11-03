#include "localkeyprofile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include <windows.h>
#include <wincrypt.h>

#include <algorithm>

namespace LocalKeyProfile {

QString load(const QString &version, QByteArray &apiKey, QByteArray &chapterKey)
{
    apiKey.fill('\0');
    apiKey.clear();
    chapterKey.fill('\0');
    chapterKey.clear();
    if (version != QStringLiteral("4.0.9"))
        return QStringLiteral("当前仅配置了 App 4.0.9 的自动密钥，此请求版本不受支持。");

    const auto directory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (directory.isEmpty())
        return QStringLiteral("无法确定本机密钥配置目录。");
    const auto path = QDir(directory).filePath(QStringLiteral("keys-4.0.9.dat"));
    const QFileInfo info(path);
    if (!info.isFile() || info.isSymLink() || info.isJunction())
        return QStringLiteral("本机缺少 App 4.0.9 自动密钥配置，请先配置当前 Windows 用户的密钥文件。");

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > 64 * 1024)
        return QStringLiteral("无法读取本机自动密钥配置，或配置大小不正确。");
    auto encrypted = file.read(64 * 1024 + 1);
    if (file.error() != QFileDevice::NoError || encrypted.size() > 64 * 1024)
        return QStringLiteral("本机自动密钥配置读取失败。");

    DATA_BLOB input{static_cast<DWORD>(encrypted.size()), reinterpret_cast<BYTE *>(encrypted.data())};
    DATA_BLOB output{};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output))
        return QStringLiteral("无法解密本机密钥配置；该配置只能由创建它的 Windows 用户使用。");
    QByteArray payload(reinterpret_cast<const char *>(output.pbData), output.cbData);
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);

    const QByteArray prefix("NovDecoder-4.0.9\n");
    const bool valid = payload.size() == prefix.size() + 32 && payload.startsWith(prefix)
        && std::all_of(payload.cbegin() + prefix.size(), payload.cend(), [](unsigned char ch) {
            return ch >= 0x20 && ch <= 0x7e;
        });
    if (valid) {
        apiKey = payload.sliced(prefix.size(), 16);
        chapterKey = payload.sliced(prefix.size() + 16, 16);
    }
    payload.fill('\0');
    if (!valid)
        return QStringLiteral("本机自动密钥配置格式或版本不正确。");
    return {};
}

}
