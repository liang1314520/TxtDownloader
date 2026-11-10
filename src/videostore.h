#pragma once

#include "videoapitypes.h"

#include <QDateTime>
#include <QHash>

struct VideoStoragePaths {
    QString dataRoot;
    QString cacheRoot;
    static VideoStoragePaths defaults();
};
struct VideoSnapshot {
    VideoApi::Query query;
    QVector<VideoApi::Summary> items;
    int page = 0;
    bool hasMore = true;
    bool complete = false;
    QDateTime updatedAt;
};
struct VideoCategoryOrder {
    QStringList parents;
    QHash<QString, QStringList> children;
};
struct VideoSession {
    QString uuid;
    QByteArray token;
    QUrl origin;
    bool registrationAttempted = false;
};
namespace VideoStore {
QString categoriesPath(const VideoStoragePaths &paths);
QString favoritesPath(const VideoStoragePaths &paths);
QString orderPath(const VideoStoragePaths &paths);
QString sessionPath(const VideoStoragePaths &paths);
QString snapshotPath(const VideoStoragePaths &paths, const VideoApi::Query &query);
bool safeLocation(const QString &path);
QString loadCategories(const VideoStoragePaths &, QVector<VideoApi::Category> &);
QString saveCategories(const VideoStoragePaths &, const QVector<VideoApi::Category> &);
QString loadOrder(const VideoStoragePaths &, VideoCategoryOrder &);
QString saveOrder(const VideoStoragePaths &, const QVector<VideoApi::Category> &, const VideoCategoryOrder &);
VideoCategoryOrder mergedOrder(const QVector<VideoApi::Category> &, const VideoCategoryOrder &);
QString loadSnapshot(const VideoStoragePaths &, const VideoApi::Query &, VideoSnapshot &);
QString saveSnapshot(const VideoStoragePaths &, const VideoSnapshot &);
QString loadFavorites(const VideoStoragePaths &, QVector<VideoApi::Summary> &);
QString updateFavorite(const VideoStoragePaths &, const VideoApi::Summary &, bool favorite,
                       QVector<VideoApi::Summary> &committed);
QString loadSession(const VideoStoragePaths &, VideoSession &);
QString saveSession(const VideoStoragePaths &, const VideoSession &);
QString clearSessionToken(const VideoStoragePaths &, VideoSession &committed);
}
