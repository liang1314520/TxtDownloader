#pragma once

#include <QDateTime>
#include <QMap>
#include <QString>

struct SessionProfile {
    QString deviceId;
    QString token;
    QMap<QByteArray, QByteArray> headers;
    QDateTime expiresAt;
    bool accountSession = false;

    bool hasSession() const;
    QString rawDeviceId() const;
    QString requestDeviceId() const;
    void clearCredentials();
};

namespace SessionStore {
QString defaultPath();
QString validate(const SessionProfile &profile);
QString load(const QString &path, SessionProfile &profile);
QString save(const QString &path, const SessionProfile &profile);
}
