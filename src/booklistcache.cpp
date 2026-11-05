#include "booklistcache.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>

namespace {
constexpr qint64 MaxCacheSize = 32 * 1024 * 1024;

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

bool validText(const QJsonValue &value, qsizetype maximum, bool multiline = false)
{
    if (!value.isString() || value.toString().size() > maximum)
        return false;
    const auto text = value.toString();
    return std::none_of(text.cbegin(), text.cend(), [multiline](QChar ch) {
        return ch.isNull() || (ch.category() == QChar::Other_Control
            && !(multiline && (ch == QLatin1Char('\n') || ch == QLatin1Char('\r') || ch == QLatin1Char('\t'))));
    });
}
}

QString BookListCache::defaultPath(const NovelCategory &category)
{
    const auto fileName = category.key() == NovelCategory::defaultCategory().key()
        ? QStringLiteral("books-4.0.9-group-5-10-23-v1.json")
        : QStringLiteral("books-4.0.9-category-%1-v2.json").arg(category.key());
    return QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).filePath(fileName);
}

QString BookListCache::load(const QString &path, BookListSnapshot &snapshot, const NovelCategory &category)
{
    if (!category.isValid())
        return QStringLiteral("分类条件无效，未读取列表缓存。");
    if (!QFileInfo::exists(path))
        return {};
    if (!safeLocation(path))
        return QStringLiteral("列表缓存位置不安全，未读取缓存。");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > MaxCacheSize)
        return QStringLiteral("列表缓存不可读或超过 32 MiB，未使用缓存。");
    const auto bytes = file.read(MaxCacheSize + 1);
    if (file.error() != QFileDevice::NoError || bytes.size() > MaxCacheSize)
        return QStringLiteral("读取列表缓存失败。");
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return QStringLiteral("列表缓存已损坏，请刷新分组。");
    const auto object = document.object();
    const auto page = object.value(QStringLiteral("page"));
    BookListSnapshot loaded;
    loaded.page = page.toInt(-1);
    loaded.updatedAt = QDateTime::fromString(object.value(QStringLiteral("updated_at")).toString(), Qt::ISODateWithMs);
    const auto version = object.value(QStringLiteral("version")).toInt();
    const bool matchesCategory = version == 1
        ? category.key() == NovelCategory::defaultCategory().key()
        : version == 2 && object.value(QStringLiteral("filters")).isObject()
            && object.value(QStringLiteral("filters")).toObject() == category.parameters();
    if (!matchesCategory
        || object.value(QStringLiteral("app_version")).toString() != QStringLiteral("4.0.9")
        || object.value(QStringLiteral("tag_id")).toString() != category.tagId
        || !page.isDouble() || page.toDouble() != loaded.page || loaded.page < 1 || loaded.page > 10000
        || !loaded.updatedAt.isValid() || !object.value(QStringLiteral("has_more")).isBool()
        || !object.value(QStringLiteral("complete")).isBool() || !object.value(QStringLiteral("books")).isArray())
        return QStringLiteral("列表缓存版本或分页状态不受支持，请刷新分组。");
    loaded.hasMore = object.value(QStringLiteral("has_more")).toBool();
    loaded.complete = object.value(QStringLiteral("complete")).toBool();
    if ((loaded.complete && loaded.hasMore) || (loaded.page == 10000 && loaded.hasMore))
        return QStringLiteral("列表缓存分页状态不一致，请刷新分组。");
    const auto books = object.value(QStringLiteral("books")).toArray();
    if (books.size() > 100000)
        return QStringLiteral("列表缓存超过 100000 本限制，未使用缓存。");
    QSet<QString> ids;
    for (const auto &entry : books) {
        if (!entry.isObject())
            return QStringLiteral("列表缓存包含无效书籍记录。");
        const auto item = entry.toObject();
        NovelSearchBook book;
        book.id = item.value(QStringLiteral("id")).toString().toLower();
        book.name = item.value(QStringLiteral("name")).toString();
        if (!NovelApi::isId(book.id) || ids.contains(book.id) || book.name.isEmpty()
            || !validText(item.value(QStringLiteral("name")), 256)
            || !validText(item.value(QStringLiteral("author")), 256)
            || !validText(item.value(QStringLiteral("chapter_count")), 256)
            || !validText(item.value(QStringLiteral("status")), 256)
            || !validText(item.value(QStringLiteral("description")), 65536, true))
            return QStringLiteral("列表缓存的书籍资料无效，请刷新分组。");
        ids.insert(book.id);
        book.author = item.value(QStringLiteral("author")).toString();
        book.chapterCount = item.value(QStringLiteral("chapter_count")).toString();
        book.status = item.value(QStringLiteral("status")).toString();
        book.description = item.value(QStringLiteral("description")).toString();
        loaded.books.append(book);
    }
    snapshot = std::move(loaded);
    return {};
}

QString BookListCache::save(const QString &path, const BookListSnapshot &snapshot, const NovelCategory &category)
{
    if (!category.isValid() || snapshot.page < 1 || snapshot.page > 10000 || !snapshot.updatedAt.isValid()
        || snapshot.books.size() > 100000 || (snapshot.complete && snapshot.hasMore))
        return QStringLiteral("列表缓存状态不正确，未写入磁盘；内存结果仍保留。");
    const QFileInfo info(path);
    if (!safeLocation(path) || !QDir().mkpath(info.absolutePath()) || !safeLocation(path))
        return QStringLiteral("无法创建安全的列表缓存位置；内存结果仍保留。");
    QJsonArray books;
    for (const auto &book : snapshot.books) {
        books.append(QJsonObject{
            {QStringLiteral("id"), book.id}, {QStringLiteral("name"), book.name},
            {QStringLiteral("author"), book.author}, {QStringLiteral("chapter_count"), book.chapterCount},
            {QStringLiteral("status"), book.status}, {QStringLiteral("description"), book.description}});
    }
    const QJsonObject object{
        {QStringLiteral("version"), 2}, {QStringLiteral("app_version"), QStringLiteral("4.0.9")},
        {QStringLiteral("tag_id"), category.tagId}, {QStringLiteral("filters"), category.parameters()},
        {QStringLiteral("updated_at"), snapshot.updatedAt.toUTC().toString(Qt::ISODateWithMs)},
        {QStringLiteral("page"), snapshot.page}, {QStringLiteral("has_more"), snapshot.hasMore},
        {QStringLiteral("complete"), snapshot.complete}, {QStringLiteral("books"), books}};
    const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    if (bytes.size() > MaxCacheSize)
        return QStringLiteral("列表缓存超过 32 MiB，未写入磁盘；内存结果仍保留。");
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return QStringLiteral("保存列表缓存失败；内存结果仍保留。");
    return {};
}

QString BookListCache::categoriesPath()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .filePath(QStringLiteral("categories-4.0.9-v1.json"));
}

QString BookListCache::loadCategories(const QString &path, QVector<NovelCategory> &categories)
{
    if (!QFileInfo::exists(path))
        return {};
    if (!safeLocation(path))
        return QStringLiteral("分类配置位置不安全，未读取。");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > 256 * 1024)
        return QStringLiteral("分类配置不可读或超过 256 KiB，未读取。");
    const auto bytes = file.read(256 * 1024 + 1);
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    const auto object = document.object();
    if (file.error() != QFileDevice::NoError || bytes.size() > 256 * 1024
        || error.error != QJsonParseError::NoError || !document.isObject()
        || object.value(QStringLiteral("version")).toInt() != 1
        || object.value(QStringLiteral("app_version")).toString() != QStringLiteral("4.0.9")
        || !object.value(QStringLiteral("categories")).isArray())
        return QStringLiteral("分类配置格式不正确，未替换当前分类。");
    const auto entries = object.value(QStringLiteral("categories")).toArray();
    if (entries.isEmpty() || entries.size() > 256)
        return QStringLiteral("分类数量无效，最多支持 256 个分类。");
    QVector<NovelCategory> loaded;
    QSet<QString> keys;
    for (const auto &entry : entries) {
        const auto item = entry.toObject();
        NovelCategory category;
        if (!entry.isObject() || !validText(item.value(QStringLiteral("name")), 128)
            || item.value(QStringLiteral("name")).toString().trimmed().isEmpty()
            || !item.value(QStringLiteral("filters")).isObject()
            || !NovelCategory::fromRequestData(item.value(QStringLiteral("filters")).toObject(), category).isEmpty()
            || keys.contains(category.key()))
            return QStringLiteral("分类配置包含无效或重复记录，未替换当前分类。");
        category.name = item.value(QStringLiteral("name")).toString();
        keys.insert(category.key());
        loaded.append(category);
    }
    categories = std::move(loaded);
    return {};
}

QString BookListCache::saveCategories(const QString &path, const QVector<NovelCategory> &categories)
{
    if (categories.isEmpty() || categories.size() > 256)
        return QStringLiteral("分类数量无效，最多支持 256 个分类。");
    QJsonArray entries;
    QSet<QString> keys;
    for (const auto &category : categories) {
        if (!category.isValid() || !validText(category.name, 128) || category.name.trimmed().isEmpty()
            || keys.contains(category.key()))
            return QStringLiteral("分类名称或条件无效，或包含重复分类，未保存。");
        keys.insert(category.key());
        entries.append(QJsonObject{{QStringLiteral("name"), category.name},
                                   {QStringLiteral("filters"), category.parameters()}});
    }
    const auto bytes = QJsonDocument(QJsonObject{
        {QStringLiteral("version"), 1}, {QStringLiteral("app_version"), QStringLiteral("4.0.9")},
        {QStringLiteral("categories"), entries}}).toJson(QJsonDocument::Compact);
    if (bytes.size() > 256 * 1024 || !safeLocation(path)
        || !QDir().mkpath(QFileInfo(path).absolutePath()) || !safeLocation(path))
        return QStringLiteral("无法安全保存分类配置，未保存分类配置。");
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return QStringLiteral("保存分类配置失败，未保存分类配置。");
    return {};
}
