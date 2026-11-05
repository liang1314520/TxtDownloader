#include "novelcategory.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QRegularExpression>

QJsonObject NovelCategory::parameters() const
{
    QJsonObject data{{QStringLiteral("tag_id"), tagId}};
    if (!adCode.isEmpty())
        data.insert(QStringLiteral("ad_code"), adCode);
    if (!position.isEmpty())
        data.insert(QStringLiteral("position"), position);
    return data;
}

QString NovelCategory::key() const
{
    return QString::fromLatin1(QCryptographicHash::hash(
        QJsonDocument(parameters()).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
}

bool NovelCategory::isValid() const
{
    static const QRegularExpression tags(QStringLiteral("\\A[0-9]{1,10}(?:,[0-9]{1,10}){0,31}\\z"));
    static const QRegularExpression code(QStringLiteral("\\A[A-Za-z0-9_-]{0,128}\\z"));
    return tags.match(tagId).hasMatch() && code.match(adCode).hasMatch()
        && (position.isEmpty() || position == QStringLiteral("novel"));
}

NovelCategory NovelCategory::defaultCategory()
{
    return {QStringLiteral("原分类（5,10,23）"), QStringLiteral("5,10,23"),
            QStringLiteral("video_mix_vertical"), {}};
}

QString NovelCategory::fromRequestData(const QJsonObject &data, NovelCategory &category)
{
    for (auto it = data.begin(); it != data.end(); ++it) {
        const auto &key = it.key();
        if (key != QStringLiteral("tag_id") && key != QStringLiteral("ad_code")
            && key != QStringLiteral("position") && key != QStringLiteral("page")
            && key != QStringLiteral("keywords"))
            return QStringLiteral("分类请求包含尚未支持的筛选字段，未添加分类。");
        if (!it.value().isString())
            return QStringLiteral("分类请求参数格式不正确，未添加分类。");
    }
    if (!data.value(QStringLiteral("keywords")).toString().isEmpty())
        return QStringLiteral("关键词搜索不是分类请求，未添加分类。");
    if (data.contains(QStringLiteral("page"))) {
        bool ok = false;
        const auto page = data.value(QStringLiteral("page")).toString().toInt(&ok);
        if (!ok || page < 1 || page > 10000)
            return QStringLiteral("分类请求页码无效，未添加分类。");
    }
    NovelCategory parsed;
    parsed.tagId = data.value(QStringLiteral("tag_id")).toString();
    parsed.adCode = data.value(QStringLiteral("ad_code")).toString();
    parsed.position = data.value(QStringLiteral("position")).toString();
    if (!parsed.isValid())
        return QStringLiteral("请求没有受支持的小说分类条件，未添加分类。");
    category = std::move(parsed);
    return {};
}
