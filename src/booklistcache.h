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
QString defaultPath(const NovelCategory &category = NovelCategory::defaultCategory());
QString load(const QString &path, BookListSnapshot &snapshot,
             const NovelCategory &category = NovelCategory::defaultCategory());
QString save(const QString &path, const BookListSnapshot &snapshot,
             const NovelCategory &category = NovelCategory::defaultCategory());
QString categoriesPath();
QString loadCategories(const QString &path, QVector<NovelCategory> &categories);
QString saveCategories(const QString &path, const QVector<NovelCategory> &categories);
}
