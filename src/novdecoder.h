#pragma once

#include <QByteArray>
#include <QString>

namespace NovDecoder {

inline constexpr qsizetype MaxFileSize = 32 * 1024 * 1024;

struct DecodeResult {
    QByteArray utf8;
    QString error;
    qsizetype characters = 0;

    bool ok() const { return error.isEmpty(); }
};

struct SaveResult {
    QString error;
    qint64 bytesWritten = 0;
    qsizetype characters = 0;

    bool ok() const { return error.isEmpty(); }
};

DecodeResult decode(const QByteArray &encrypted, QByteArray key);
SaveResult convertFile(const QString &inputPath, const QString &outputPath,
                       const QByteArray &key, bool allowOverwrite = false);

}
