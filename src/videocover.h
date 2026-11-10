#pragma once

#include "videostore.h"

#include <QCache>
#include <QImage>
#include <QObject>

namespace VideoCover {
struct Result { QImage image; QString error; };
QByteArray deriveKey(const QString &originalUrl);
Result decode(const QString &originalUrl, const QByteArray &payload);
}
class VideoCoverCache : public QObject {
    Q_OBJECT
public:
    explicit VideoCoverCache(const VideoStoragePaths &paths, QObject *parent = nullptr);
    QImage cached(const QString &thumbnailKey) const;
    QString store(const QString &thumbnailKey, const QImage &image);
signals:
    void thumbnailReady(const QString &thumbnailKey);
private:
    QString pathFor(const QString &key) const;
    VideoStoragePaths paths;
    mutable QCache<QString, QImage> memory{64 * 1024 * 1024};
};
