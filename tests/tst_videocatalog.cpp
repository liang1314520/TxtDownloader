#include "videoapitypes.h"
#include "videostore.h"
#include "videocover.h"
#include "videocatalogcontroller.h"
#include "videoclient.h"
#include "videolistmodel.h"
#include "videocatalogwidget.h"

#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPlainTextEdit>
#include <QMouseEvent>
#include <QStyleOptionViewItem>
#include <QScrollBar>
#include <QTabWidget>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QStackedWidget>
#include <QTreeWidget>

#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

#include <QSignalSpy>

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>

#include <algorithm>
#include <cstring>

class VideoCatalogTest : public QObject {
    Q_OBJECT
private slots:
    void favoritesAreLocal();
    void preservesCorruptFilesAndLockedOutput();
    void categoryOrderIsComplete();
    void snapshotsKeepQueryIdentity();
    void sessionIsProtectedAndRetainsIdentity();
    void coverCacheIsLocalAndRejectsPaths();
    void coverCacheEvictsOnlyOwnOldFiles();
    void controllerStartsOffline();
    void controllerKeepsIndependentPages();
    void duplicateItemsDoNotConsumeCapacity();
    void controllerRejectsLateReplies();
    void refreshedCategoriesDropStaleSelectionOffline();
    void controllerKeepsLatestDetailsOnReselect();
    void clientRequiresConsentAndPublicAddresses();
    void clientPinsOriginAndKeepsTokensScoped();
    void clientRegistersOnceAndStopsOnSaveFailure();
    void clientRejectsRedirectAndCancelledReplies();
    void clientRejectsPrivateResolutionAndOversizedBody();
    void clientRegistrationLockPreventsDuplicateIdentity();
    void clientRejectsTlsFailureAndTimeout();
    void corruptSessionIsNotRecreated();
    void controllerDoesNotCommitFavoriteOnFailure();
    void delegateOnlyFavoritesInsideHitArea();
    void largeModelDoesNotCreateRowWidgets();
    void modelUsesLocalFavoritesAndPlainText();
    void widgetStartsOfflineAndKeepsLocalDetails();
    void categoryOrderCancelDoesNotWrite();
    void categoryOrderMovesWithinParentAndSaves();
    void widgetKeepsCategoryAndSearchSelections();
};

namespace {
class SyntheticReply : public QNetworkReply {
public:
    SyntheticReply(QNetworkAccessManager::Operation operation, const QNetworkRequest &request, QObject *parent)
        : QNetworkReply(parent)
    {
        setOperation(operation);
        setRequest(request);
        setUrl(request.url());
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    }
    void respond(int status, const QByteArray &body)
    {
        if (isFinished())
            return;
        bytes = body;
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        setFinished(true);
        emit readyRead();
        emit finished();
    }
    void fail(NetworkError error)
    {
        if (isFinished())
            return;
        setError(error, QStringLiteral("合成传输失败"));
        setFinished(true);
        emit finished();
    }
    void abort() override
    {
        if (isFinished())
            return;
        setError(OperationCanceledError, QStringLiteral("合成取消"));
        setFinished(true);
        emit finished();
    }
    qint64 bytesAvailable() const override { return bytes.size() - position + QNetworkReply::bytesAvailable(); }
protected:
    qint64 readData(char *data, qint64 maximum) override
    {
        const auto count = qMin(maximum, qint64(bytes.size() - position));
        if (count <= 0)
            return -1;
        memcpy(data, bytes.constData() + position, static_cast<size_t>(count));
        position += count;
        return count;
    }
private:
    QByteArray bytes;
    qint64 position = 0;
};
class SyntheticNetwork : public QNetworkAccessManager {
public:
    struct Sent { Operation operation; QNetworkRequest request; QByteArray body; QPointer<SyntheticReply> reply; };
    QVector<Sent> sent;
protected:
    QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request, QIODevice *outgoing) override
    {
        auto *reply = new SyntheticReply(operation, request, this);
        sent.append({operation, request, outgoing ? outgoing->readAll() : QByteArray(), reply});
        return reply;
    }
};
VideoClient::Resolver syntheticResolver()
{
    return [](const QString &, QObject *context, VideoClient::AddressCallback callback) {
        QTimer::singleShot(0, context, [callback] { callback({QHostAddress(QStringLiteral("8.8.8.8"))}); });
    };
}
VideoApi::Summary sample(const QString &id = QStringLiteral("synthetic-1"))
{
    VideoApi::Summary item;
    item.id = id;
    item.title = QStringLiteral("<b>合成视频</b>");
    item.coverUrl = QUrl(QStringLiteral("https://covers.example.invalid/a.png?signature=synthetic"));
    return item;
}
QByteArray readBytes(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
VideoStoragePaths pathsFor(QTemporaryDir &dir)
{
    return {dir.filePath("data"), dir.filePath("cache")};
}
}

void VideoCatalogTest::favoritesAreLocal()
{
    QTemporaryDir dir;
    const auto paths = pathsFor(dir);
    QVector<VideoApi::Summary> committed;
    const auto first = sample();
    const auto second = sample("synthetic-2");
    QVERIFY(VideoStore::updateFavorite(paths, first, true, committed).isEmpty());
    QVERIFY(VideoStore::updateFavorite(paths, second, true, committed).isEmpty());
    QVERIFY(VideoStore::updateFavorite(paths, first, true, committed).isEmpty());
    QCOMPARE(committed.size(), 2);
    QCOMPARE(committed.first().id, second.id);
    QVector<VideoApi::Summary> loaded;
    QVERIFY(VideoStore::loadFavorites(paths, loaded).isEmpty());
    QCOMPARE(loaded.size(), 2);
    QVERIFY(loaded.first().coverUrl.isEmpty());
    const auto bytes = readBytes(VideoStore::favoritesPath(paths));
    QVERIFY(!bytes.contains("signature"));
    QVERIFY(!bytes.contains("https://"));
    QVERIFY(!bytes.contains("token"));
    QVector<VideoApi::Summary> otherInstance;
    QVERIFY(VideoStore::updateFavorite(paths, first, false, otherInstance).isEmpty());
    QCOMPARE(otherInstance.size(), 1);
    QCOMPARE(otherInstance.first().id, second.id);
}

void VideoCatalogTest::preservesCorruptFilesAndLockedOutput()
{
    QTemporaryDir dir;
    const auto paths = pathsFor(dir);
    QVector<VideoApi::Summary> committed;
    QVERIFY(VideoStore::updateFavorite(paths, sample(), true, committed).isEmpty());
    const auto path = VideoStore::favoritesPath(paths);
    QLockFile lock(path + ".lock");
    QVERIFY(lock.tryLock(0));
    QVERIFY(!VideoStore::updateFavorite(paths, sample("another"), true, committed).isEmpty());
    QCOMPARE(committed.size(), 1);
    lock.unlock();
    QFile broken(path);
    QVERIFY(broken.open(QIODevice::WriteOnly));
    QCOMPARE(broken.write("broken"), 6);
    broken.close();
    QVERIFY(!VideoStore::updateFavorite(paths, sample(), false, committed).isEmpty());
    QCOMPARE(readBytes(path), QByteArrayLiteral("broken"));
    QCOMPARE(committed.size(), 1);
}

void VideoCatalogTest::categoryOrderIsComplete()
{
    QTemporaryDir dir;
    const auto paths = pathsFor(dir);
    const QVector<VideoApi::Category> source{{"p1", QStringLiteral("同名"), {{"a", "A", 0}, {"b", "B", 1}}},
                                             {"p2", QStringLiteral("同名"), {{"c", "C", 0}}}};
    QVERIFY(VideoStore::saveCategories(paths, source).isEmpty());
    VideoCategoryOrder order{{"p2", "p1"}, {{"p1", {"b", "a"}}, {"p2", {"c"}}}};
    QVERIFY(VideoStore::saveOrder(paths, source, order).isEmpty());
    VideoCategoryOrder loaded;
    QVERIFY(VideoStore::loadOrder(paths, loaded).isEmpty());
    QCOMPARE(loaded.parents, order.parents);
    auto invalid = order;
    invalid.children["p1"] = {"a", "c"};
    const auto bytes = readBytes(VideoStore::orderPath(paths));
    QVERIFY(!VideoStore::saveOrder(paths, source, invalid).isEmpty());
    QCOMPARE(readBytes(VideoStore::orderPath(paths)), bytes);
}

void VideoCatalogTest::snapshotsKeepQueryIdentity()
{
    QTemporaryDir dir;
    const auto paths = pathsFor(dir);
    VideoSnapshot saved;
    saved.page = 2;
    saved.updatedAt = QDateTime::currentDateTimeUtc();
    saved.items = {sample()};
    QVERIFY(VideoStore::saveSnapshot(paths, saved).isEmpty());
    VideoSnapshot loaded;
    QVERIFY(VideoStore::loadSnapshot(paths, saved.query, loaded).isEmpty());
    QCOMPARE(loaded.page, 2);
    QCOMPARE(loaded.items.first().id, saved.items.first().id);
    QVERIFY(loaded.items.first().coverUrl.isEmpty());
    auto other = saved.query;
    other.typeIds = {"different"};
    QVERIFY(VideoStore::snapshotPath(paths, saved.query) != VideoStore::snapshotPath(paths, other));
}

void VideoCatalogTest::sessionIsProtectedAndRetainsIdentity()
{
    QTemporaryDir dir;
    const auto paths = pathsFor(dir);
    VideoSession session;
    session.uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    session.origin = QUrl(QStringLiteral("https://api.example.invalid"));
    session.token = QByteArrayLiteral("synthetic-test-token-only");
    session.registrationAttempted = true;
    QVERIFY(VideoStore::saveSession(paths, session).isEmpty());
    QVERIFY(!readBytes(VideoStore::sessionPath(paths)).contains(session.token));
    VideoSession restored;
    QVERIFY(VideoStore::loadSession(paths, restored).isEmpty());
    QCOMPARE(restored.uuid, session.uuid);
    QVERIFY(restored.token == session.token);
    QVERIFY(VideoStore::clearSessionToken(paths, restored).isEmpty());
    QCOMPARE(restored.uuid, session.uuid);
    QVERIFY(restored.token.isEmpty());
    QVERIFY(restored.registrationAttempted);
    auto different = session;
    different.uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QVERIFY(!VideoStore::saveSession(paths, different).isEmpty());
}

void VideoCatalogTest::coverCacheIsLocalAndRejectsPaths()
{
    QTemporaryDir dir;
    const auto paths = pathsFor(dir);
    VideoCoverCache cache(paths);
    QImage image(30, 20, QImage::Format_ARGB32);
    image.fill(Qt::blue);
    const auto key = QString(64, QLatin1Char('a'));
    QVERIFY(cache.cached(key).isNull());
    QVERIFY(!cache.store(QStringLiteral("../escape"), image).isEmpty());
    QVERIFY(cache.store(key, image).isEmpty());
    QVERIFY(!cache.cached(key).isNull());
    VideoCoverCache otherInstance(paths);
    QVERIFY(!otherInstance.cached(key).isNull());
    QVERIFY(otherInstance.cached(QStringLiteral("../escape")).isNull());
}

void VideoCatalogTest::coverCacheEvictsOnlyOwnOldFiles()
{
    QTemporaryDir dir;
    const auto paths = pathsFor(dir);
    VideoCoverCache cache(paths);
    QImage image(360, 240, QImage::Format_ARGB32);
    image.fill(Qt::blue);
    const auto oldest = QString(64, QLatin1Char('a'));
    QVERIFY(cache.store(oldest, image).isEmpty());
    const auto folder = QDir(paths.cacheRoot).filePath("thumbnails");
    const auto oldTime = QDateTime::fromSecsSinceEpoch(1000000000, Qt::UTC);
    for (int index = 0; index < 33; ++index) {
        const auto key = index == 0 ? oldest : QString::number(index, 16).rightJustified(64, QLatin1Char('0'));
        QFile file(QDir(folder).filePath(key + ".png"));
        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.resize(8 * 1024 * 1024));
        QVERIFY(file.setFileTime(oldTime.addSecs(index), QFileDevice::FileAccessTime));
    }
    QFile unrelated(QDir(folder).filePath("keep-user-file.png"));
    QVERIFY(unrelated.open(QIODevice::WriteOnly));
    QCOMPARE(unrelated.write("keep"), 4);
    unrelated.close();
    QVERIFY(cache.store(QString(64, QLatin1Char('f')), image).isEmpty());
    QVERIFY(!QFileInfo::exists(QDir(folder).filePath(oldest + ".png")));
    QCOMPARE(readBytes(unrelated.fileName()), QByteArrayLiteral("keep"));
    qint64 total = 0;
    const auto files = QDir(folder).entryInfoList({"*.png"}, QDir::Files);
    for (const auto &file : files) {
        if (file.completeBaseName().size() == 64)
            total += file.size();
    }
    QVERIFY(total <= 256 * 1024 * 1024);
}

void VideoCatalogTest::controllerStartsOffline()
{
    QTemporaryDir dir;
    const auto paths = pathsFor(dir);
    QVector<VideoApi::Summary> saved;
    QVERIFY(VideoStore::updateFavorite(paths, sample(), true, saved).isEmpty());
    VideoCatalogController controller(paths);
    QSignalSpy busy(&controller, &VideoCatalogController::busyChanged);
    QSignalSpy consent(&controller, &VideoCatalogController::consentRequired);
    QCOMPARE(controller.favorites().size(), 1);
    QVERIFY(!controller.isBusy());
    QVERIFY(!QFileInfo::exists(VideoStore::sessionPath(paths)));
    controller.showSavedDetails(controller.favorites().first());
    QCOMPARE(controller.details().title, sample().title);
    QCOMPARE(busy.count(), 0);
    QCOMPARE(consent.count(), 0);
    controller.search(QStringLiteral("合成搜索"), VideoApi::SearchPart::Video);
    QCOMPARE(consent.count(), 1);
    controller.grantConsent(false);
    QVERIFY(!controller.isBusy());
    QVERIFY(!QFileInfo::exists(VideoStore::sessionPath(paths)));
}

void VideoCatalogTest::controllerKeepsIndependentPages()
{
    QTemporaryDir dir;
    VideoCatalogController controller(pathsFor(dir));
    const auto body = [](const QStringList &ids) {
        QJsonArray items;
        for (const auto &id : ids)
            items.append(QJsonObject{{"id", id}, {"title", "synthetic"}, {"cover", ""},
                                     {"duration", 1}, {"views", 0}});
        return QJsonDocument(QJsonObject{{"code", 200}, {"data", QJsonObject{{"list", items}}}}).toJson();
    };
    const auto deliver = [&](VideoCatalogController::Operation operation, const VideoApi::Query &query,
                             int page, const QStringList &ids) {
        controller.pending = {};
        controller.pending.operation = operation;
        controller.pending.query = query;
        controller.pending.page = page;
        controller.pending.requestId = 7;
        controller.pending.generation = controller.generation;
        controller.setBusy(true);
        controller.receive(7, body(ids), {});
    };
    VideoApi::Query category, search;
    search.kind = VideoApi::QueryKind::Search;
    search.keyword = QStringLiteral("合成搜索");
    deliver(VideoCatalogController::Operation::List, category, 1, {"category-1"});
    QVERIFY(controller.categorySnapshot().hasMore);
    QVERIFY(!controller.categorySnapshot().complete);
    deliver(VideoCatalogController::Operation::Search, search, 1, {"search-1"});
    QCOMPARE(controller.categorySnapshot().items.first().id, QStringLiteral("category-1"));
    QCOMPARE(controller.searchSnapshot().items.first().id, QStringLiteral("search-1"));
    deliver(VideoCatalogController::Operation::List, category, 2, {"category-1"});
    QVERIFY(!controller.categorySnapshot().hasMore);
    QVERIFY(!controller.categorySnapshot().complete);
    deliver(VideoCatalogController::Operation::Search, search, 2, {});
    QVERIFY(!controller.searchSnapshot().hasMore);
    QVERIFY(controller.searchSnapshot().complete);
    QCOMPARE(controller.searchSnapshot().items.size(), 1);
    QCOMPARE(controller.categorySnapshot().page, 2);
}

void VideoCatalogTest::duplicateItemsDoNotConsumeCapacity()
{
    QTemporaryDir dir;
    VideoCatalogController controller(pathsFor(dir));
    controller.categoryState.page = 1;
    for (int index = 0; index < 9998; ++index)
        controller.categoryState.items.append(sample(QString::number(index)));
    controller.pending.operation = VideoCatalogController::Operation::List;
    controller.pending.page = 2;
    controller.pending.requestId = 1;
    controller.pending.generation = controller.generation;
    controller.setBusy(true);
    QJsonArray items;
    for (const auto &id : {"0", "1", "new"})
        items.append(QJsonObject{{"id", id}, {"title", "synthetic"}, {"cover", ""}, {"duration", 1}, {"views", 0}});
    controller.receive(1, QJsonDocument(QJsonObject{{"code", 200}, {"data", QJsonObject{{"list", items}}}}).toJson(), {});
    QCOMPARE(controller.categorySnapshot().items.size(), 9999);
    QVERIFY(controller.categorySnapshot().hasMore);
    QVERIFY(!controller.categorySnapshot().complete);
}

void VideoCatalogTest::controllerRejectsLateReplies()
{
    QTemporaryDir dir;
    VideoCatalogController controller(pathsFor(dir));
    controller.categoryState.items = {sample("kept")};
    controller.pending.operation = VideoCatalogController::Operation::List;
    controller.pending.requestId = 2;
    controller.pending.generation = controller.generation;
    controller.setBusy(true);
    const auto empty = QByteArrayLiteral("{\"code\":200,\"data\":{\"list\":[]}}");
    controller.receive(1, empty, {});
    QVERIFY(controller.isBusy());
    QCOMPARE(controller.categorySnapshot().items.first().id, QStringLiteral("kept"));
    controller.cancel();
    controller.receive(2, empty, {});
    QVERIFY(!controller.isBusy());
    QCOMPARE(controller.categorySnapshot().items.first().id, QStringLiteral("kept"));
    controller.pending.requestId = 3;
    controller.pending.generation = controller.generation;
    controller.setBusy(true);
    controller.receive(3, {}, QStringLiteral("合成网络失败"));
    QCOMPARE(controller.categorySnapshot().items.first().id, QStringLiteral("kept"));
    QCOMPARE(controller.categorySnapshot().page, 0);
}

void VideoCatalogTest::refreshedCategoriesDropStaleSelectionOffline()
{
    QTemporaryDir dir;
    const auto paths = pathsFor(dir);
    const QVector<VideoApi::Category> categories{{"p", "P", {{"child", "Child", 0}}}};
    QVERIFY(VideoStore::saveCategories(paths, categories).isEmpty());
    VideoCatalogWidget widget(paths, nullptr);
    auto *controller = widget.findChild<VideoCatalogController *>();
    widget.findChild<QComboBox *>("videoParentCategoryCombo")->setCurrentIndex(1);
    QCOMPARE(controller->categorySnapshot().query.parentId, QStringLiteral("p"));
    controller->pending.operation = VideoCatalogController::Operation::Categories;
    controller->pending.requestId = 1;
    controller->pending.generation = controller->generation;
    controller->setBusy(true);
    controller->receive(1, "{\"code\":200,\"data\":{\"list\":[]}}", {});
    QVERIFY(controller->categorySnapshot().query.parentId.isEmpty());
    QCOMPARE(widget.findChild<QComboBox *>("videoParentCategoryCombo")->count(), 1);
    QVERIFY(!QFileInfo::exists(VideoStore::sessionPath(paths)));
}

void VideoCatalogTest::controllerKeepsLatestDetailsOnReselect()
{
    QTemporaryDir dir;
    VideoCatalogController controller(pathsFor(dir));
    controller.showSavedDetails(sample());
    controller.currentDetails.labels = {QStringLiteral("已获取标签")};
    controller.showSavedDetails(sample());
    QCOMPARE(controller.details().labels, QStringList{QStringLiteral("已获取标签")});
    controller.showSavedDetails(sample("other"));
    QVERIFY(controller.details().labels.isEmpty());
}

void VideoCatalogTest::clientRequiresConsentAndPublicAddresses()
{
    QTemporaryDir dir;
    SyntheticNetwork network;
    VideoClient client(pathsFor(dir), &network, syntheticResolver());
    QSignalSpy finished(&client, &VideoClient::finished);
    client.request(VideoClient::Operation::Categories, {}, 1);
    QCOMPARE(finished.count(), 0);
    QTRY_COMPARE(finished.count(), 1);
    QCOMPARE(network.sent.size(), 0);
    QVERIFY(!QFileInfo::exists(VideoStore::sessionPath(pathsFor(dir))));
    for (const auto &ip : {"127.0.0.1", "10.0.0.1", "169.254.169.254", "100.64.0.1", "192.0.2.1", "::1", "fc00::1", "2001:db8::1"})
        QVERIFY(!VideoClient::isPublicAddress(QHostAddress(QString::fromLatin1(ip))));
    QVERIFY(VideoClient::isPublicAddress(QHostAddress(QStringLiteral("8.8.8.8"))));
    QVERIFY(!VideoClient::isAllowedOrigin(QUrl(QStringLiteral("https://unknown.example.invalid"))));
    QVERIFY(!VideoClient::isAllowedOrigin(QUrl(QStringLiteral("http://0826.aiyejikou0001.com"))));
}

void VideoCatalogTest::clientPinsOriginAndKeepsTokensScoped()
{
    QTemporaryDir dir;
    const auto paths = pathsFor(dir);
    VideoSession session;
    session.uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    session.origin = VideoClient::defaultOrigin();
    session.registrationAttempted = true;
    session.token = "synthetic-token-only";
    QVERIFY(VideoStore::saveSession(paths, session).isEmpty());
    SyntheticNetwork network;
    VideoClient client(paths, &network, syntheticResolver());
    client.setConsentGranted(true);
    QSignalSpy finished(&client, &VideoClient::finished);
    client.request(VideoClient::Operation::Categories, {}, 1);
    QTRY_COMPARE(network.sent.size(), 1);
    const auto request = network.sent.first().request;
    QCOMPARE(request.url().host(), QStringLiteral("8.8.8.8"));
    QCOMPARE(request.peerVerifyName(), session.origin.host());
    QCOMPARE(request.rawHeader("Host"), session.origin.host().toLatin1());
    QVERIFY(request.rawHeader("x-token") == session.token);
    QCOMPARE(request.url().path(), QStringLiteral("/api/vod/type"));
    network.sent.first().reply->respond(200, "{\"code\":200,\"data\":{\"list\":[]}}");
    QTRY_COMPARE(finished.count(), 1);
    QVERIFY(finished.first().at(2).toString().isEmpty());
    auto changed = session;
    changed.origin = QUrl(QStringLiteral("https://0826.aiyejikou0002.com"));
    QVERIFY(!VideoStore::saveSession(paths, changed).isEmpty());
    QTemporaryDir otherDir;
    changed.origin = QUrl(QStringLiteral("https://unknown.example.invalid"));
    QVERIFY(VideoStore::saveSession(pathsFor(otherDir), changed).isEmpty());
    VideoClient other(pathsFor(otherDir), &network, syntheticResolver());
    other.setConsentGranted(true);
    QSignalSpy rejected(&other, &VideoClient::finished);
    other.request(VideoClient::Operation::Categories, {}, 1);
    QTRY_COMPARE(rejected.count(), 1);
    QCOMPARE(network.sent.size(), 1);
}

void VideoCatalogTest::clientRegistersOnceAndStopsOnSaveFailure()
{
    QTemporaryDir dir;
    const auto paths = pathsFor(dir);
    SyntheticNetwork network;
    VideoClient client(paths, &network, syntheticResolver());
    client.setConsentGranted(true);
    QSignalSpy finished(&client, &VideoClient::finished);
    client.request(VideoClient::Operation::Categories, {}, 1);
    QTRY_COMPARE(network.sent.size(), 1);
    QCOMPARE(network.sent.first().request.url().path(), QStringLiteral("/api/user/registeruuid"));
    QVERIFY(network.sent.first().request.rawHeader("x-token").isEmpty());
    const auto registration = QJsonDocument::fromJson(network.sent.first().body).object();
    QCOMPARE(registration.size(), 1);
    VideoSession before;
    QVERIFY(VideoStore::loadSession(paths, before).isEmpty());
    QCOMPARE(registration.value("uuid").toString(), before.uuid);
    QVERIFY(before.registrationAttempted);
    QLockFile lock(VideoStore::sessionPath(paths) + ".lock");
    QVERIFY(lock.tryLock(0));
    network.sent.first().reply->respond(200, "{\"code\":200,\"data\":{\"token\":\"synthetic-new-token\"}}");
    QTRY_COMPARE(finished.count(), 1);
    QVERIFY(!finished.first().at(2).toString().isEmpty());
    QCOMPARE(network.sent.size(), 1);
    lock.unlock();
    client.request(VideoClient::Operation::Session, {}, 1);
    QTRY_COMPARE(finished.count(), 2);
    QCOMPARE(network.sent.size(), 1);
    VideoSession restored;
    QVERIFY(VideoStore::loadSession(paths, restored).isEmpty());
    QCOMPARE(restored.uuid, before.uuid);
    QVERIFY(!restored.token.isEmpty());
}

void VideoCatalogTest::clientRejectsRedirectAndCancelledReplies()
{
    QTemporaryDir dir;
    const auto paths = pathsFor(dir);
    VideoSession session;
    session.uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    session.origin = VideoClient::defaultOrigin();
    session.registrationAttempted = true;
    session.token = "synthetic-token";
    QVERIFY(VideoStore::saveSession(paths, session).isEmpty());
    SyntheticNetwork network;
    VideoClient client(paths, &network, syntheticResolver());
    client.setConsentGranted(true);
    QSignalSpy finished(&client, &VideoClient::finished);
    client.request(VideoClient::Operation::Categories, {}, 1);
    QTRY_COMPARE(network.sent.size(), 1);
    network.sent.last().reply->respond(302, {});
    QTRY_COMPARE(finished.count(), 1);
    QVERIFY(!finished.first().at(2).toString().isEmpty());
    const auto id = client.request(VideoClient::Operation::Categories, {}, 1);
    QTRY_COMPARE(network.sent.size(), 2);
    client.cancel(id);
    QCoreApplication::processEvents();
    QCOMPARE(finished.count(), 1);
    QCOMPARE(network.sent.size(), 2);
}

void VideoCatalogTest::clientRejectsPrivateResolutionAndOversizedBody()
{
    QTemporaryDir dir;
    SyntheticNetwork network;
    VideoClient client(pathsFor(dir), &network,
        [](const QString &, QObject *, VideoClient::AddressCallback callback) {
            callback({QHostAddress(QStringLiteral("127.0.0.1"))});
        });
    client.setConsentGranted(true);
    QSignalSpy finished(&client, &VideoClient::finished);
    client.request(VideoClient::Operation::Categories, {}, 1);
    QTRY_COMPARE(finished.count(), 1);
    QCOMPARE(network.sent.size(), 0);
    QVERIFY(!QFileInfo::exists(VideoStore::sessionPath(pathsFor(dir))));
    VideoClient publicClient(pathsFor(dir), &network, syntheticResolver());
    publicClient.setConsentGranted(true);
    QSignalSpy publicFinished(&publicClient, &VideoClient::finished);
    publicClient.request(VideoClient::Operation::Session, {}, 1);
    QTRY_COMPARE(network.sent.size(), 1);
    network.sent.last().reply->respond(200, QByteArray(64 * 1024 + 1, 'x'));
    QTRY_COMPARE(publicFinished.count(), 1);
    QVERIFY(!publicFinished.first().at(2).toString().isEmpty());
}

void VideoCatalogTest::clientRegistrationLockPreventsDuplicateIdentity()
{
    QTemporaryDir dir;
    const auto paths = pathsFor(dir);
    SyntheticNetwork network;
    VideoClient first(paths, &network, syntheticResolver());
    VideoClient second(paths, &network, syntheticResolver());
    first.setConsentGranted(true);
    second.setConsentGranted(true);
    first.request(VideoClient::Operation::Session, {}, 1);
    QTRY_COMPARE(network.sent.size(), 1);
    VideoSession identity;
    QVERIFY(VideoStore::loadSession(paths, identity).isEmpty());
    QSignalSpy finished(&second, &VideoClient::finished);
    second.request(VideoClient::Operation::Session, {}, 1);
    QTRY_COMPARE(finished.count(), 1);
    QCOMPARE(network.sent.size(), 1);
    VideoSession unchanged;
    QVERIFY(VideoStore::loadSession(paths, unchanged).isEmpty());
    QCOMPARE(unchanged.uuid, identity.uuid);
    QVERIFY(!VideoStore::clearSessionToken(paths, unchanged).isEmpty());
    first.cancelAll();
}

void VideoCatalogTest::clientRejectsTlsFailureAndTimeout()
{
    QTemporaryDir dir;
    const auto paths = pathsFor(dir);
    VideoSession session;
    session.uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    session.origin = VideoClient::defaultOrigin();
    session.registrationAttempted = true;
    session.token = "synthetic-token";
    QVERIFY(VideoStore::saveSession(paths, session).isEmpty());
    SyntheticNetwork network;
    VideoClient client(paths, &network, syntheticResolver());
    client.setConsentGranted(true);
    QSignalSpy finished(&client, &VideoClient::finished);
    client.request(VideoClient::Operation::Categories, {}, 1);
    QTRY_COMPARE(network.sent.size(), 1);
    network.sent.last().reply->fail(QNetworkReply::SslHandshakeFailedError);
    QTRY_COMPARE(finished.count(), 1);
    QVERIFY(!finished.first().at(2).toString().isEmpty());
    client.request(VideoClient::Operation::Categories, {}, 1);
    QTRY_COMPARE(network.sent.size(), 2);
    const auto timers = client.findChildren<QTimer *>();
    auto active = std::find_if(timers.cbegin(), timers.cend(), [](QTimer *timer) { return timer->isActive(); });
    QVERIFY(active != timers.cend());
    QCOMPARE((*active)->interval(), 30000);
    QVERIFY(QMetaObject::invokeMethod(*active, "timeout", Qt::DirectConnection));
    QTRY_COMPARE(finished.count(), 2);
    QVERIFY(!finished.last().at(2).toString().isEmpty());
    QCOMPARE(network.sent.size(), 2);
    VideoSession preserved;
    QVERIFY(VideoStore::loadSession(paths, preserved).isEmpty());
    QVERIFY(preserved.token == session.token);
}

void VideoCatalogTest::corruptSessionIsNotRecreated()
{
    QTemporaryDir dir;
    const auto paths = pathsFor(dir);
    VideoSession session;
    session.uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QVERIFY(VideoStore::saveSession(paths, session).isEmpty());
    const auto path = VideoStore::sessionPath(paths);
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("broken-session"), 14);
    file.close();
    SyntheticNetwork network;
    VideoClient client(paths, &network, syntheticResolver());
    client.setConsentGranted(true);
    QSignalSpy finished(&client, &VideoClient::finished);
    client.request(VideoClient::Operation::Session, {}, 1);
    QTRY_COMPARE(finished.count(), 1);
    QCOMPARE(network.sent.size(), 0);
    QCOMPARE(readBytes(path), QByteArrayLiteral("broken-session"));
}

void VideoCatalogTest::controllerDoesNotCommitFavoriteOnFailure()
{
    QTemporaryDir dir;
    const auto paths = pathsFor(dir);
    VideoCatalogController controller(paths);
    QVERIFY(controller.setFavorite(sample(), true).isEmpty());
    const auto before = readBytes(VideoStore::favoritesPath(paths));
    QLockFile lock(VideoStore::favoritesPath(paths) + ".lock");
    QVERIFY(lock.tryLock(0));
    QSignalSpy changes(&controller, &VideoCatalogController::favoritesChanged);
    QVERIFY(!controller.setFavorite(sample(), false).isEmpty());
    QVERIFY(controller.isFavorite(sample().id));
    QCOMPARE(changes.count(), 0);
    QCOMPARE(readBytes(VideoStore::favoritesPath(paths)), before);
}

void VideoCatalogTest::delegateOnlyFavoritesInsideHitArea()
{
    QTemporaryDir dir;
    VideoCoverCache covers(pathsFor(dir));
    VideoListModel model(&covers);
    model.setItems({sample()});
    VideoCardDelegate delegate;
    QSignalSpy favorite(&delegate, &VideoCardDelegate::favoriteRequested);
    QStyleOptionViewItem option;
    option.rect = QRect(0, 0, 600, 126);
    QMouseEvent outside(QEvent::MouseButtonRelease, QPointF(200, 20), QPointF(200, 20),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QVERIFY(!delegate.editorEvent(&outside, &model, option, model.index(0)));
    QCOMPARE(favorite.count(), 0);
    QMouseEvent inside(QEvent::MouseButtonRelease, QPointF(550, 95), QPointF(550, 95),
                       Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QVERIFY(delegate.editorEvent(&inside, &model, option, model.index(0)));
    QCOMPARE(favorite.count(), 1);
}

void VideoCatalogTest::largeModelDoesNotCreateRowWidgets()
{
    QTemporaryDir dir;
    VideoCoverCache covers(pathsFor(dir));
    VideoListModel model(&covers);
    QVector<VideoApi::Summary> items;
    for (int row = 0; row < VideoApi::MaxItems; ++row)
        items.append(sample(QString::number(row)));
    QListView view;
    view.setItemDelegate(new VideoCardDelegate(&view));
    view.setModel(&model);
    const auto widgets = view.findChildren<QWidget *>().size();
    model.setItems(items);
    QCOMPARE(model.rowCount(), VideoApi::MaxItems);
    QCOMPARE(view.findChildren<QWidget *>().size(), widgets);
    QVERIFY(!view.indexWidget(model.index(0)));
}

void VideoCatalogTest::modelUsesLocalFavoritesAndPlainText()
{
    QTemporaryDir dir;
    VideoCoverCache covers(pathsFor(dir));
    VideoListModel model(&covers);
    model.setItems({sample("A"), sample("B")});
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.data(model.index(0), VideoListModel::TitleRole).toString(), sample().title);
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    model.setFavorites({"B"});
    QVERIFY(!model.data(model.index(0), VideoListModel::FavoriteRole).toBool());
    QVERIFY(model.data(model.index(1), VideoListModel::FavoriteRole).toBool());
    QCOMPARE(changed.count(), 1);
    QCOMPARE(changed.first().at(0).value<QModelIndex>().row(), 1);
    QVERIFY(model.data(model.index(0), VideoListModel::ThumbnailRole).value<QImage>().isNull());
}

void VideoCatalogTest::widgetStartsOfflineAndKeepsLocalDetails()
{
    QTemporaryDir dir;
    const auto paths = pathsFor(dir);
    QVector<VideoApi::Summary> saved;
    QVERIFY(VideoStore::updateFavorite(paths, sample(), true, saved).isEmpty());
    VideoCatalogWidget widget(paths, nullptr);
    auto *controller = widget.findChild<VideoCatalogController *>();
    auto *stack = widget.findChild<QStackedWidget *>("videoWorkspaceStack");
    auto *favorites = widget.findChild<QListView *>("videoFavoritesView");
    auto *details = widget.findChild<QPlainTextEdit *>("videoDetailsView");
    QVERIFY(controller && stack && favorites && details);
    QSignalSpy busy(controller, &VideoCatalogController::busyChanged);
    QSignalSpy consent(controller, &VideoCatalogController::consentRequired);
    widget.setPage(VideoCatalogWidget::Page::Favorites);
    QCOMPARE(stack->currentIndex(), 1);
    QCOMPARE(favorites->model()->rowCount(), 1);
    favorites->setCurrentIndex(favorites->model()->index(0, 0));
    QVERIFY(details->toPlainText().contains(QStringLiteral("<b>合成视频</b>")));
    widget.findChild<QLineEdit *>("videoFavoritesFilterEdit")->setText(QStringLiteral("不匹配"));
    QCOMPARE(favorites->model()->rowCount(), 0);
    QCOMPARE(controller->favorites().size(), 1);
    widget.setPage(VideoCatalogWidget::Page::Connection);
    widget.setPage(VideoCatalogWidget::Page::Categories);
    QCOMPARE(busy.count(), 0);
    QCOMPARE(consent.count(), 0);
    QVERIFY(!QFileInfo::exists(VideoStore::sessionPath(paths)));
    QCOMPARE(widget.findChild<QLabel *>("videoConnectionStatusLabel")->textFormat(), Qt::PlainText);
}

void VideoCatalogTest::categoryOrderCancelDoesNotWrite()
{
    QTemporaryDir dir;
    const auto paths = pathsFor(dir);
    const QVector<VideoApi::Category> categories{{"p1", "P1", {{"a", "A", 0}, {"b", "B", 0}}},
                                                 {"p2", "P2", {{"c", "C", 0}}}};
    QVERIFY(VideoStore::saveCategories(paths, categories).isEmpty());
    VideoCatalogWidget widget(paths, nullptr);
    bool inspected = false;
    QTimer::singleShot(0, &widget, [&] {
        auto *dialog = widget.findChild<QDialog *>("videoCategoryOrderDialog");
        if (!dialog)
            return;
        auto *tree = dialog->findChild<QTreeWidget *>("videoCategoryOrderTree");
        inspected = tree && tree->topLevelItemCount() == 2 && !tree->dragEnabled();
        dialog->reject();
    });
    widget.findChild<QPushButton *>("videoCategoryOrderButton")->click();
    QVERIFY(inspected);
    QVERIFY(!QFileInfo::exists(VideoStore::orderPath(paths)));
    QCOMPARE(widget.findChild<QComboBox *>("videoParentCategoryCombo")->count(), 3);
}

void VideoCatalogTest::categoryOrderMovesWithinParentAndSaves()
{
    QTemporaryDir dir;
    const auto paths = pathsFor(dir);
    const QVector<VideoApi::Category> source{{"p", "Parent", {{"a", "A", 0}, {"b", "B", 0}}}};
    QVERIFY(VideoStore::saveCategories(paths, source).isEmpty());
    VideoCatalogWidget widget(paths, nullptr);
    bool stayedUnderParent = false;
    QTimer::singleShot(0, &widget, [&] {
        auto *dialog = widget.findChild<QDialog *>("videoCategoryOrderDialog");
        if (!dialog)
            return;
        auto *tree = dialog->findChild<QTreeWidget *>("videoCategoryOrderTree");
        auto *parent = tree->topLevelItem(0);
        auto *child = parent->child(1);
        tree->setCurrentItem(child);
        dialog->findChild<QPushButton *>("videoCategoryMoveUpButton")->click();
        stayedUnderParent = child->parent() == parent && parent->child(0) == child;
        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
        if (dialog->isVisible())
            dialog->reject();
    });
    widget.findChild<QPushButton *>("videoCategoryOrderButton")->click();
    QVERIFY(stayedUnderParent);
    VideoCategoryOrder restored;
    QVERIFY(VideoStore::loadOrder(paths, restored).isEmpty());
    QCOMPARE(restored.children.value("p"), QStringList({"b", "a"}));
    QCOMPARE(widget.findChild<VideoCatalogController *>()->categories().first().children.first().id, QStringLiteral("a"));
}

void VideoCatalogTest::widgetKeepsCategoryAndSearchSelections()
{
    QTemporaryDir dir;
    VideoCatalogWidget widget(pathsFor(dir), nullptr);
    auto *controller = widget.findChild<VideoCatalogController *>();
    controller->categoryState.items = {sample("category-A"), sample("category-B")};
    controller->searchState.items = {sample("search-A"), sample("search-B")};
    emit controller->categoryResultsChanged();
    emit controller->searchResultsChanged();
    auto *category = widget.findChild<QListView *>("videoResultsView");
    auto *search = widget.findChild<QListView *>("videoSearchResultsView");
    auto *tabs = widget.findChild<QTabWidget *>("videoResultsTabs");
    category->setCurrentIndex(category->model()->index(1, 0));
    QCOMPARE(controller->details().id, QStringLiteral("category-B"));
    tabs->setCurrentIndex(1);
    search->setCurrentIndex(search->model()->index(0, 0));
    QCOMPARE(controller->details().id, QStringLiteral("search-A"));
    tabs->setCurrentIndex(0);
    QCOMPARE(category->currentIndex().row(), 1);
    QCOMPARE(controller->details().id, QStringLiteral("category-B"));
    QCOMPARE(search->currentIndex().row(), 0);
}

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    VideoCatalogTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "tst_videocatalog.moc"
