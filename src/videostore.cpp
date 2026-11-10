#include "videostore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>

#include <windows.h>
#include <wincrypt.h>

#include <algorithm>
#include <cmath>

namespace {
constexpr qint64 MaxSessionBytes = 64 * 1024;
QString read(const QString &path, QByteArray &bytes, qint64 maximum = VideoApi::MaxJsonBytes)
{
    if (!VideoStore::safeLocation(path))
        return QStringLiteral("视频数据位置不安全，未读取。 ");
    if (!QFileInfo::exists(path)) {
        bytes.clear();
        return {};
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > maximum)
        return QStringLiteral("视频数据不可读、为空或超过大小限制，原文件已保留。 ");
    auto loaded = file.read(maximum + 1);
    if (file.error() != QFileDevice::NoError || loaded.size() > maximum)
        return QStringLiteral("视频数据读取失败，原文件已保留。 ");
    bytes = std::move(loaded);
    return {};
}
QString readObject(const QString &path, QJsonObject &object)
{
    QByteArray bytes;
    auto error = read(path, bytes);
    if (!error.isEmpty())
        return error;
    if (bytes.isEmpty()) {
        object = {};
        return {};
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return QStringLiteral("视频数据 JSON 已损坏，原文件已保留。 ");
    const auto loaded = document.object();
    if (loaded.value("version") != QJsonValue(1) || loaded.value("adapter") != QJsonValue("1.0.7"))
        return QStringLiteral("视频数据版本不受支持，原文件已保留。 ");
    object = loaded;
    return {};
}
QString lockFile(const QString &path, QLockFile &lock)
{
    if (!VideoStore::safeLocation(path) || !VideoStore::safeLocation(path + ".lock")
        || !QDir().mkpath(QFileInfo(path).absolutePath()) || !VideoStore::safeLocation(path)
        || !VideoStore::safeLocation(path + ".lock"))
        return QStringLiteral("无法创建安全的视频数据位置。 ");
    if (!lock.tryLock(0))
        return QStringLiteral("视频数据被其他窗口使用或无法锁定，请稍后重试；原资料未改动。 ");
    return {};
}
QString write(const QString &path, const QByteArray &bytes, qint64 maximum = VideoApi::MaxJsonBytes)
{
    if (bytes.isEmpty() || bytes.size() > maximum || !VideoStore::safeLocation(path))
        return QStringLiteral("视频数据无效、超限或位置不安全，未保存。 ");
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return QStringLiteral("保存视频数据失败，原资料未改动。 ");
    return {};
}
QByteArray jsonBytes(QJsonObject object)
{
    object.insert("version", 1);
    object.insert("adapter", "1.0.7");
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}
QJsonObject summaryObject(const VideoApi::Summary &item)
{
    return {{"id", item.id}, {"title", item.title}, {"category", item.categoryText},
            {"duration", item.durationSeconds}, {"views", item.views}, {"thumbnail", item.thumbnailKey}};
}
QString readSummaries(const QJsonValue &value, QVector<VideoApi::Summary> &output)
{
    if (!value.isArray() || value.toArray().size() > VideoApi::MaxItems)
        return QStringLiteral("视频条目数量或格式无效，未读取。 ");
    QVector<VideoApi::Summary> loaded;
    QSet<QString> ids;
    for (const auto &entry : value.toArray()) {
        const auto item = entry.toObject();
        if (!entry.isObject() || item.size() != 6 || !item.value("id").isString()
            || !item.value("title").isString() || !item.value("category").isString()
            || !item.value("thumbnail").isString())
            return QStringLiteral("视频收藏或缓存字段无效，原文件已保留。 ");
        VideoApi::Summary summary;
        summary.id = item.value("id").toString();
        summary.title = item.value("title").toString();
        summary.categoryText = item.value("category").toString();
        summary.thumbnailKey = item.value("thumbnail").toString();
        for (const auto &field : {QStringLiteral("duration"), QStringLiteral("views")}) {
            const auto v = item.value(field);
            const auto n = v.toDouble(-1);
            if (!v.isDouble() || !std::isfinite(n) || n < 0 || n > 9007199254740991.0 || std::floor(n) != n)
                return QStringLiteral("视频计数或时长无效，未读取。 ");
        }
        summary.durationSeconds = item.value("duration").toInteger(-1);
        summary.views = item.value("views").toInteger(-1);
        if (!VideoApi::validateSummary(summary).isEmpty() || ids.contains(summary.id))
            return QStringLiteral("视频资料无效或 ID 重复，原文件已保留。 ");
        ids.insert(summary.id);
        loaded.append(summary);
    }
    output = std::move(loaded);
    return {};
}
QString summariesArray(const QVector<VideoApi::Summary> &items, QJsonArray &array)
{
    if (items.size() > VideoApi::MaxItems)
        return QStringLiteral("视频资料超过 10000 条，未保存。 ");
    QSet<QString> ids;
    for (const auto &item : items) {
        const auto error = VideoApi::validateSummary(item);
        if (!error.isEmpty() || ids.contains(item.id))
            return QStringLiteral("视频资料无效或重复，未保存。 ");
        ids.insert(item.id);
        array.append(summaryObject(item));
    }
    return {};
}
QJsonArray categoriesArray(const QVector<VideoApi::Category> &categories)
{
    QJsonArray parents;
    for (const auto &parent : categories) {
        QJsonArray children;
        for (const auto &child : parent.children)
            children.append(QJsonObject{{"id", child.id}, {"name", child.name}, {"stype", child.style}});
        parents.append(QJsonObject{{"id", parent.id}, {"name", parent.name}, {"child", children}});
    }
    return parents;
}
bool stringIds(const QJsonValue &value, QStringList &output)
{
    if (!value.isArray() || value.toArray().size() > VideoApi::MaxCategories)
        return false;
    QSet<QString> ids;
    for (const auto &entry : value.toArray()) {
        const auto id = entry.toString();
        if (!entry.isString() || !VideoApi::validateId(id).isEmpty() || ids.contains(id))
            return false;
        ids.insert(id);
        output.append(id);
    }
    return true;
}
QString sessionValid(const VideoSession &session)
{
    const QUuid uuid(session.uuid);
    if (uuid.isNull() || uuid.toString(QUuid::WithoutBraces) != session.uuid
        || session.token.size() > 2048
        || std::any_of(session.token.cbegin(), session.token.cend(), [](unsigned char c) { return c < 0x21 || c > 0x7e; })
        || (!session.token.isEmpty() && (!session.registrationAttempted || session.origin.isEmpty()))
        || (!session.origin.isEmpty() && (!session.origin.isValid() || session.origin.scheme() != "https"
            || session.origin.host().isEmpty() || !session.origin.userInfo().isEmpty()
            || session.origin.hasQuery() || session.origin.hasFragment()
            || (!session.origin.path().isEmpty() && session.origin.path() != "/") || session.origin.port(443) != 443)))
        return QStringLiteral("视频游客身份或会话格式无效，未使用。 ");
    return {};
}
QString writeSession(const QString &path, const VideoSession &session)
{
    auto bytes = jsonBytes({{"uuid", session.uuid}, {"token", QString::fromLatin1(session.token)},
                            {"origin", session.origin.toString()}, {"attempted", session.registrationAttempted}});
    DATA_BLOB input{static_cast<DWORD>(bytes.size()), reinterpret_cast<BYTE *>(bytes.data())};
    DATA_BLOB output{};
    const bool ok = CryptProtectData(&input, L"NovDecoder video session 1.0.7", nullptr, nullptr, nullptr,
                                     CRYPTPROTECT_UI_FORBIDDEN, &output);
    bytes.fill('\0');
    if (!ok)
        return QStringLiteral("无法保护视频会话，未保存或继续联网。 ");
    const QByteArray encrypted(reinterpret_cast<char *>(output.pbData), output.cbData);
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    return write(path, encrypted, MaxSessionBytes);
}
}

VideoStoragePaths VideoStoragePaths::defaults()
{
    return {QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation),
            QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).filePath("video")};
}
QString VideoStore::categoriesPath(const VideoStoragePaths &paths) { return QDir(paths.dataRoot).filePath("video-categories-v1.json"); }
QString VideoStore::favoritesPath(const VideoStoragePaths &paths) { return QDir(paths.dataRoot).filePath("video-favorites-v1.json"); }
QString VideoStore::orderPath(const VideoStoragePaths &paths) { return QDir(paths.dataRoot).filePath("video-category-order-v1.json"); }
QString VideoStore::sessionPath(const VideoStoragePaths &paths) { return QDir(paths.dataRoot).filePath("video-session-v1.dat"); }
QString VideoStore::snapshotPath(const VideoStoragePaths &paths, const VideoApi::Query &query)
{
    return QDir(paths.cacheRoot).filePath(QStringLiteral("list-%1-v1.json").arg(query.key()));
}
bool VideoStore::safeLocation(const QString &path)
{
    if (path.isEmpty() || !QDir::isAbsolutePath(path))
        return false;
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
QString VideoStore::loadCategories(const VideoStoragePaths &paths, QVector<VideoApi::Category> &output)
{
    QJsonObject object;
    auto error = readObject(categoriesPath(paths), object);
    if (!error.isEmpty())
        return error;
    if (object.isEmpty()) {
        output.clear();
        return {};
    }
    if (object.size() != 3 || !object.value("categories").isArray())
        return QStringLiteral("视频分类缓存字段无效。 ");
    return VideoApi::parseCategories(QJsonDocument(QJsonObject{{"code", 200},
        {"data", QJsonObject{{"list", object.value("categories")}}}}).toJson(QJsonDocument::Compact), output);
}
QString VideoStore::saveCategories(const VideoStoragePaths &paths, const QVector<VideoApi::Category> &categories)
{
    auto error = VideoApi::validateCategories(categories);
    if (!error.isEmpty())
        return error;
    const auto path = categoriesPath(paths);
    QLockFile lock(path + ".lock");
    error = lockFile(path, lock);
    if (!error.isEmpty())
        return error;
    QVector<VideoApi::Category> existing;
    error = loadCategories(paths, existing);
    return error.isEmpty() ? write(path, jsonBytes({{"categories", categoriesArray(categories)}})) : error;
}
QString VideoStore::loadFavorites(const VideoStoragePaths &paths, QVector<VideoApi::Summary> &output)
{
    QJsonObject object;
    auto error = readObject(favoritesPath(paths), object);
    if (!error.isEmpty())
        return error;
    if (object.isEmpty()) {
        output.clear();
        return {};
    }
    if (object.size() != 3)
        return QStringLiteral("视频收藏字段不受支持，原文件已保留。 ");
    return readSummaries(object.value("items"), output);
}
QString VideoStore::updateFavorite(const VideoStoragePaths &paths, const VideoApi::Summary &item,
                                   bool favorite, QVector<VideoApi::Summary> &committed)
{
    const auto invalid = VideoApi::validateSummary(item);
    if (!invalid.isEmpty())
        return invalid;
    const auto path = favoritesPath(paths);
    QLockFile lock(path + ".lock");
    auto error = lockFile(path, lock);
    if (!error.isEmpty())
        return error;
    QVector<VideoApi::Summary> updated;
    error = loadFavorites(paths, updated);
    if (!error.isEmpty())
        return error;
    const auto found = std::find_if(updated.cbegin(), updated.cend(), [&item](const VideoApi::Summary &s) { return s.id == item.id; });
    if (favorite && found == updated.cend()) {
        auto local = item;
        local.coverUrl = QUrl();
        updated.prepend(local);
    } else if (!favorite && found != updated.cend()) {
        updated.removeAt(found - updated.cbegin());
    } else {
        committed = std::move(updated);
        return {};
    }
    QJsonArray array;
    error = summariesArray(updated, array);
    if (error.isEmpty())
        error = write(path, jsonBytes({{"items", array}}));
    if (!error.isEmpty())
        return error;
    committed = std::move(updated);
    return {};
}
QString VideoStore::loadOrder(const VideoStoragePaths &paths, VideoCategoryOrder &output)
{
    QJsonObject object;
    auto error = readObject(orderPath(paths), object);
    if (!error.isEmpty())
        return error;
    if (object.isEmpty()) {
        output = {};
        return {};
    }
    VideoCategoryOrder loaded;
    if (object.size() != 4 || !stringIds(object.value("parents"), loaded.parents)
        || !object.value("children").isObject())
        return QStringLiteral("视频分类排序已损坏，原文件已保留。 ");
    const auto children = object.value("children").toObject();
    if (children.size() != loaded.parents.size())
        return QStringLiteral("视频分类排序父子不一致。 ");
    int count = loaded.parents.size();
    QSet<QString> childIds;
    for (const auto &parent : loaded.parents) {
        QStringList ids;
        if (!stringIds(children.value(parent), ids))
            return QStringLiteral("视频子分类排序无效。 ");
        for (const auto &id : ids) {
            if (childIds.contains(id) || ++count > VideoApi::MaxCategories)
                return QStringLiteral("视频子分类排序重复或超限。 ");
            childIds.insert(id);
        }
        loaded.children.insert(parent, ids);
    }
    output = std::move(loaded);
    return {};
}
VideoCategoryOrder VideoStore::mergedOrder(const QVector<VideoApi::Category> &source, const VideoCategoryOrder &order)
{
    VideoCategoryOrder merged;
    for (const auto &id : order.parents) {
        if (std::any_of(source.cbegin(), source.cend(), [&id](const VideoApi::Category &p) { return p.id == id; })
            && !merged.parents.contains(id))
            merged.parents.append(id);
    }
    for (const auto &parent : source) {
        if (!merged.parents.contains(parent.id))
            merged.parents.append(parent.id);
        QStringList ids;
        for (const auto &id : order.children.value(parent.id)) {
            if (std::any_of(parent.children.cbegin(), parent.children.cend(), [&id](const VideoApi::ChildCategory &c) { return c.id == id; })
                && !ids.contains(id))
                ids.append(id);
        }
        for (const auto &child : parent.children) {
            if (!ids.contains(child.id))
                ids.append(child.id);
        }
        merged.children.insert(parent.id, ids);
    }
    return merged;
}
QString VideoStore::saveOrder(const VideoStoragePaths &paths, const QVector<VideoApi::Category> &source,
                              const VideoCategoryOrder &order)
{
    auto error = VideoApi::validateCategories(source);
    const auto expected = mergedOrder(source, order);
    if (!error.isEmpty() || expected.parents != order.parents || expected.children != order.children)
        return QStringLiteral("分类排序必须完整且无重复，子分类不能跨父级移动。 ");
    const auto path = orderPath(paths);
    QLockFile lock(path + ".lock");
    error = lockFile(path, lock);
    if (!error.isEmpty())
        return error;
    VideoCategoryOrder existing;
    error = loadOrder(paths, existing);
    if (!error.isEmpty())
        return error;
    QJsonObject children;
    for (auto it = order.children.cbegin(); it != order.children.cend(); ++it)
        children.insert(it.key(), QJsonArray::fromStringList(it.value()));
    return write(path, jsonBytes({{"parents", QJsonArray::fromStringList(order.parents)}, {"children", children}}));
}
QString VideoStore::loadSnapshot(const VideoStoragePaths &paths, const VideoApi::Query &query, VideoSnapshot &output)
{
    QJsonObject object;
    auto error = readObject(snapshotPath(paths, query), object);
    if (!error.isEmpty())
        return error;
    if (object.isEmpty()) {
        output = {};
        output.query = query;
        return {};
    }
    VideoSnapshot loaded;
    const auto page = object.value("page");
    if (object.size() != 8 || !object.value("query").isObject()
        || !VideoApi::Query::fromJson(object.value("query").toObject(), loaded.query).isEmpty()
        || loaded.query.key() != query.key() || !page.isDouble() || page.toDouble() != page.toInt(-1)
        || page.toInt() < 1 || page.toInt() > 10000 || !object.value("has_more").isBool()
        || !object.value("complete").isBool() || !object.value("updated_at").isString())
        return QStringLiteral("视频列表缓存条件或分页无效。 ");
    loaded.page = page.toInt();
    loaded.hasMore = object.value("has_more").toBool();
    loaded.complete = object.value("complete").toBool();
    loaded.updatedAt = QDateTime::fromString(object.value("updated_at").toString(), Qt::ISODateWithMs);
    if (!loaded.updatedAt.isValid() || (loaded.complete && loaded.hasMore) || (loaded.page == 10000 && loaded.hasMore))
        return QStringLiteral("视频列表缓存分页状态不一致。 ");
    error = readSummaries(object.value("items"), loaded.items);
    if (!error.isEmpty())
        return error;
    output = std::move(loaded);
    return {};
}
QString VideoStore::saveSnapshot(const VideoStoragePaths &paths, const VideoSnapshot &snapshot)
{
    if (!VideoApi::validateQuery(snapshot.query, snapshot.page).isEmpty() || !snapshot.updatedAt.isValid()
        || (snapshot.hasMore && (snapshot.complete || snapshot.page == 10000)))
        return QStringLiteral("视频列表缓存状态无效，未保存。 ");
    QJsonArray items;
    auto error = summariesArray(snapshot.items, items);
    if (!error.isEmpty())
        return error;
    const auto path = snapshotPath(paths, snapshot.query);
    QLockFile lock(path + ".lock");
    error = lockFile(path, lock);
    if (!error.isEmpty())
        return error;
    VideoSnapshot existing;
    error = loadSnapshot(paths, snapshot.query, existing);
    if (!error.isEmpty())
        return error;
    return write(path, jsonBytes({{"query", snapshot.query.toJson()}, {"page", snapshot.page},
        {"has_more", snapshot.hasMore}, {"complete", snapshot.complete},
        {"updated_at", snapshot.updatedAt.toUTC().toString(Qt::ISODateWithMs)}, {"items", items}}));
}
QString VideoStore::loadSession(const VideoStoragePaths &paths, VideoSession &output)
{
    QByteArray encrypted;
    auto error = read(sessionPath(paths), encrypted, MaxSessionBytes);
    if (!error.isEmpty())
        return error;
    if (encrypted.isEmpty()) {
        output = {};
        return {};
    }
    DATA_BLOB input{static_cast<DWORD>(encrypted.size()), reinterpret_cast<BYTE *>(encrypted.data())};
    DATA_BLOB plaintext{};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &plaintext))
        return QStringLiteral("视频会话无法由当前 Windows 用户解密，原文件已保留。 ");
    QByteArray bytes;
    if (plaintext.cbData <= MaxSessionBytes)
        bytes = QByteArray(reinterpret_cast<char *>(plaintext.pbData), plaintext.cbData);
    SecureZeroMemory(plaintext.pbData, plaintext.cbData);
    LocalFree(plaintext.pbData);
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(bytes, &parseError);
    bytes.fill('\0');
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return QStringLiteral("受保护视频会话已损坏，原文件已保留。 ");
    const auto object = document.object();
    if (object.size() != 6 || object.value("version") != QJsonValue(1) || object.value("adapter") != QJsonValue("1.0.7")
        || !object.value("uuid").isString() || !object.value("token").isString()
        || !object.value("origin").isString() || !object.value("attempted").isBool())
        return QStringLiteral("受保护视频会话字段无效。 ");
    VideoSession loaded;
    loaded.uuid = object.value("uuid").toString();
    const auto token = object.value("token").toString();
    loaded.token = token.toLatin1();
    if (QString::fromLatin1(loaded.token) != token)
        return QStringLiteral("受保护视频会话凭证编码无效。 ");
    loaded.origin = QUrl(object.value("origin").toString(), QUrl::StrictMode);
    loaded.registrationAttempted = object.value("attempted").toBool();
    error = sessionValid(loaded);
    if (!error.isEmpty())
        return error;
    output.token.fill('\0');
    output = std::move(loaded);
    return {};
}
QString VideoStore::saveSession(const VideoStoragePaths &paths, const VideoSession &session)
{
    auto error = sessionValid(session);
    if (!error.isEmpty())
        return error;
    const auto path = sessionPath(paths);
    QLockFile lock(path + ".lock");
    error = lockFile(path, lock);
    if (!error.isEmpty())
        return error;
    VideoSession existing;
    error = loadSession(paths, existing);
    if (!error.isEmpty())
        return error;
    if (!existing.uuid.isEmpty() && existing.uuid != session.uuid)
        return QStringLiteral("其他窗口已保存不同的视频身份，未覆盖。 ");
    if (!existing.origin.isEmpty() && existing.origin != session.origin)
        return QStringLiteral("视频身份已绑定其他来源，未移动或复用凭证。 ");
    return writeSession(path, session);
}
QString VideoStore::clearSessionToken(const VideoStoragePaths &paths, VideoSession &committed)
{
    const auto path = sessionPath(paths);
    QLockFile registration(path + ".registration.lock");
    if (!safeLocation(path + ".registration.lock") || !QDir().mkpath(QFileInfo(path).absolutePath())
        || !safeLocation(path + ".registration.lock") || !registration.tryLock(0))
        return QStringLiteral("视频游客初始化正在进行，未清除会话。 ");
    QLockFile lock(path + ".lock");
    auto error = lockFile(path, lock);
    if (!error.isEmpty())
        return error;
    VideoSession updated;
    error = loadSession(paths, updated);
    if (!error.isEmpty())
        return error;
    if (!updated.uuid.isEmpty()) {
        updated.token.fill('\0');
        updated.token.clear();
        error = writeSession(path, updated);
        if (!error.isEmpty())
            return error;
    }
    committed.token.fill('\0');
    committed = std::move(updated);
    return {};
}
