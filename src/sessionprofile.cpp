#include "sessionprofile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

#include <windows.h>
#include <wincrypt.h>

#include <algorithm>

namespace {
constexpr qint64 MaxProfileSize = 64 * 1024;

bool safeLocation(const QString &path)
{
    const QFileInfo file(path);
    if (file.isSymLink() || file.isJunction() || (file.exists() && !file.isFile()))
        return false;
    QDir directory(file.absolutePath());
    for (;;) {
        const QFileInfo info(directory.absolutePath());
        if ((info.exists() && !info.isDir()) || info.isSymLink() || info.isJunction())
            return false;
        if (directory.isRoot())
            return true;
        directory.setPath(info.absolutePath());
    }
}

bool validText(const QString &text, qsizetype maximum)
{
    return !text.isEmpty() && text.size() <= maximum
        && std::none_of(text.cbegin(), text.cend(), [](QChar ch) {
            return ch.isNull() || ch.isSpace() || ch.category() == QChar::Other_Control;
        });
}

bool validHeader(const QByteArray &value)
{
    return !value.isEmpty() && value.size() <= 2048
        && std::none_of(value.cbegin(), value.cend(), [](unsigned char ch) { return ch < 0x20 || ch > 0x7e; });
}
}

bool SessionProfile::hasSession() const
{
    return !token.isEmpty() && (!expiresAt.isValid() || expiresAt > QDateTime::currentDateTimeUtc());
}

QString SessionProfile::rawDeviceId() const
{
    return deviceId.endsWith(QLatin1Char('P')) ? deviceId.chopped(1) : deviceId;
}

QString SessionProfile::requestDeviceId() const
{
    return rawDeviceId() + QLatin1Char('P');
}

void SessionProfile::clearCredentials()
{
    token.fill(QChar::Null);
    token.clear();
    expiresAt = {};
    accountSession = false;
}

QString SessionStore::defaultPath()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .filePath(QStringLiteral("session-4.0.9.dat"));
}

QString SessionStore::validate(const SessionProfile &profile)
{
    if (!validText(profile.deviceId, 256)
        || (!profile.token.isEmpty() && !validText(profile.token, 1024)))
        return QStringLiteral("设备身份或会话格式无效，未使用。");
    if (profile.headers.value("version") != "4.0.9" || profile.headers.value("devicetype") != "android"
        || !validHeader(profile.headers.value("user-agent")))
        return QStringLiteral("会话的 App 版本或必要请求头无效。");
    for (auto it = profile.headers.cbegin(); it != profile.headers.cend(); ++it) {
        if ((it.key() != "version" && it.key() != "devicetype" && it.key() != "user-agent" && it.key() != "time")
            || !validHeader(it.value()))
            return QStringLiteral("会话包含不支持或不安全的请求头。");
    }
    if (profile.token.isEmpty() && (profile.accountSession || profile.expiresAt.isValid()))
        return QStringLiteral("会话身份状态不一致。");
    return {};
}

QString SessionStore::load(const QString &path, SessionProfile &profile)
{
    if (!safeLocation(path))
        return QStringLiteral("会话保存位置不安全，未读取。");
    if (!QFileInfo::exists(path))
        return {};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > MaxProfileSize)
        return QStringLiteral("无法读取受保护会话，或文件超过 64 KiB。");
    auto encrypted = file.read(MaxProfileSize + 1);
    if (file.error() != QFileDevice::NoError || encrypted.size() > MaxProfileSize)
        return QStringLiteral("读取受保护会话失败。");
    DATA_BLOB input{static_cast<DWORD>(encrypted.size()), reinterpret_cast<BYTE *>(encrypted.data())};
    DATA_BLOB output{};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output))
        return QStringLiteral("无法解密会话，请使用保存它的 Windows 用户重新登录或导入。");
    if (output.cbData > MaxProfileSize) {
        SecureZeroMemory(output.pbData, output.cbData);
        LocalFree(output.pbData);
        return QStringLiteral("受保护会话解密后超过大小限制。");
    }
    QByteArray payload(reinterpret_cast<const char *>(output.pbData), output.cbData);
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(payload, &error);
    payload.fill('\0');
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return QStringLiteral("受保护会话格式已损坏。");
    const auto object = document.object();
    if (object.size() != 7 || object.value(QStringLiteral("schema")) != QJsonValue(1)
        || object.value(QStringLiteral("app_version")).toString() != QStringLiteral("4.0.9")
        || !object.value(QStringLiteral("device_id")).isString() || !object.value(QStringLiteral("token")).isString()
        || !object.value(QStringLiteral("account_session")).isBool() || !object.value(QStringLiteral("headers")).isObject()
        || !object.value(QStringLiteral("expires_at")).isString())
        return QStringLiteral("受保护会话版本或字段不受支持。");
    SessionProfile loaded;
    loaded.deviceId = object.value(QStringLiteral("device_id")).toString();
    loaded.token = object.value(QStringLiteral("token")).toString();
    loaded.accountSession = object.value(QStringLiteral("account_session")).toBool();
    const auto expiry = object.value(QStringLiteral("expires_at")).toString();
    loaded.expiresAt = QDateTime::fromString(expiry, Qt::ISODateWithMs);
    if (!expiry.isEmpty() && !loaded.expiresAt.isValid())
        return QStringLiteral("受保护会话的过期时间无效。");
    const auto headers = object.value(QStringLiteral("headers")).toObject();
    for (auto it = headers.constBegin(); it != headers.constEnd(); ++it) {
        if (!it.value().isString() || QString::fromLatin1(it.key().toLatin1()) != it.key()
            || QString::fromLatin1(it.value().toString().toLatin1()) != it.value().toString())
            return QStringLiteral("受保护会话的请求头格式无效。");
        loaded.headers.insert(it.key().toLatin1(), it.value().toString().toLatin1());
    }
    const auto invalid = validate(loaded);
    if (!invalid.isEmpty())
        return invalid;
    profile.clearCredentials();
    profile = std::move(loaded);
    return {};
}

QString SessionStore::save(const QString &path, const SessionProfile &profile)
{
    const auto invalid = validate(profile);
    if (!invalid.isEmpty())
        return invalid;
    QJsonObject headers;
    for (auto it = profile.headers.cbegin(); it != profile.headers.cend(); ++it)
        headers.insert(QString::fromLatin1(it.key()), QString::fromLatin1(it.value()));
    auto payload = QJsonDocument(QJsonObject{
        {QStringLiteral("schema"), 1}, {QStringLiteral("app_version"), QStringLiteral("4.0.9")},
        {QStringLiteral("device_id"), profile.deviceId}, {QStringLiteral("token"), profile.token},
        {QStringLiteral("account_session"), profile.accountSession}, {QStringLiteral("headers"), headers},
        {QStringLiteral("expires_at"), profile.expiresAt.isValid()
             ? profile.expiresAt.toUTC().toString(Qt::ISODateWithMs) : QString()}}).toJson(QJsonDocument::Compact);
    DATA_BLOB input{static_cast<DWORD>(payload.size()), reinterpret_cast<BYTE *>(payload.data())};
    DATA_BLOB output{};
    const bool protectedOk = CryptProtectData(&input, L"NovDecoder session 4.0.9", nullptr, nullptr, nullptr,
                                               CRYPTPROTECT_UI_FORBIDDEN, &output);
    payload.fill('\0');
    if (!protectedOk)
        return QStringLiteral("无法加密保存会话；当前内存会话仍可使用。");
    const QByteArray encrypted(reinterpret_cast<const char *>(output.pbData), output.cbData);
    LocalFree(output.pbData);
    const QFileInfo info(path);
    if (encrypted.size() > MaxProfileSize || !safeLocation(path)
        || !QDir().mkpath(info.absolutePath()) || !safeLocation(path))
        return QStringLiteral("无法创建安全的会话保存位置；当前内存会话仍保留。");
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(encrypted) != encrypted.size() || !file.commit())
        return QStringLiteral("保存受保护会话失败；当前内存会话仍保留。");
    return {};
}
