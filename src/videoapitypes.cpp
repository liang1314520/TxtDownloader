#include "videoapitypes.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace {
constexpr double MaxExactInteger = 9007199254740991.0;

bool text(const QJsonValue &value, qsizetype maximum, bool required = false)
{
    return value.isString() && VideoApi::validText(value.toString(), maximum)
        && (!required || !value.toString().trimmed().isEmpty());
}
bool number(const QJsonValue &value, qint64 &output)
{
    const auto n = value.toDouble(-1);
    if (!value.isDouble() || !std::isfinite(n) || n < 0 || n > MaxExactInteger || std::floor(n) != n)
        return false;
    output = static_cast<qint64>(n);
    return true;
}
QString envelope(const QByteArray &bytes, QJsonObject &data)
{
    if (bytes.isEmpty() || bytes.size() > VideoApi::MaxJsonBytes)
        return QStringLiteral("视频响应为空或超过 32 MiB，未使用。 ");
    if (!bytes.isValidUtf8())
        return QStringLiteral("视频响应不是有效的 UTF-8。 ");
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return QStringLiteral("视频响应 JSON 无效。 ");
    const auto object = document.object();
    const auto code = object.value(QStringLiteral("code"));
    if (!code.isDouble() || code.toDouble() != 200)
        return QStringLiteral("视频服务未返回正常业务结果；未修改现有资料。 ");
    if (!object.value(QStringLiteral("data")).isObject())
        return QStringLiteral("视频响应缺少有效的数据对象。 ");
    data = object.value(QStringLiteral("data")).toObject();
    return {};
}
QString parsePage(const QByteArray &bytes, VideoApi::Page &output)
{
    QJsonObject data;
    const auto error = envelope(bytes, data);
    if (!error.isEmpty())
        return error;
    const auto value = data.value(QStringLiteral("list"));
    if (!value.isArray() || value.toArray().size() > VideoApi::MaxItems)
        return QStringLiteral("视频列表字段无效或超过 10000 条。 ");
    VideoApi::Page loaded;
    QSet<QString> ids;
    for (const auto &entry : value.toArray()) {
        if (!entry.isObject())
            return QStringLiteral("视频列表包含无效条目。 ");
        const auto item = entry.toObject();
        VideoApi::Summary summary;
        if (!text(item.value("id"), 256, true) || !text(item.value("title"), 256, true)
            || !text(item.value("cover"), 4096)
            || !number(item.value("duration"), summary.durationSeconds)
            || !number(item.value("views"), summary.views))
            return QStringLiteral("视频条目的必要元数据无效。 ");
        summary.id = item.value("id").toString();
        summary.title = item.value("title").toString();
        const auto cover = item.value("cover").toString();
        if (!cover.isEmpty()) {
            summary.coverUrl = QUrl(cover, QUrl::StrictMode);
            if (!summary.coverUrl.isValid() || summary.coverUrl.scheme() != QStringLiteral("https")
                || summary.coverUrl.host().isEmpty() || !summary.coverUrl.userInfo().isEmpty()
                || summary.coverUrl.hasFragment())
                summary.coverUrl = QUrl();
            else
                summary.thumbnailKey = QString::fromLatin1(QCryptographicHash::hash(cover.toUtf8(), QCryptographicHash::Sha256).toHex());
        }
        if (!VideoApi::validateSummary(summary).isEmpty() || ids.contains(summary.id))
            return QStringLiteral("视频条目无效或包含重复 ID。 ");
        ids.insert(summary.id);
        loaded.items.append(summary);
    }
    output = std::move(loaded);
    return {};
}
bool names(const QJsonValue &value, QStringList &output)
{
    if (value.isUndefined() || value.isNull())
        return true;
    if (!value.isArray() || value.toArray().size() > 128)
        return false;
    QSet<QString> ids;
    for (const auto &entry : value.toArray()) {
        if (!entry.isObject())
            return false;
        const auto item = entry.toObject();
        const auto id = item.value("id").toString();
        if (!text(item.value("id"), 256, true) || !text(item.value("name"), 256, true)
            || ids.contains(id))
            return false;
        ids.insert(id);
        output.append(item.value("name").toString());
    }
    return true;
}
}

bool VideoApi::validText(const QString &value, qsizetype maximum, bool multiline)
{
    return value.size() <= maximum && value.isValidUtf16()
        && std::none_of(value.cbegin(), value.cend(), [multiline](QChar ch) {
            return ch.isNull() || (ch.category() == QChar::Other_Control
                && !(multiline && (ch == QLatin1Char('\n') || ch == QLatin1Char('\r') || ch == QLatin1Char('\t'))));
        });
}
QString VideoApi::validateId(const QString &id)
{
    return !id.trimmed().isEmpty() && validText(id, 256) ? QString()
        : QStringLiteral("视频或分类 ID 无效。 ");
}
QString VideoApi::validateSummary(const Summary &item)
{
    static const QRegularExpression hash(QStringLiteral("^[0-9a-f]{64}$"));
    if (!validateId(item.id).isEmpty() || item.title.trimmed().isEmpty()
        || !validText(item.title, 256) || !validText(item.categoryText, 256)
        || item.durationSeconds < 0 || item.views < 0
        || item.durationSeconds > MaxExactInteger || item.views > MaxExactInteger
        || (!item.thumbnailKey.isEmpty() && !hash.match(item.thumbnailKey).hasMatch()))
        return QStringLiteral("视频展示资料无效，未使用。 ");
    return {};
}
QString VideoApi::validateCategories(const QVector<Category> &categories)
{
    QSet<QString> ids;
    int count = 0;
    for (const auto &parent : categories) {
        if (!validateId(parent.id).isEmpty() || parent.id.contains(QLatin1Char(','))
            || parent.name.trimmed().isEmpty() || !validText(parent.name, 256) || ids.contains(parent.id))
            return QStringLiteral("视频父分类无效或 ID 重复。 ");
        ids.insert(parent.id);
        ++count;
        for (const auto &child : parent.children) {
            if (!validateId(child.id).isEmpty() || child.id.contains(QLatin1Char(','))
                || child.name.trimmed().isEmpty() || !validText(child.name, 256)
                || child.style < 0 || ids.contains(child.id))
                return QStringLiteral("视频子分类无效或 ID 重复。 ");
            ids.insert(child.id);
            ++count;
        }
        if (count > MaxCategories)
            return QStringLiteral("视频分类超过 2048 项。 ");
    }
    return {};
}
QString VideoApi::categoryQuery(const QVector<Category> &source, const QString &parentId,
                                const QString &childId, Query &query)
{
    Query built;
    built.parentId = parentId;
    built.childId = childId;
    if (parentId.isEmpty() && childId.isEmpty()) {
        query = built;
        return {};
    }
    const auto parent = std::find_if(source.cbegin(), source.cend(), [&parentId](const Category &c) { return c.id == parentId; });
    if (parent == source.cend() || parent->children.isEmpty())
        return QStringLiteral("分类不存在或没有可确认的子分类筛选条件。 ");
    for (const auto &child : parent->children) {
        if (childId.isEmpty() || child.id == childId)
            built.typeIds.append(child.id);
    }
    if (built.typeIds.isEmpty())
        return QStringLiteral("子分类不属于当前父分类。 ");
    query = std::move(built);
    return {};
}
QString VideoApi::validateQuery(const Query &query, int page)
{
    if (page < 1 || page > 10000)
        return QStringLiteral("视频页码必须在 1 至 10000 之间。 ");
    if (query.kind == QueryKind::Search) {
        const auto part = static_cast<int>(query.part);
        if (query.keyword.trimmed().isEmpty() || !validText(query.keyword, 256) || part < 1 || part > 4
            || !query.typeIds.isEmpty() || !query.parentId.isEmpty() || !query.childId.isEmpty())
            return QStringLiteral("视频搜索词或分区无效。 ");
    } else if (query.kind == QueryKind::Category) {
        if (!query.keyword.isEmpty() || query.typeIds.size() > MaxCategories
            || (!query.parentId.isEmpty() && !validateId(query.parentId).isEmpty())
            || (!query.childId.isEmpty() && !validateId(query.childId).isEmpty()))
            return QStringLiteral("视频分类筛选无效。 ");
        QSet<QString> ids;
        for (const auto &id : query.typeIds) {
            if (!validateId(id).isEmpty() || id.contains(QLatin1Char(',')) || ids.contains(id))
                return QStringLiteral("视频分类条件重复或无效。 ");
            ids.insert(id);
        }
    } else {
        return QStringLiteral("视频查询类型无效。 ");
    }
    return {};
}
QJsonObject VideoApi::Query::toJson() const
{
    if (kind == QueryKind::Search)
        return {{"kind", "search"}, {"keyword", keyword}, {"part", static_cast<int>(part)}};
    return {{"kind", "category"}, {"parent", parentId}, {"child", childId},
            {"types", QJsonArray::fromStringList(typeIds)}, {"order", "-id"}};
}
QString VideoApi::Query::key() const
{
    auto identity = toJson();
    if (kind == QueryKind::Category) {
        auto types = typeIds;
        std::sort(types.begin(), types.end());
        identity.insert("types", QJsonArray::fromStringList(types));
    }
    return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(identity).toJson(QJsonDocument::Compact),
                                                      QCryptographicHash::Sha256).toHex());
}
QUrlQuery VideoApi::Query::parameters(int page) const
{
    QUrlQuery result;
    result.addQueryItem("limit", "20");
    result.addQueryItem("page", QString::number(page));
    if (kind == QueryKind::Search) {
        result.addQueryItem("wd", keyword);
        result.addQueryItem("part", QString::number(static_cast<int>(part)));
    } else {
        result.addQueryItem("types", typeIds.join(QLatin1Char(',')));
        result.addQueryItem("order", "-id");
    }
    return result;
}
QString VideoApi::Query::fromJson(const QJsonObject &object, Query &output)
{
    Query loaded;
    if (object.value("kind") == QJsonValue("search")) {
        loaded.kind = QueryKind::Search;
        qint64 part = 0;
        if (object.size() != 3 || !text(object.value("keyword"), 256, true)
            || !number(object.value("part"), part) || part < 1 || part > 4)
            return QStringLiteral("缓存搜索条件无效。 ");
        loaded.keyword = object.value("keyword").toString();
        loaded.part = static_cast<SearchPart>(part);
    } else if (object.value("kind") == QJsonValue("category")) {
        if (object.size() != 5 || !text(object.value("parent"), 256) || !text(object.value("child"), 256)
            || !object.value("types").isArray() || object.value("types").toArray().size() > MaxCategories
            || object.value("order") != QJsonValue("-id"))
            return QStringLiteral("缓存分类条件无效。 ");
        loaded.parentId = object.value("parent").toString();
        loaded.childId = object.value("child").toString();
        for (const auto &id : object.value("types").toArray()) {
            if (!id.isString())
                return QStringLiteral("缓存分类 ID 格式无效。 ");
            loaded.typeIds.append(id.toString());
        }
    } else {
        return QStringLiteral("缓存查询类型无效。 ");
    }
    const auto error = validateQuery(loaded, 1);
    if (!error.isEmpty())
        return error;
    output = std::move(loaded);
    return {};
}
QString VideoApi::parseCategories(const QByteArray &bytes, QVector<Category> &output)
{
    QJsonObject data;
    auto error = envelope(bytes, data);
    if (!error.isEmpty())
        return error;
    if (!data.value("list").isArray() || data.value("list").toArray().size() > MaxCategories)
        return QStringLiteral("视频分类列表格式无效。 ");
    QVector<Category> loaded;
    int count = 0;
    for (const auto &entry : data.value("list").toArray()) {
        if (!entry.isObject())
            return QStringLiteral("视频父分类格式无效。 ");
        const auto item = entry.toObject();
        if (!text(item.value("id"), 256, true) || !text(item.value("name"), 256, true) || !item.value("child").isArray())
            return QStringLiteral("视频父分类字段无效。 ");
        Category parent{item.value("id").toString(), item.value("name").toString(), {}};
        ++count;
        for (const auto &childEntry : item.value("child").toArray()) {
            const auto child = childEntry.toObject();
            qint64 style = 0;
            if (!childEntry.isObject() || child.contains("child") || !text(child.value("id"), 256, true)
                || !text(child.value("name"), 256, true) || !number(child.value("stype"), style)
                || style > 2147483647 || ++count > MaxCategories)
                return QStringLiteral("视频子分类字段无效、层级过深或超过限制。 ");
            parent.children.append({child.value("id").toString(), child.value("name").toString(), static_cast<int>(style)});
        }
        loaded.append(parent);
    }
    error = validateCategories(loaded);
    if (!error.isEmpty())
        return error;
    output = std::move(loaded);
    return {};
}
QString VideoApi::parseList(const QByteArray &bytes, Page &output) { return parsePage(bytes, output); }
QString VideoApi::parseSearch(const QByteArray &bytes, Page &output) { return parsePage(bytes, output); }
QString VideoApi::parseDetails(const QByteArray &bytes, const QString &expectedId, Details &output)
{
    QJsonObject data;
    auto error = envelope(bytes, data);
    if (!error.isEmpty())
        return error;
    Details loaded;
    if (!text(data.value("id"), 256, true) || data.value("id").toString() != expectedId
        || !text(data.value("title"), 256, true))
        return QStringLiteral("视频详情 ID 或标题不匹配，未使用。 ");
    loaded.id = expectedId;
    loaded.title = data.value("title").toString();
    for (const auto &field : {QStringLiteral("created_at"), QStringLiteral("views"), QStringLiteral("comments"), QStringLiteral("favorites")}) {
        const auto value = data.value(field);
        if (!value.isUndefined() && !value.isNull() && !text(value, 256))
            return QStringLiteral("视频详情展示字段无效。 ");
    }
    loaded.createdAt = data.value("created_at").toString();
    loaded.viewsText = data.value("views").toString();
    loaded.commentsText = data.value("comments").toString();
    loaded.favoritesText = data.value("favorites").toString();
    if (!names(data.value("labels"), loaded.labels) || !names(data.value("users"), loaded.people))
        return QStringLiteral("视频详情标签或人员资料无效。 ");
    qsizetype total = loaded.id.size() + loaded.title.size() + loaded.createdAt.size()
        + loaded.viewsText.size() + loaded.commentsText.size() + loaded.favoritesText.size();
    for (const auto &values : {loaded.labels, loaded.people}) {
        for (const auto &value : values)
            total += value.size();
    }
    if (total > 65536)
        return QStringLiteral("视频详情文字超过本机显示上限。 ");
    output = std::move(loaded);
    return {};
}
QString VideoApi::parseGuestToken(const QByteArray &bytes, QString &output)
{
    QJsonObject data;
    const auto error = envelope(bytes, data);
    if (!error.isEmpty())
        return error;
    const auto token = data.value("token").toString();
    if (!text(data.value("token"), 2048, true)
        || std::any_of(token.cbegin(), token.cend(), [](QChar c) { return c.unicode() < 0x21 || c.unicode() > 0x7e; }))
        return QStringLiteral("游客会话字段无效，未保存。 ");
    output = token;
    return {};
}
