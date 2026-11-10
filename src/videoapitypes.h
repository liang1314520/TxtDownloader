#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QStringList>
#include <QUrl>
#include <QUrlQuery>
#include <QVector>

namespace VideoApi {
constexpr qint64 MaxJsonBytes = 32 * 1024 * 1024;
constexpr int MaxItems = 10000;
constexpr int MaxCategories = 2048;

enum class SearchPart { Video = 2, Av = 1, Animation = 3, Restricted = 4 };
enum class QueryKind { Category, Search };
struct ChildCategory { QString id; QString name; int style = 0; };
struct Category { QString id; QString name; QVector<ChildCategory> children; };
struct Query {
    QueryKind kind = QueryKind::Category;
    QString parentId;
    QString childId;
    QStringList typeIds;
    QString keyword;
    SearchPart part = SearchPart::Video;
    QString key() const;
    QUrlQuery parameters(int page) const;
    QJsonObject toJson() const;
    static QString fromJson(const QJsonObject &object, Query &output);
};
struct Summary {
    QString id;
    QString title;
    QString categoryText;
    qint64 durationSeconds = 0;
    qint64 views = 0;
    QUrl coverUrl;
    QString thumbnailKey;
};
struct Details {
    QString id;
    QString title;
    QString createdAt;
    QString viewsText;
    QString commentsText;
    QString favoritesText;
    QStringList labels;
    QStringList people;
};
struct Page { QVector<Summary> items; };

bool validText(const QString &text, qsizetype maximum, bool multiline = false);
QString validateId(const QString &id);
QString validateSummary(const Summary &item);
QString validateCategories(const QVector<Category> &categories);
QString categoryQuery(const QVector<Category> &source, const QString &parentId,
                      const QString &childId, Query &query);
QString validateQuery(const Query &query, int page);
QString parseCategories(const QByteArray &bytes, QVector<Category> &output);
QString parseList(const QByteArray &bytes, Page &output);
QString parseSearch(const QByteArray &bytes, Page &output);
QString parseDetails(const QByteArray &bytes, const QString &expectedId, Details &output);
QString parseGuestToken(const QByteArray &bytes, QString &output);
}
