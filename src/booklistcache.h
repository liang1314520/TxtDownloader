#pragma once

#include "novelapi.h"

#include <QDateTime>

struct BookListSnapshot {
    QVector<NovelSearchBook> books;
    int page = 0;
    bool hasMore = false;
    bool complete = false;
    QDateTime updatedAt;
};

namespace BookListCache {
QString defaultPath();
QString load(const QString &path, BookListSnapshot &snapshot);
QString save(const QString &path, const BookListSnapshot &snapshot);
}
