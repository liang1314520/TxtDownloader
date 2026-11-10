#include "videoapitypes.h"
#include "videocover.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QImage>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

#include <algorithm>

#include <windows.h>
#include <bcrypt.h>

class VideoApiTest : public QObject {
    Q_OBJECT
private slots:
    void exactSearchParameters();
    void categoryIdentity();
    void rejectsInvalidEnvelope_data();
    void rejectsInvalidEnvelope();
    void parsesOnlyMetadata();
    void rejectsInvalidMetadata();
    void parsesDetailsAsPlainText();
    void enforcesLimits();
    void coverDerivationAndPlainImage();
    void coverCbcRoundTripAndInvalidPadding();
    void rejectsOversizedDetailsAndQueryPart();
};

namespace {
QByteArray encryptCover(const QByteArray &plain, QByteArray key)
{
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_KEY_HANDLE handle = nullptr;
    auto status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_AES_ALGORITHM, nullptr, 0);
    if (status >= 0)
        status = BCryptSetProperty(algorithm, BCRYPT_CHAINING_MODE,
            reinterpret_cast<PUCHAR>(const_cast<wchar_t *>(BCRYPT_CHAIN_MODE_CBC)), sizeof(BCRYPT_CHAIN_MODE_CBC), 0);
    if (status >= 0)
        status = BCryptGenerateSymmetricKey(algorithm, &handle, nullptr, 0,
            reinterpret_cast<PUCHAR>(key.data()), static_cast<ULONG>(key.size()), 0);
    auto iv = key;
    QByteArray encrypted(plain.size() + 16, Qt::Uninitialized);
    ULONG written = 0;
    if (status >= 0)
        status = BCryptEncrypt(handle, reinterpret_cast<PUCHAR>(const_cast<char *>(plain.constData())),
            static_cast<ULONG>(plain.size()), nullptr, reinterpret_cast<PUCHAR>(iv.data()), static_cast<ULONG>(iv.size()),
            reinterpret_cast<PUCHAR>(encrypted.data()), static_cast<ULONG>(encrypted.size()), &written, BCRYPT_BLOCK_PADDING);
    if (handle)
        BCryptDestroyKey(handle);
    if (algorithm)
        BCryptCloseAlgorithmProvider(algorithm, 0);
    return status >= 0 ? encrypted.first(written) : QByteArray();
}
QJsonObject sample()
{
    return {{"id", "synthetic-A"}, {"title", QStringLiteral("<b>合成视频</b>")},
            {"cover", "https://covers.example.invalid/synthetic.png"}, {"duration", 90}, {"views", 12}};
}
QByteArray response(const QJsonArray &items)
{
    return QJsonDocument(QJsonObject{{"code", 200}, {"data", QJsonObject{{"list", items}}}}).toJson();
}
}

void VideoApiTest::exactSearchParameters()
{
    VideoApi::Query query;
    query.kind = VideoApi::QueryKind::Search;
    query.keyword = QStringLiteral("合成关键词 & 字符");
    for (const auto part : {VideoApi::SearchPart::Video, VideoApi::SearchPart::Av,
                            VideoApi::SearchPart::Animation, VideoApi::SearchPart::Restricted}) {
        query.part = part;
        const auto parameters = query.parameters(3);
        QCOMPARE(parameters.queryItemValue("wd"), query.keyword);
        QCOMPARE(parameters.queryItemValue("part"), QString::number(static_cast<int>(part)));
        QCOMPARE(parameters.queryItemValue("limit"), QStringLiteral("20"));
        QCOMPARE(parameters.queryItemValue("page"), QStringLiteral("3"));
        QVERIFY(!parameters.hasQueryItem("keyword"));
        QVERIFY(!parameters.hasQueryItem("types"));
    }
    QVERIFY(VideoApi::validateQuery(query, 0).size() > 0);
    query.part = static_cast<VideoApi::SearchPart>(5);
    QVERIFY(!VideoApi::validateQuery(query, 1).isEmpty());
}

void VideoApiTest::categoryIdentity()
{
    QVector<VideoApi::Category> source{{"parent", QStringLiteral("同名"),
        {{"child-A", QStringLiteral("同名"), 0}, {"child-B", QStringLiteral("同名"), 1}}}};
    VideoApi::Query all, child;
    QVERIFY(VideoApi::categoryQuery(source, "parent", {}, all).isEmpty());
    QCOMPARE(all.parameters(1).queryItemValue("types"), QStringLiteral("child-A,child-B"));
    QVERIFY(VideoApi::categoryQuery(source, "parent", "child-A", child).isEmpty());
    QCOMPARE(child.parameters(2).queryItemValue("types"), QStringLiteral("child-A"));
    QVERIFY(all.key() != child.key());
    const auto key = all.key();
    std::reverse(all.typeIds.begin(), all.typeIds.end());
    QCOMPARE(all.key(), key);
    source[0].name = QStringLiteral("改名不改身份");
    VideoApi::Query renamed;
    QVERIFY(VideoApi::categoryQuery(source, "parent", {}, renamed).isEmpty());
    QCOMPARE(renamed.key(), key);
    renamed.typeIds = {"child-B", "child-A"};
    VideoApi::Query restored;
    QVERIFY(VideoApi::Query::fromJson(renamed.toJson(), restored).isEmpty());
    QCOMPARE(restored.typeIds, renamed.typeIds);
    QCOMPARE(restored.key(), key);
    QVERIFY(!VideoApi::categoryQuery(source, "missing", {}, renamed).isEmpty());
}

void VideoApiTest::rejectsInvalidEnvelope_data()
{
    QTest::addColumn<QByteArray>("body");
    for (int code : {1, 201, 302, 403})
        QTest::newRow(qPrintable(QString::number(code))) << QJsonDocument(QJsonObject{
            {"code", code}, {"data", QJsonObject{{"list", QJsonArray()}}}}).toJson();
    QTest::newRow("null") << QByteArrayLiteral("{\"code\":200,\"data\":null}");
    QTest::newRow("missing-list") << QByteArrayLiteral("{\"code\":200,\"data\":{}}");
    QTest::newRow("string-code") << QByteArrayLiteral("{\"code\":\"200\",\"data\":{\"list\":[]}}");
    QTest::newRow("malformed") << QByteArrayLiteral("{");
    QTest::newRow("invalid-utf8") << QByteArray::fromHex("7b22636f6465223a3230302c2264617461223a7b226c697374223a5b5d7d2c2278223a22ff227d");
}

void VideoApiTest::rejectsInvalidEnvelope()
{
    QFETCH(QByteArray, body);
    VideoApi::Page page;
    VideoApi::Summary saved;
    saved.id = "kept";
    saved.title = QStringLiteral("保留");
    page.items.append(saved);
    QVERIFY(!VideoApi::parseList(body, page).isEmpty());
    QCOMPARE(page.items.size(), 1);
    QCOMPARE(page.items.first().id, saved.id);
}

void VideoApiTest::parsesOnlyMetadata()
{
    auto item = sample();
    item.insert("play_url", "https://media.example.invalid/never-request");
    VideoApi::Page list, search;
    QVERIFY(VideoApi::parseList(response({item}), list).isEmpty());
    QVERIFY(VideoApi::parseSearch(response({item}), search).isEmpty());
    QCOMPARE(list.items.first().id, QStringLiteral("synthetic-A"));
    QCOMPARE(search.items.first().title, QStringLiteral("<b>合成视频</b>"));
    QCOMPARE(list.items.first().durationSeconds, 90);
    QCOMPARE(list.items.first().thumbnailKey.size(), 64);
    QVERIFY(VideoApi::parseList(response({}), list).isEmpty());
    QVERIFY(list.items.isEmpty());
}

void VideoApiTest::rejectsInvalidMetadata()
{
    for (const QJsonValue &value : {QJsonValue(-1), QJsonValue(0.5), QJsonValue("12")}) {
        auto item = sample();
        item.insert("duration", value);
        VideoApi::Page page;
        QVERIFY(!VideoApi::parseList(response({item}), page).isEmpty());
    }
    VideoApi::Page page;
    QVERIFY(!VideoApi::parseList(response({sample(), sample()}), page).isEmpty());
    auto item = sample();
    item.insert("title", QString(257, QLatin1Char('x')));
    QVERIFY(!VideoApi::parseList(response({item}), page).isEmpty());
    QVERIFY(!VideoApi::validateId(QStringLiteral("bad\nid")).isEmpty());
    QVERIFY(VideoApi::validateId(QStringLiteral("Not-a-novel-id")).isEmpty());
}

void VideoApiTest::parsesDetailsAsPlainText()
{
    const QJsonObject details{{"id", "synthetic-A"}, {"title", "<b>title</b>"},
        {"views", "1.2万"}, {"comments", "0"}, {"favorites", "2"}, {"created_at", "2026-01-01"},
        {"labels", QJsonArray{QJsonObject{{"id", "label-1"}, {"name", "<i>tag</i>"}}}},
        {"users", QJsonArray{QJsonObject{{"id", "person-1"}, {"name", "synthetic"}}}},
        {"play_url", "https://media.example.invalid/ignored"}};
    const auto bytes = QJsonDocument(QJsonObject{{"code", 200}, {"data", details}}).toJson();
    VideoApi::Details result;
    QVERIFY(VideoApi::parseDetails(bytes, "synthetic-A", result).isEmpty());
    QCOMPARE(result.labels, QStringList{"<i>tag</i>"});
    QCOMPARE(result.viewsText, QStringLiteral("1.2万"));
    QVERIFY(!VideoApi::parseDetails(bytes, "different", result).isEmpty());
    QCOMPARE(result.id, QStringLiteral("synthetic-A"));
}

void VideoApiTest::enforcesLimits()
{
    VideoApi::Page result;
    QVERIFY(!VideoApi::parseList(QByteArray(32 * 1024 * 1024 + 1, ' '), result).isEmpty());
    QVector<VideoApi::Category> categories;
    const auto child = QJsonObject{{"id", "child"}, {"name", "synthetic"}, {"stype", 0}};
    auto parent = QJsonObject{{"id", "parent"}, {"name", "synthetic"}, {"child", QJsonArray{child}}};
    QVERIFY(VideoApi::parseCategories(response({parent}), categories).isEmpty());
    parent.insert("child", QJsonArray{child, child});
    QVERIFY(!VideoApi::parseCategories(response({parent}), categories).isEmpty());
    QCOMPARE(categories.size(), 1);
    QCOMPARE(categories.first().children.size(), 1);
}

void VideoApiTest::coverDerivationAndPlainImage()
{
    const auto ordinary = VideoCover::deriveKey(QStringLiteral("https://covers.example.invalid/synthetic.png"));
    QCOMPARE(ordinary, QCryptographicHash::hash("synthetic", QCryptographicHash::Md5));
    const auto vlog = VideoCover::deriveKey(QStringLiteral("https://covers.example.invalid/vlog/synthetic.png"));
    QCOMPARE(vlog, QCryptographicHash::hash(QCryptographicHash::hash("synthetic", QCryptographicHash::Sha1).toHex(),
                                           QCryptographicHash::Md5));
    QImage image(16, 12, QImage::Format_ARGB32);
    image.fill(Qt::blue);
    QByteArray png;
    QBuffer buffer(&png);
    QVERIFY(buffer.open(QIODevice::WriteOnly));
    QVERIFY(image.save(&buffer, "PNG"));
    const auto decoded = VideoCover::decode(QStringLiteral("https://covers.example.invalid/synthetic.png"), png);
    QVERIFY(decoded.error.isEmpty());
    QCOMPARE(decoded.image.size(), image.size());
    QVERIFY(!VideoCover::decode(QStringLiteral("https://covers.example.invalid/synthetic.png"), "invalid").error.isEmpty());
    QVERIFY(!VideoCover::decode({}, QByteArray(8 * 1024 * 1024 + 1, 'x')).error.isEmpty());
}

void VideoApiTest::coverCbcRoundTripAndInvalidPadding()
{
    QImage image(20, 16, QImage::Format_ARGB32);
    image.fill(Qt::green);
    QByteArray png;
    QBuffer buffer(&png);
    QVERIFY(buffer.open(QIODevice::WriteOnly));
    QVERIFY(image.save(&buffer, "PNG"));
    for (const auto &url : {QStringLiteral("https://covers.example.invalid/合成.png"),
                            QStringLiteral("https://covers.example.invalid/vlog/合成.png")}) {
        const auto key = VideoCover::deriveKey(url);
        const auto encrypted = encryptCover(png, key);
        QVERIFY(!encrypted.isEmpty());
        QCOMPARE(encrypted.size() % 16, 0);
        const auto result = VideoCover::decode(url, encrypted);
        QVERIFY(result.error.isEmpty());
        QCOMPARE(result.image.convertToFormat(QImage::Format_ARGB32), image);
        auto wrongPadding = encrypted;
        const int padding = 16 - static_cast<int>(png.size() % 16);
        wrongPadding[wrongPadding.size() - 17] = char(static_cast<unsigned char>(wrongPadding[wrongPadding.size() - 17]) ^ padding);
        QVERIFY(!VideoCover::decode(url, wrongPadding).error.isEmpty());
        QVERIFY(!VideoCover::decode(url, encrypted.chopped(1)).error.isEmpty());
        QVERIFY(!VideoCover::decode(QStringLiteral("short"), encrypted).error.isEmpty());
    }
    const auto upper = VideoCover::deriveKey(QStringLiteral("https://covers.example.invalid/VLOG/synthetic.png"));
    QCOMPARE(upper, QCryptographicHash::hash("synthetic", QCryptographicHash::Md5));
    QVERIFY(!VideoCover::decode({}, "GIF89a").error.isEmpty());
}

void VideoApiTest::rejectsOversizedDetailsAndQueryPart()
{
    VideoApi::Query query;
    QVERIFY(!VideoApi::Query::fromJson({{"kind", "search"}, {"keyword", "synthetic"},
                                      {"part", 4294967298.0}}, query).isEmpty());
    QJsonArray labels, users;
    for (int index = 0; index < 128; ++index) {
        const QJsonObject name{{"id", QString::number(index)}, {"name", QString(256, QLatin1Char('x'))}};
        labels.append(name);
        users.append(name);
    }
    VideoApi::Details details;
    const auto body = QJsonDocument(QJsonObject{{"code", 200}, {"data", QJsonObject{
        {"id", "synthetic"}, {"title", "synthetic"}, {"labels", labels}, {"users", users}}}}).toJson();
    QVERIFY(!VideoApi::parseDetails(body, "synthetic", details).isEmpty());
}

QTEST_GUILESS_MAIN(VideoApiTest)
#include "tst_videoapi.moc"
