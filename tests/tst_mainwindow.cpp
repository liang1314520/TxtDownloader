#include "mainwindow.h"
#include "booklistcache.h"
#include "bookdownloadcontroller.h"
#include "bookdetaildialog.h"
#include "sessionprofile.h"
#include "pcaprequestreader.h"
#include "mumucapture.h"
#include "mumucapturedialog.h"
#include "mumudiscovery.h"
#include "aesecb.h"

#include <windows.h>
#include <wincrypt.h>

#include <QCheckBox>

#include <QtEndian>

#include <QTableWidget>

#include <QComboBox>
#include <QInputDialog>
#include <QMap>
#include <QPlainTextEdit>
#include <QStandardPaths>
#include <QScopeGuard>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <QApplication>
#include <QFile>
#include <QFileDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

namespace {

QByteArray encryptedSample()
{
    return QByteArray::fromHex(
        "9d1a2433cc11719bbceca20e6b8cc9abe9fab02d3b530f4cf2b3f4b69aa8c48c"
        "17633df7684baa527dcd5c1f28e417b4");
}

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

auto preserveCategoryFiles(const QVector<NovelCategory> &categories)
{
    QStringList paths{BookListCache::categoriesPath()};
    for (const auto &category : categories)
        paths.append(BookListCache::defaultPath(category));
    QMap<QString, QByteArray> previous;
    for (const auto &path : paths) {
        if (QFile::exists(path))
            previous.insert(path, readFile(path));
    }
    return qScopeGuard([paths, previous] {
        for (const auto &path : paths) {
            if (previous.contains(path))
                writeFile(path, previous.value(path));
            else
                QFile::remove(path);
        }
    });
}

}

class MainWindowTest : public QObject {
    Q_OBJECT

private slots:
    void startsWithDisabledAction();
    void derivesDefaultOutput();
    void selectsInputThroughDialog();
    void selectsOutputThroughDialog();
    void convertsWithoutPreview();
    void cancelKeepsExistingOutput();
    void confirmOverwritesOutput();
    void invalidFileReportsError();
    void cachesBookMetadataWithoutCredentials();
    void rejectsCorruptBookCache();
    void detailShowsPlainTextAndKeepsChapterPreview();
    void normalizesCatalogBeforePreview_data();
    void normalizesCatalogBeforePreview();
    void searchDoesNotDiscardGroupCache();
    void cachedChapterCountsSortNumerically();
    void rejectsOtherGroupCache();
    void searchesOnlyCachedGroupByDefault();
    void protectsSessionProfile();
    void rejectsInvalidSessionProfile();
    void clearsSessionWithoutChangingDeviceIdentity();
    void parsesNormalSessionToken();
    void rejectsSessionProfileBoundaries();
    void restoresSessionWithoutCapture();
    void rejectsLoginWithoutIdentity();
    void preservesSessionAfterBadCapture();
    void keepsDeviceMarkerAtEnd();
    void extractsTargetRequestFromPcap();
    void rejectsUnsafePcapRequests();
    void drainsBufferedPcapRequests();
    void parsesMuMuTraceState();
    void parsesMuMuEnabledTraceState();
    void rejectsIncompleteMuMuTraceState();
    void refusesInvalidCapturePaths();
    void requiresCaptureConsent();
    void extractsCategoryConditions();
    void rejectsAmbiguousCategoryConditions();
    void persistsCategoriesWithoutSession();
    void isolatesCategoryBookCaches();
    void readsLegacyDefaultCategoryCache();
    void pairsDetectedMuMuInstancesByVersion();
    void rejectsInvalidMuMuInstances();
    void rejectsNonlocalMuMuRoots();
    void rejectsMuMuRootsWithLinkedAncestor();
    void switchesCategoryCachesWithoutNetwork();
    void rejectsDuplicateCategories();
    void categoryCaptureKeepsConsentExplicit();
    void identifiesCategoryRequestWithoutChangingSession();
    void addsCategoryWithoutStartingNetwork();
    void preservesCorruptCategoryConfiguration();
    void renamesCategoryWithoutDiscardingCache_data();
    void renamesCategoryWithoutDiscardingCache();
    void rejectsInvalidCategoryRenames();
    void removesCategoriesWithoutDeletingCaches();
    void preservesCategoriesWhenManagementCannotSave_data();
    void preservesCategoriesWhenManagementCannotSave();
    void categoryManagementDialogsPreserveState();
};

void MainWindowTest::startsWithDisabledAction()
{
    MainWindow window;
    auto *button = window.findChild<QPushButton *>("decodeButton");
    QVERIFY(button);
    QVERIFY(!button->isEnabled());
    QVERIFY(window.windowTitle().contains(QStringLiteral("NOV")));
}

void MainWindowTest::derivesDefaultOutput()
{
    MainWindow window;
    auto *input = window.findChild<QLineEdit *>("inputPathEdit");
    auto *output = window.findChild<QLineEdit *>("outputPathEdit");
    auto *button = window.findChild<QPushButton *>("decodeButton");
    QVERIFY(input && output && button);
    auto *key = window.findChild<QLineEdit *>("keyEdit");
    QVERIFY(key);
    key->setText(QStringLiteral("0123456789abcdef"));
    input->setText("E:/chapters/chapter29.nov");
    QCOMPARE(output->text(), QStringLiteral("E:/chapters/chapter29.txt"));
    QVERIFY(button->isEnabled());
    input->clear();
    QVERIFY(!button->isEnabled());
}

void MainWindowTest::selectsInputThroughDialog()
{
    QTemporaryDir dir;
    const auto path = dir.filePath("chapter.nov");
    QVERIFY(writeFile(path, encryptedSample()));
    MainWindow window;
    auto *button = window.findChild<QPushButton *>("chooseInputButton");
    auto *input = window.findChild<QLineEdit *>("inputPathEdit");
    QVERIFY(button && input);
    QTimer::singleShot(0, &window, [&] {
        auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
        QVERIFY(dialog);
        dialog->selectFile(path);
        QVERIFY(QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection));
    });
    button->click();
    QCOMPARE(input->text(), path);
}

void MainWindowTest::selectsOutputThroughDialog()
{
    QTemporaryDir dir;
    const auto path = dir.filePath("export.txt");
    MainWindow window;
    auto *button = window.findChild<QPushButton *>("chooseOutputButton");
    auto *output = window.findChild<QLineEdit *>("outputPathEdit");
    QVERIFY(button && output);
    QTimer::singleShot(0, &window, [&] {
        auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
        QVERIFY(dialog);
        dialog->selectFile(path);
        QVERIFY(QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection));
    });
    button->click();
    QCOMPARE(output->text(), path);
}

void MainWindowTest::convertsWithoutPreview()
{
    QTemporaryDir dir;
    const auto inputPath = dir.filePath("chapter.nov");
    QVERIFY(writeFile(inputPath, encryptedSample()));
    MainWindow window;
    auto *input = window.findChild<QLineEdit *>("inputPathEdit");
    auto *status = window.findChild<QLabel *>("statusLabel");
    auto *button = window.findChild<QPushButton *>("decodeButton");
    QVERIFY(input && status && button);
    auto *key = window.findChild<QLineEdit *>("keyEdit");
    QVERIFY(key);
    key->setText(QStringLiteral("0123456789abcdef"));
    input->setText(inputPath);
    button->click();
    QCOMPARE(readFile(dir.filePath("chapter.txt")),
             QStringLiteral("离线解码测试\r\n第二行。\r\n").toUtf8());
    QVERIFY(status->text().contains(QStringLiteral("已保存")));
    QVERIFY(!status->text().contains(QStringLiteral("第二行")));
    QCOMPARE(readFile(inputPath), encryptedSample());
    QVERIFY(button->isEnabled());
}

void MainWindowTest::cancelKeepsExistingOutput()
{
    QTemporaryDir dir;
    const auto inputPath = dir.filePath("chapter.nov");
    const auto outputPath = dir.filePath("chapter.txt");
    QVERIFY(writeFile(inputPath, encryptedSample()));
    QVERIFY(writeFile(outputPath, "keep this"));
    MainWindow window;
    auto *input = window.findChild<QLineEdit *>("inputPathEdit");
    auto *button = window.findChild<QPushButton *>("decodeButton");
    auto *status = window.findChild<QLabel *>("statusLabel");
    QVERIFY(input && button && status);
    auto *key = window.findChild<QLineEdit *>("keyEdit");
    QVERIFY(key);
    key->setText(QStringLiteral("0123456789abcdef"));
    input->setText(inputPath);
    QTimer::singleShot(0, &window, [] {
        auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        QVERIFY(dialog);
        QCOMPARE(dialog->defaultButton(), dialog->button(QMessageBox::No));
        dialog->button(QMessageBox::No)->click();
    });
    button->click();
    QCOMPARE(readFile(outputPath), QByteArray("keep this"));
    QVERIFY(status->text().contains(QStringLiteral("已取消")));
}

void MainWindowTest::confirmOverwritesOutput()
{
    QTemporaryDir dir;
    const auto inputPath = dir.filePath("chapter.nov");
    const auto outputPath = dir.filePath("chapter.txt");
    QVERIFY(writeFile(inputPath, encryptedSample()));
    QVERIFY(writeFile(outputPath, "old text"));
    MainWindow window;
    auto *input = window.findChild<QLineEdit *>("inputPathEdit");
    auto *button = window.findChild<QPushButton *>("decodeButton");
    QVERIFY(input && button);
    auto *key = window.findChild<QLineEdit *>("keyEdit");
    QVERIFY(key);
    key->setText(QStringLiteral("0123456789abcdef"));
    input->setText(inputPath);
    QTimer::singleShot(0, &window, [] {
        auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        QVERIFY(dialog);
        dialog->button(QMessageBox::Yes)->click();
    });
    button->click();
    QCOMPARE(readFile(outputPath), QStringLiteral("离线解码测试\r\n第二行。\r\n").toUtf8());
    QCOMPARE(readFile(inputPath), encryptedSample());
}

void MainWindowTest::invalidFileReportsError()
{
    QTemporaryDir dir;
    const auto inputPath = dir.filePath("invalid.bin");
    QVERIFY(writeFile(inputPath, "broken file"));
    MainWindow window;
    auto *input = window.findChild<QLineEdit *>("inputPathEdit");
    auto *status = window.findChild<QLabel *>("statusLabel");
    auto *button = window.findChild<QPushButton *>("decodeButton");
    QVERIFY(input && status && button);
    auto *key = window.findChild<QLineEdit *>("keyEdit");
    QVERIFY(key);
    key->setText(QStringLiteral("0123456789abcdef"));
    input->setText(inputPath);
    button->click();
    QVERIFY(status->text().contains(QStringLiteral("未保存")));
    QVERIFY(!QFile::exists(dir.filePath("invalid.txt")));
    QVERIFY(button->isEnabled());
}

void MainWindowTest::cachesBookMetadataWithoutCredentials()
{
    QTemporaryDir dir;
    BookListSnapshot snapshot;
    snapshot.page = 26;
    snapshot.hasMore = false;
    snapshot.complete = true;
    snapshot.updatedAt = QDateTime::currentDateTimeUtc();
    NovelSearchBook book;
    book.id = QStringLiteral("0123456789abcdef0123456789abcdef");
    book.name = QStringLiteral("合成书籍");
    book.chapterCount = QStringLiteral("130");
    book.description = QStringLiteral("用于缓存往返校验的合成简介。\n第二行。");
    snapshot.books.append(book);
    const auto path = dir.filePath(QStringLiteral("group.json"));
    QVERIFY(BookListCache::save(path, snapshot).isEmpty());
    const auto raw = readFile(path);
    const auto object = QJsonDocument::fromJson(raw).object();
    QCOMPARE(object.size(), 9);
    QCOMPARE(object.value(QStringLiteral("tag_id")).toString(), QStringLiteral("5,10,23"));
    QVERIFY(BookListCache::defaultPath().contains(QStringLiteral("group-5-10-23")));
    QVERIFY(!object.contains(QStringLiteral("token")));
    QVERIFY(!object.contains(QStringLiteral("deviceId")));
    QCOMPARE(object.value(QStringLiteral("books")).toArray().first().toObject().size(), 6);
    BookListSnapshot restored;
    QVERIFY(BookListCache::load(path, restored).isEmpty());
    QCOMPARE(restored.page, 26);
    QVERIFY(restored.complete);
    QVERIFY(!restored.hasMore);
    QCOMPARE(restored.books.size(), 1);
    QCOMPARE(restored.books.first().description, book.description);
    QCOMPARE(restored.books.first().chapterCount, QStringLiteral("130"));
}

void MainWindowTest::rejectsCorruptBookCache()
{
    QTemporaryDir dir;
    const auto path = dir.filePath(QStringLiteral("group.json"));
    QVERIFY(writeFile(path, "{broken"));
    BookListSnapshot restored;
    QVERIFY(!BookListCache::load(path, restored).isEmpty());
    QVERIFY(restored.books.isEmpty());
}

void MainWindowTest::detailShowsPlainTextAndKeepsChapterPreview()
{
    NovelSearchBook item;
    item.name = QStringLiteral("合成书名");
    item.description = QStringLiteral("<b>简介按纯文本展示</b>");
    BookDetailDialog dialog(item);
    auto *description = dialog.findChild<QPlainTextEdit *>(QStringLiteral("bookDescriptionView"));
    auto *body = dialog.findChild<QPlainTextEdit *>(QStringLiteral("chapterPreviewView"));
    auto *chapters = dialog.findChild<QComboBox *>(QStringLiteral("previewChapterCombo"));
    QVERIFY(description && body && chapters);
    QCOMPARE(description->toPlainText(), item.description);
    NovelBook book;
    for (int i = 0; i < 4; ++i) {
        NovelChapter chapter;
        chapter.name = QStringLiteral("第%1章").arg(i + 1);
        book.chapters.append(chapter);
    }
    dialog.setCatalog(book);
    QCOMPARE(chapters->count(), 3);
    dialog.setChapter(0, QStringLiteral("合成正文甲"), QStringLiteral("已加载"));
    dialog.setChapter(1, QStringLiteral("合成正文乙"), QStringLiteral("已加载"));
    chapters->setCurrentIndex(1);
    QCOMPARE(body->toPlainText(), QStringLiteral("合成正文乙"));
    chapters->setCurrentIndex(0);
    QCOMPARE(body->toPlainText(), QStringLiteral("合成正文甲"));
}

void MainWindowTest::normalizesCatalogBeforePreview_data()
{
    QTest::addColumn<QStringList>("input");
    QTest::addColumn<QStringList>("expected");
    QTest::newRow("descending-numbers")
        << QStringList{QStringLiteral("第12章"), QStringLiteral("第10章"), QStringLiteral("第3章"),
                       QStringLiteral("第2章"), QStringLiteral("第1章")}
        << QStringList{QStringLiteral("第1章"), QStringLiteral("第2章"), QStringLiteral("第3章"),
                       QStringLiteral("第10章"), QStringLiteral("第12章")};
    QTest::newRow("descending-with-unnumbered-entries")
        << QStringList{QStringLiteral("尾声"), QStringLiteral("第3章"), QStringLiteral("第2章"),
                       QStringLiteral("第1章"), QStringLiteral("序章")}
        << QStringList{QStringLiteral("序章"), QStringLiteral("第1章"), QStringLiteral("第2章"),
                       QStringLiteral("第3章"), QStringLiteral("尾声")};
    QTest::newRow("descending-with-leading-whitespace")
        << QStringList{QStringLiteral("　 第3章"), QStringLiteral(" 第2章"), QStringLiteral("第1章")}
        << QStringList{QStringLiteral("第1章"), QStringLiteral(" 第2章"), QStringLiteral("　 第3章")};
    QTest::newRow("descending-chinese-numbers")
        << QStringList{QStringLiteral("第一百零一章"), QStringLiteral("第一百章"), QStringLiteral("第二十一章"),
                       QStringLiteral("第二十章"), QStringLiteral("第十一章"), QStringLiteral("第十章"),
                       QStringLiteral("第三章"), QStringLiteral("第二章"), QStringLiteral("第一章")}
        << QStringList{QStringLiteral("第一章"), QStringLiteral("第二章"), QStringLiteral("第三章"),
                       QStringLiteral("第十章"), QStringLiteral("第十一章"), QStringLiteral("第二十章"),
                       QStringLiteral("第二十一章"), QStringLiteral("第一百章"), QStringLiteral("第一百零一章")};
    QTest::newRow("descending-positional-chinese-numbers")
        << QStringList{QStringLiteral("第一〇二章"), QStringLiteral("第一〇一章"), QStringLiteral("第一〇〇章"),
                       QStringLiteral("第九九章"), QStringLiteral("第一二章"), QStringLiteral("第一一章"),
                       QStringLiteral("第一〇章"), QStringLiteral("第九章")}
        << QStringList{QStringLiteral("第九章"), QStringLiteral("第一〇章"), QStringLiteral("第一一章"),
                       QStringLiteral("第一二章"), QStringLiteral("第九九章"), QStringLiteral("第一〇〇章"),
                       QStringLiteral("第一〇一章"), QStringLiteral("第一〇二章")};
    QTest::newRow("descending-chapter-ranges")
        << QStringList{QStringLiteral("第276-277章"), QStringLiteral("第274-275章"),
                       QStringLiteral("第272-273章"), QStringLiteral("第270-271章")}
        << QStringList{QStringLiteral("第270-271章"), QStringLiteral("第272-273章"),
                       QStringLiteral("第274-275章"), QStringLiteral("第276-277章")};
    const QStringList ascending{QStringLiteral("序章"), QStringLiteral("第1章"), QStringLiteral("第2章"),
                                QStringLiteral("第3章"), QStringLiteral("尾声")};
    QTest::newRow("already-ascending") << ascending << ascending;
    const QStringList ambiguous{QStringLiteral("上篇"), QStringLiteral("中篇"), QStringLiteral("下篇")};
    QTest::newRow("unknown-order-is-preserved") << ambiguous << ambiguous;
    const QStringList multipleVolumes{QStringLiteral("第1章 上卷"), QStringLiteral("第2章 上卷"),
                                      QStringLiteral("第1章 下卷"), QStringLiteral("第2章 下卷")};
    QTest::newRow("volume-number-reset-is-preserved") << multipleVolumes << multipleVolumes;
}

void MainWindowTest::normalizesCatalogBeforePreview()
{
    QFETCH(QStringList, input);
    QFETCH(QStringList, expected);
    NovelBook book;
    for (int index = 0; index < input.size(); ++index) {
        NovelChapter chapter;
        chapter.id = QStringLiteral("%1").arg(index + 1, 32, 16, QLatin1Char('0'));
        chapter.name = input[index];
        chapter.canRead = index % 2 == 0;
        book.chapters.append(chapter);
    }
    book.normalizeChapterOrder();
    QCOMPARE(book.chapters.size(), expected.size());
    for (int index = 0; index < expected.size(); ++index) {
        const auto sourceIndex = input.indexOf(expected[index]);
        QCOMPARE(book.chapters[index].name, expected[index]);
        QCOMPARE(book.chapters[index].id,
                 QStringLiteral("%1").arg(sourceIndex + 1, 32, 16, QLatin1Char('0')));
        QCOMPARE(book.chapters[index].canRead, sourceIndex % 2 == 0);
    }
    book.normalizeChapterOrder();
    for (int index = 0; index < expected.size(); ++index)
        QCOMPARE(book.chapters[index].name, expected[index]);
    BookDetailDialog dialog(NovelSearchBook{});
    dialog.setCatalog(book);
    auto *chapters = dialog.findChild<QComboBox *>(QStringLiteral("previewChapterCombo"));
    auto *body = dialog.findChild<QPlainTextEdit *>(QStringLiteral("chapterPreviewView"));
    QVERIFY(chapters && body);
    QCOMPARE(chapters->count(), qMin(3, static_cast<int>(expected.size())));
    for (int index = 0; index < chapters->count(); ++index) {
        QCOMPARE(chapters->itemText(index), expected[index]);
        dialog.setChapter(index, book.chapters[index].id, QStringLiteral("已加载"));
        chapters->setCurrentIndex(index);
        QCOMPARE(body->toPlainText(), book.chapters[index].id);
    }
}

void MainWindowTest::searchDoesNotDiscardGroupCache()
{
    BookListSnapshot snapshot;
    snapshot.page = 2;
    snapshot.complete = true;
    snapshot.updatedAt = QDateTime::currentDateTimeUtc();
    NovelSearchBook book;
    book.id = QStringLiteral("0123456789abcdef0123456789abcdef");
    book.name = QStringLiteral("缓存中的合成书籍");
    snapshot.books.append(book);
    QVERIFY(BookListCache::save(BookListCache::defaultPath(), snapshot).isEmpty());
    BookDownloadController controller;
    QCOMPARE(controller.searchResults().size(), 1);
    controller.invalidateSearch();
    controller.invalidate(QStringLiteral("missing-capture.hcy"));
    controller.browseGroup(QStringLiteral("missing-capture.hcy"));
    QVERIFY(!controller.isBusy());
    QVERIFY(controller.searchComplete());
    QCOMPARE(controller.searchPage(), 2);
    QCOMPARE(controller.searchResults().first().id, book.id);
}

void MainWindowTest::cachedChapterCountsSortNumerically()
{
    BookListSnapshot snapshot;
    snapshot.page = 1;
    snapshot.complete = true;
    snapshot.updatedAt = QDateTime::currentDateTimeUtc();
    for (int count : {130, 17, 10}) {
        NovelSearchBook book;
        book.id = QStringLiteral("%1").arg(count, 32, 16, QLatin1Char('0'));
        book.name = QStringLiteral("合成书籍%1").arg(count);
        book.chapterCount = QString::number(count);
        snapshot.books.append(book);
    }
    QVERIFY(BookListCache::save(BookListCache::defaultPath(), snapshot).isEmpty());
    MainWindow window;
    auto *table = window.findChild<QTableWidget *>(QStringLiteral("bookSearchResultsTable"));
    QVERIFY(table);
    table->sortItems(2, Qt::AscendingOrder);
    QCOMPARE(table->item(0, 2)->text(), QStringLiteral("10"));
    QCOMPARE(table->item(1, 2)->text(), QStringLiteral("17"));
    QCOMPARE(table->item(2, 2)->text(), QStringLiteral("130"));
    table->sortItems(2, Qt::DescendingOrder);
    QCOMPARE(table->item(0, 2)->text(), QStringLiteral("130"));
    QCOMPARE(table->item(2, 2)->text(), QStringLiteral("10"));
}

void MainWindowTest::rejectsOtherGroupCache()
{
    QTemporaryDir dir;
    BookListSnapshot snapshot;
    snapshot.page = 1;
    snapshot.complete = true;
    snapshot.updatedAt = QDateTime::currentDateTimeUtc();
    const auto path = dir.filePath(QStringLiteral("other-group.json"));
    QVERIFY(BookListCache::save(path, snapshot).isEmpty());
    auto object = QJsonDocument::fromJson(readFile(path)).object();
    object.insert(QStringLiteral("tag_id"), QStringLiteral("80"));
    QVERIFY(writeFile(path, QJsonDocument(object).toJson(QJsonDocument::Compact)));
    BookListSnapshot restored;
    QVERIFY(!BookListCache::load(path, restored).isEmpty());
    QCOMPARE(restored.page, 0);
}

void MainWindowTest::searchesOnlyCachedGroupByDefault()
{
    BookListSnapshot snapshot;
    snapshot.page = 2;
    snapshot.complete = true;
    snapshot.updatedAt = QDateTime::currentDateTimeUtc();
    for (int i = 1; i <= 2; ++i) {
        NovelSearchBook book;
        book.id = QStringLiteral("%1").arg(i, 32, 16, QLatin1Char('0'));
        book.name = i == 1 ? QStringLiteral("分类内目标") : QStringLiteral("分类内其他");
        snapshot.books.append(book);
    }
    QVERIFY(BookListCache::save(BookListCache::defaultPath(), snapshot).isEmpty());
    MainWindow window;
    auto *input = window.findChild<QLineEdit *>(QStringLiteral("bookSearchKeywordsEdit"));
    auto *search = window.findChild<QPushButton *>(QStringLiteral("searchBooksButton"));
    auto *globalSearch = window.findChild<QPushButton *>(QStringLiteral("globalBookSearchButton"));
    auto *table = window.findChild<QTableWidget *>(QStringLiteral("bookSearchResultsTable"));
    QVERIFY(input && search && globalSearch && table);
    input->setText(QStringLiteral("目标"));
    QVERIFY(search->isEnabled());
    QVERIFY(!globalSearch->isEnabled());
    search->click();
    QCOMPARE(table->rowCount(), 2);
    int visible = 0;
    for (int row = 0; row < table->rowCount(); ++row) {
        if (!table->isRowHidden(row)) {
            ++visible;
            QCOMPARE(table->item(row, 0)->text(), QStringLiteral("分类内目标"));
        }
    }
    QCOMPARE(visible, 1);
}

void MainWindowTest::protectsSessionProfile()
{
    QTemporaryDir dir;
    SessionProfile profile;
    profile.deviceId = QStringLiteral("synthetic-device-onlyP");
    profile.token = QStringLiteral("synthetic-session-token_123");
    profile.accountSession = true;
    profile.expiresAt = QDateTime::currentDateTimeUtc().addDays(1);
    profile.headers = {{"version", "4.0.9"}, {"devicetype", "android"},
                       {"user-agent", "SyntheticTest/1"}};
    const auto path = dir.filePath(QStringLiteral("session.dat"));
    QVERIFY(SessionStore::save(path, profile).isEmpty());
    const auto raw = readFile(path);
    QVERIFY(!raw.contains(profile.token.toUtf8()));
    QVERIFY(!raw.contains(profile.deviceId.toUtf8()));
    SessionProfile restored;
    QVERIFY(SessionStore::load(path, restored).isEmpty());
    QCOMPARE(restored.deviceId, profile.deviceId);
    QCOMPARE(restored.token, profile.token);
    QCOMPARE(restored.headers, profile.headers);
    QVERIFY(restored.accountSession);
    QVERIFY(restored.hasSession());
    QCOMPARE(restored.expiresAt, profile.expiresAt);
}

void MainWindowTest::rejectsInvalidSessionProfile()
{
    QTemporaryDir dir;
    const auto path = dir.filePath(QStringLiteral("session.dat"));
    QVERIFY(writeFile(path, "not-dpapi-data"));
    SessionProfile profile;
    profile.deviceId = QStringLiteral("keep-synthetic-device");
    QVERIFY(!SessionStore::load(path, profile).isEmpty());
    QCOMPARE(profile.deviceId, QStringLiteral("keep-synthetic-device"));
    profile.token = QStringLiteral("invalid\nheader");
    profile.headers = {{"version", "4.0.9"}, {"devicetype", "android"}, {"user-agent", "Test/1"}};
    QVERIFY(!SessionStore::save(path, profile).isEmpty());
    profile.token.clear();
    profile.headers.insert("authorization", "unexpected");
    QVERIFY(!SessionStore::save(path, profile).isEmpty());
    QCOMPARE(readFile(path), QByteArray("not-dpapi-data"));
}

void MainWindowTest::clearsSessionWithoutChangingDeviceIdentity()
{
    SessionProfile profile;
    profile.deviceId = QStringLiteral("synthetic-device-only");
    profile.token = QStringLiteral("synthetic-token_123");
    profile.accountSession = true;
    profile.clearCredentials();
    QCOMPARE(profile.deviceId, QStringLiteral("synthetic-device-only"));
    QVERIFY(!profile.hasSession());
    QVERIFY(!profile.accountSession);
    profile.token = QStringLiteral("synthetic-expired-token_123");
    profile.expiresAt = QDateTime::currentDateTimeUtc().addSecs(-1);
    QVERIFY(!profile.hasSession());
}

void MainWindowTest::parsesNormalSessionToken()
{
    SessionProfile profile;
    profile.deviceId = QStringLiteral("synthetic-device-only");
    profile.headers = {{"version", "4.0.9"}, {"devicetype", "android"}, {"user-agent", "Test/1"}};
    const QJsonObject data{{QStringLiteral("token"), QStringLiteral("synthetic-issued-token")},
                           {QStringLiteral("user_id"), QStringLiteral("123")}};
    QVERIFY(NovelApi::parseSessionToken(data, profile, true).isEmpty());
    QCOMPARE(profile.token, QStringLiteral("synthetic-issued-token_123"));
    QVERIFY(profile.accountSession);
    QCOMPARE(profile.deviceId, QStringLiteral("synthetic-device-only"));
    auto broken = data;
    broken.insert(QStringLiteral("token"), QStringLiteral("bad\nvalue"));
    QVERIFY(!NovelApi::parseSessionToken(broken, profile, false).isEmpty());
    QCOMPARE(profile.token, QStringLiteral("synthetic-issued-token_123"));
    broken = data;
    broken.insert(QStringLiteral("user_id"), QJsonValue(123));
    QVERIFY(!NovelApi::parseSessionToken(broken, profile, false).isEmpty());
}

void MainWindowTest::rejectsSessionProfileBoundaries()
{
    QTemporaryDir dir;
    SessionProfile profile;
    profile.deviceId = QStringLiteral("synthetic-device-only");
    profile.headers = {{"version", "4.0.9"}, {"devicetype", "android"}, {"user-agent", "Test/1"}};
    QVERIFY(SessionStore::validate(profile).isEmpty());
    profile.deviceId = QString(257, QLatin1Char('x'));
    QVERIFY(!SessionStore::validate(profile).isEmpty());
    profile.deviceId = QStringLiteral("synthetic-device-only");
    profile.headers["user-agent"] = "bad\r\nheader";
    QVERIFY(!SessionStore::validate(profile).isEmpty());
    profile.headers["user-agent"] = "Test/1";
    profile.headers["version"] = "4.1.0";
    QVERIFY(!SessionStore::validate(profile).isEmpty());
    profile.headers["version"] = "4.0.9";
    QVERIFY(!SessionStore::save(dir.path(), profile).isEmpty());
    const auto oversized = dir.filePath(QStringLiteral("oversized.dat"));
    QVERIFY(writeFile(oversized, QByteArray(64 * 1024 + 1, 'x')));
    SessionProfile restored;
    QVERIFY(!SessionStore::load(oversized, restored).isEmpty());
    QVERIFY(restored.deviceId.isEmpty());
}

void MainWindowTest::restoresSessionWithoutCapture()
{
    QTemporaryDir dir;
    const auto isolatedPath = dir.filePath(QStringLiteral("session.dat"));
    SessionProfile profile;
    profile.deviceId = QStringLiteral("synthetic-restored-deviceP");
    profile.token = QStringLiteral("synthetic-restored-token_123");
    profile.headers = {{"version", "4.0.9"}, {"devicetype", "android"}, {"user-agent", "Test/1"}};
    QVERIFY(SessionStore::save(isolatedPath, profile).isEmpty());
    SessionProfile loaded;
    QVERIFY(SessionStore::load(isolatedPath, loaded).isEmpty());
    NovelApi api;
    QSignalSpy changes(&api, &NovelApi::sessionChanged);
    QVERIFY(api.restoreSession(loaded).isEmpty());
    QVERIFY(api.hasSession());
    QVERIFY(!api.isBusy());
    QCOMPARE(changes.count(), 1);
    QCOMPARE(api.sessionProfile().deviceId, profile.deviceId);
    const auto cleanup = qScopeGuard([&] {
        profile.clearCredentials();
        SessionStore::save(SessionStore::defaultPath(), profile);
    });
    QVERIFY(SessionStore::save(SessionStore::defaultPath(), profile).isEmpty());
    {
        MainWindow window;
        auto *capture = window.findChild<QLineEdit *>(QStringLiteral("sessionCaptureEdit"));
        auto *input = window.findChild<QLineEdit *>(QStringLiteral("bookSearchKeywordsEdit"));
        auto *global = window.findChild<QPushButton *>(QStringLiteral("globalBookSearchButton"));
        auto *status = window.findChild<QLabel *>(QStringLiteral("sessionStateLabel"));
        QVERIFY(capture && input && global && status);
        QVERIFY(capture->text().isEmpty());
        input->setText(QStringLiteral("合成查询"));
        QVERIFY(global->isEnabled());
        QVERIFY(!status->text().contains(profile.token));
        QVERIFY(!status->text().contains(profile.deviceId));
    }
    {
        BookDownloadController controller;
        QVERIFY(controller.hasSession());
        controller.forgetSession();
        QVERIFY(!controller.hasSession());
        QVERIFY(controller.hasDeviceIdentity());
        SessionProfile forgotten;
        QVERIFY(SessionStore::load(SessionStore::defaultPath(), forgotten).isEmpty());
        QVERIFY(forgotten.token.isEmpty());
        QCOMPARE(forgotten.deviceId, profile.deviceId);
        controller.invalidate(QStringLiteral("missing-synthetic-capture.hcy"));
        QVERIFY(!controller.hasSession());
        QVERIFY(controller.hasDeviceIdentity());
    }
}

void MainWindowTest::rejectsLoginWithoutIdentity()
{
    NovelApi api;
    QSignalSpy failures(&api, &NovelApi::failed);
    api.acquireDeviceSession();
    QCOMPARE(failures.count(), 1);
    QVERIFY(api.sessionProfile().deviceId.isEmpty());
    QVERIFY(!api.isBusy());
    api.loginAccount(QStringLiteral("synthetic-account"), QStringLiteral("synthetic-password"));
    QCOMPARE(failures.count(), 2);
    QVERIFY(api.sessionProfile().deviceId.isEmpty());
    QVERIFY(!api.isBusy());
}

void MainWindowTest::preservesSessionAfterBadCapture()
{
    SessionProfile profile;
    profile.deviceId = QStringLiteral("synthetic-device-only");
    profile.token = QStringLiteral("synthetic-existing-token_123");
    profile.headers = {{"version", "4.0.9"}, {"devicetype", "android"}, {"user-agent", "Test/1"}};
    NovelApi api;
    QVERIFY(api.restoreSession(profile).isEmpty());
    QVERIFY(!api.importSession(QStringLiteral("missing-synthetic-capture.hcy")).isEmpty());
    QVERIFY(!api.importRequest({}).isEmpty());
    QVERIFY(!api.importRequest(QByteArray(1024 * 1024 + 1, 'x')).isEmpty());
    QVERIFY(!api.importRequest(QByteArrayLiteral("GET / HTTP/1.1\r\n\r\n")).isEmpty());
    QVERIFY(api.hasSession());
    QCOMPARE(api.sessionProfile().token, profile.token);
    QCOMPARE(api.sessionProfile().deviceId, profile.deviceId);
    QVERIFY(!api.isBusy());
}

void MainWindowTest::keepsDeviceMarkerAtEnd()
{
    SessionProfile profile;
    profile.deviceId = QStringLiteral("0123456789abcdefP");
    QCOMPARE(profile.rawDeviceId(), QStringLiteral("0123456789abcdef"));
    QCOMPARE(profile.requestDeviceId(), profile.deviceId);
    profile.deviceId = QStringLiteral("0123456789abcdef");
    QCOMPARE(profile.rawDeviceId(), profile.deviceId);
    QCOMPARE(profile.requestDeviceId(), QStringLiteral("0123456789abcdefP"));
}

namespace {
QByteArray pcapHeader()
{
    QByteArray bytes(24, '\0');
    qToLittleEndian<quint32>(0xa1b2c3d4, bytes.data());
    qToLittleEndian<quint16>(2, bytes.data() + 4);
    qToLittleEndian<quint16>(4, bytes.data() + 6);
    qToLittleEndian<quint32>(65535, bytes.data() + 16);
    qToLittleEndian<quint32>(1, bytes.data() + 20);
    return bytes;
}

QByteArray tcpRecord(quint32 sequence, const QByteArray &payload, quint8 flags = 0x18)
{
    QByteArray frame(54, '\0');
    qToBigEndian<quint16>(0x0800, frame.data() + 12);
    frame[14] = 0x45;
    qToBigEndian<quint16>(40 + payload.size(), frame.data() + 16);
    frame[23] = 6;
    qToBigEndian<quint32>(0x0a00020f, frame.data() + 26);
    qToBigEndian<quint32>(0x01020304, frame.data() + 30);
    qToBigEndian<quint16>(34567, frame.data() + 34);
    qToBigEndian<quint16>(80, frame.data() + 36);
    qToBigEndian<quint32>(sequence, frame.data() + 38);
    frame[46] = 0x50;
    frame[47] = static_cast<char>(flags);
    frame += payload;
    QByteArray record(16, '\0');
    qToLittleEndian<quint32>(frame.size(), record.data() + 8);
    qToLittleEndian<quint32>(frame.size(), record.data() + 12);
    return record + frame;
}
}

void MainWindowTest::extractsTargetRequestFromPcap()
{
    const QByteArray request("POST /v1/xs/ss HTTP/1.1\r\nHost: yj.kkj234ls.com\r\n"
                             "Content-Type: application/octet-stream\r\nContent-Length: 16\r\n\r\n0123456789abcdef");
    const auto capture = pcapHeader() + tcpRecord(100, {}, 0x02)
        + tcpRecord(161, request.sliced(60)) + tcpRecord(101, request.first(60))
        + tcpRecord(101, request.first(60));
    PcapRequestReader reader;
    QList<QByteArray> found;
    for (qsizetype offset = 0; offset < capture.size(); offset += 7)
        found.append(reader.append(capture.sliced(offset, qMin<qsizetype>(7, capture.size() - offset))));
    QVERIFY(reader.errorString().isEmpty());
    QCOMPARE(found.size(), 1);
    QCOMPARE(found.first(), request);
}

void MainWindowTest::rejectsUnsafePcapRequests()
{
    PcapRequestReader reader;
    auto malformed = pcapHeader();
    qToLittleEndian<quint32>(999, malformed.data() + 20);
    QVERIFY(reader.append(malformed).isEmpty());
    QVERIFY(!reader.errorString().isEmpty());
    reader.reset();
    const QByteArray wrongHost("POST /v1/xs/ss HTTP/1.1\r\nHost: unrelated.invalid\r\n"
                               "Content-Length: 4\r\n\r\ntest");
    const QByteArray chunked("POST /v1/xs/ss HTTP/1.1\r\nHost: yj.kkj234ls.com\r\n"
                             "Transfer-Encoding: chunked\r\n\r\n0\r\n\r\n");
    const QByteArray duplicated("POST /v1/xs/ss HTTP/1.1\r\nHost: yj.kkj234ls.com\r\n"
                                "Content-Length: 4\r\nContent-Length: 4\r\n\r\ntest");
    for (const auto &request : {wrongHost, chunked, duplicated}) {
        reader.reset();
        QVERIFY(reader.append(pcapHeader() + tcpRecord(1, request)).isEmpty());
        QVERIFY(reader.errorString().isEmpty());
    }
}

void MainWindowTest::drainsBufferedPcapRequests()
{
    const QByteArray detail("POST /v1/xs/xq HTTP/1.1\r\nHost: yj.kkj234ls.com\r\n"
                            "Content-Type: application/octet-stream\r\nContent-Length: 16\r\n\r\n0123456789abcdef");
    auto category = detail;
    category.replace("/v1/xs/xq", "/v1/xs/ss");
    for (const bool samePacket : {false, true}) {
        for (const quint8 flags : {quint8(0x18), quint8(0x19)}) {
            auto capture = pcapHeader();
            QByteArray combined;
            quint32 sequence = 1;
            for (int index = 0; index < 17; ++index) {
                const auto &request = index == 16 ? category : detail;
                if (samePacket)
                    combined.append(request);
                else
                    capture.append(tcpRecord(sequence, request, index == 16 ? flags : quint8(0x18)));
                sequence += static_cast<quint32>(request.size());
            }
            if (samePacket)
                capture.append(tcpRecord(1, combined, flags));
            PcapRequestReader reader;
            auto batch = reader.append(capture);
            QCOMPARE(batch.size(), 8);
            QList<QByteArray> found = batch;
            for (int round = 0; round < 4; ++round) {
                batch = reader.append({});
                QVERIFY(batch.size() <= 8);
                found.append(batch);
                if (batch.isEmpty())
                    break;
            }
            QVERIFY(reader.errorString().isEmpty());
            QCOMPARE(found.size(), 17);
            QCOMPARE(found.last(), category);
            QVERIFY(reader.append({}).isEmpty());
        }
    }
}

void MainWindowTest::parsesMuMuTraceState()
{
    const QByteArray output("Config file:                 C:\\synthetic VM\\instance.nemu\n"
                            "State:                       running (since 2026-11-05T00:00:00.000000000)\n"
                            "NIC 1:                       MAC: 001122334455, Attachment: NAT, Cable connected: on, "
                            "Trace: off (file: none), Type: virtio, Reported speed: 0 Mbps, Boot priority: 0, "
                            "Promisc Policy: deny, Bandwidth group: none\n"
                            "NIC 2:                       disabled\n");
    for (const auto &bytes : {output, QByteArray(output).replace("\n", "\r\n")}) {
        MuMuVmState state;
        QVERIFY(MuMuCapture::parseVmState(bytes, 1, state).isEmpty());
        QVERIFY(state.running);
        QVERIFY(!state.tracing);
        QCOMPARE(state.configPath, QStringLiteral("C:\\synthetic VM\\instance.nemu"));
        QVERIFY(state.traceFile.isEmpty());
    }
}

void MainWindowTest::parsesMuMuEnabledTraceState()
{
    const QByteArray output("Config file: C:\\synthetic\\instance.nemu\n"
                            "State: running (since 2026-11-05T00:00:00.000000000)\n"
                            "NIC 1: MAC: 001122334455, Attachment: NAT, Cable connected: on, "
                            "Trace: off (file: none), Type: virtio\n"
                            "NIC 2: MAC: 001122334466, Attachment: NAT, Cable connected: on, "
                            "Trace: on (file: C:\\capture (saved), Type: test\\network.pcap), Type: virtio, "
                            "Reported speed: 0 Mbps\n");
    MuMuVmState state;
    QVERIFY(MuMuCapture::parseVmState(output, 2, state).isEmpty());
    QVERIFY(state.running);
    QVERIFY(state.tracing);
    QCOMPARE(state.traceFile, QStringLiteral("C:\\capture (saved), Type: test\\network.pcap"));
    auto stopped = output;
    stopped.replace("State: running", "State: poweroff");
    stopped.replace("Trace: on", "Trace: off");
    QVERIFY(MuMuCapture::parseVmState(stopped, 2, state).isEmpty());
    QVERIFY(!state.running);
    QVERIFY(!state.tracing);
    QCOMPARE(state.traceFile, QStringLiteral("C:\\capture (saved), Type: test\\network.pcap"));
}

void MainWindowTest::rejectsIncompleteMuMuTraceState()
{
    const QByteArray config("Config file: C:\\synthetic\\instance.nemu\n");
    const QByteArray vmState("State: running (since 2026-11-05T00:00:00.000000000)\n");
    const QByteArray nic("NIC 1: MAC: 001122334455, Attachment: NAT, Cable connected: on, "
                         "Trace: off (file: none), Type: virtio\n");
    const QList<QByteArray> outputs{
        {},
        "CfgFile=\"C:\\\\synthetic\\\\instance.nemu\"\nVMState=\"running\"\n",
        vmState + nic,
        config + nic,
        config + "State: \n" + nic,
        config + vmState,
        config + vmState + "NIC 1: disabled\n",
        config + vmState + QByteArray(nic).replace("Trace: off", "Trace: unknown"),
        config + vmState + QByteArray(nic).replace("Trace: off (file: none)", "Trace: off"),
        config + config + vmState + nic,
        config + vmState + "State: poweroff\n" + nic,
        config + vmState + nic + nic,
        config + vmState + QByteArray(nic).replace("none", QByteArray("bad\0path", 8)),
        config + vmState + QByteArray(nic).replace("none", "bad\rpath"),
        config + vmState + QByteArray(nic).replace("none", QByteArray(4097, 'x')),
        QByteArray(128 * 1024 + 1, 'x')
    };
    MuMuVmState state;
    state.configPath = QStringLiteral("unchanged-config");
    state.traceFile = QStringLiteral("unchanged-trace");
    state.running = true;
    state.tracing = true;
    for (const auto &output : outputs) {
        QVERIFY(!MuMuCapture::parseVmState(output, 1, state).isEmpty());
        QCOMPARE(state.configPath, QStringLiteral("unchanged-config"));
        QCOMPARE(state.traceFile, QStringLiteral("unchanged-trace"));
        QVERIFY(state.running);
        QVERIFY(state.tracing);
    }
    for (const int adapter : {0, 2, 9})
        QVERIFY(!MuMuCapture::parseVmState(config + vmState + nic, adapter, state).isEmpty());
}

void MainWindowTest::refusesInvalidCapturePaths()
{
    MuMuCapture capture;
    QSignalSpy finished(&capture, &MuMuCapture::finished);
    capture.start(QStringLiteral("missing-VBoxManage.exe"), QStringLiteral("missing-instance.nemu"), 1);
    QCOMPARE(finished.count(), 1);
    QVERIFY(!finished.first().first().toBool());
    QVERIFY(!capture.isBusy());
    QVERIFY(!capture.needsRecovery());
}

void MainWindowTest::requiresCaptureConsent()
{
    QTemporaryDir dir;
    const auto tool = dir.filePath(QStringLiteral("VBoxManage.exe"));
    const auto config = dir.filePath(QStringLiteral("instance.nemu"));
    QVERIFY(writeFile(tool, "synthetic-not-executable"));
    QVERIFY(writeFile(config, "synthetic-not-a-vm"));
    MuMuCaptureDialog dialog;
    auto *toolEdit = dialog.findChild<QLineEdit *>(QStringLiteral("mumuCaptureToolEdit"));
    auto *configEdit = dialog.findChild<QLineEdit *>(QStringLiteral("mumuCaptureConfigEdit"));
    auto *consent = dialog.findChild<QCheckBox *>(QStringLiteral("mumuCaptureConsentCheck"));
    auto *instances = dialog.findChild<QComboBox *>(QStringLiteral("mumuCaptureInstanceCombo"));
    auto *detect = dialog.findChild<QPushButton *>(QStringLiteral("detectMuMuInstancesButton"));
    QVERIFY(instances && detect);
    auto *start = dialog.findChild<QPushButton *>(QStringLiteral("startMuMuCaptureButton"));
    auto *stop = dialog.findChild<QPushButton *>(QStringLiteral("stopMuMuCaptureButton"));
    QVERIFY(toolEdit && configEdit && consent && start && stop);
    toolEdit->setText(tool);
    configEdit->setText(config);
    QVERIFY(!consent->isChecked());
    QVERIFY(!start->isEnabled());
    QVERIFY(!stop->isEnabled());
    consent->setChecked(true);
    QVERIFY(start->isEnabled());
    configEdit->setText(dir.filePath(QStringLiteral("another-instance.nemu")));
    QVERIFY(!consent->isChecked());
    QVERIFY(!start->isEnabled());
}

void MainWindowTest::extractsCategoryConditions()
{
    NovelCategory category;
    const QJsonObject data{{QStringLiteral("tag_id"), QStringLiteral("5,10,23")},
                           {QStringLiteral("ad_code"), QStringLiteral("video_mix_vertical")},
                           {QStringLiteral("page"), QStringLiteral("7")}};
    QVERIFY(NovelCategory::fromRequestData(data, category).isEmpty());
    QCOMPARE(category.tagId, QStringLiteral("5,10,23"));
    QCOMPARE(category.key(), NovelCategory::defaultCategory().key());
    QVERIFY(!category.parameters().contains(QStringLiteral("page")));
    category.name = QStringLiteral("自定义名称");
    QCOMPARE(category.key(), NovelCategory::defaultCategory().key());
}

void MainWindowTest::rejectsAmbiguousCategoryConditions()
{
    const QJsonObject valid{{QStringLiteral("tag_id"), QStringLiteral("80")}};
    const QList<QJsonObject> invalid{
        {}, {{QStringLiteral("tag_id"), 80}},
        {{QStringLiteral("tag_id"), QStringLiteral("../80")}},
        {{QStringLiteral("tag_id"), QStringLiteral("80,,5")}},
        {{QStringLiteral("tag_id"), QStringLiteral("80")}, {QStringLiteral("keywords"), QStringLiteral("test")}},
        {{QStringLiteral("tag_id"), QStringLiteral("80")}, {QStringLiteral("position"), QStringLiteral("comic")}},
        {{QStringLiteral("tag_id"), QStringLiteral("80")}, {QStringLiteral("sort"), QStringLiteral("new")}},
        {{QStringLiteral("tag_id"), QStringLiteral("80")}, {QStringLiteral("token"), QStringLiteral("secret")}}
    };
    NovelCategory category = NovelCategory::defaultCategory();
    for (const auto &data : invalid) {
        QVERIFY(!NovelCategory::fromRequestData(data, category).isEmpty());
        QCOMPARE(category.key(), NovelCategory::defaultCategory().key());
    }
    QVERIFY(NovelCategory::fromRequestData(valid, category).isEmpty());
    QCOMPARE(category.tagId, QStringLiteral("80"));
    QVERIFY(!category.key().contains(QStringLiteral("80/")));
}

void MainWindowTest::persistsCategoriesWithoutSession()
{
    QTemporaryDir dir;
    auto category = NovelCategory::defaultCategory();
    category.tagId = QStringLiteral("80");
    category.name = QStringLiteral("合成分类");
    const QVector<NovelCategory> categories{NovelCategory::defaultCategory(), category};
    const auto path = dir.filePath(QStringLiteral("categories.json"));
    QVERIFY(BookListCache::saveCategories(path, categories).isEmpty());
    QVector<NovelCategory> restored;
    QVERIFY(BookListCache::loadCategories(path, restored).isEmpty());
    QCOMPARE(restored.size(), 2);
    QCOMPARE(restored[1].name, category.name);
    QCOMPARE(restored[1].key(), category.key());
    const auto raw = readFile(path);
    QVERIFY(!raw.contains("token"));
    QVERIFY(!raw.contains("deviceId"));
    QVERIFY(!BookListCache::saveCategories(path, {category, category}).isEmpty());
    QCOMPARE(readFile(path), raw);
    QVERIFY(writeFile(path, "{broken"));
    QVERIFY(!BookListCache::loadCategories(path, restored).isEmpty());
    QCOMPARE(restored.size(), 2);
}

void MainWindowTest::isolatesCategoryBookCaches()
{
    QTemporaryDir dir;
    const auto first = NovelCategory::defaultCategory();
    auto second = first;
    second.tagId = QStringLiteral("80");
    auto differentCondition = first;
    differentCondition.adCode.clear();
    QVERIFY(BookListCache::defaultPath(first) != BookListCache::defaultPath(second));
    QVERIFY(BookListCache::defaultPath(first) != BookListCache::defaultPath(differentCondition));
    BookListSnapshot snapshot;
    snapshot.page = 3;
    snapshot.hasMore = true;
    snapshot.updatedAt = QDateTime::currentDateTimeUtc();
    const auto path = dir.filePath(QStringLiteral("list.json"));
    QVERIFY(BookListCache::save(path, snapshot, first).isEmpty());
    BookListSnapshot restored;
    QVERIFY(!BookListCache::load(path, restored, second).isEmpty());
    QCOMPARE(restored.page, 0);
    QVERIFY(!BookListCache::load(path, restored, differentCondition).isEmpty());
    QVERIFY(BookListCache::load(path, restored, first).isEmpty());
    QCOMPARE(restored.page, 3);
}

void MainWindowTest::readsLegacyDefaultCategoryCache()
{
    QTemporaryDir dir;
    const auto path = dir.filePath(QStringLiteral("legacy.json"));
    const QJsonObject object{{QStringLiteral("version"), 1}, {QStringLiteral("app_version"), QStringLiteral("4.0.9")},
                             {QStringLiteral("tag_id"), QStringLiteral("5,10,23")},
                             {QStringLiteral("updated_at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                             {QStringLiteral("page"), 2}, {QStringLiteral("has_more"), false},
                             {QStringLiteral("complete"), true}, {QStringLiteral("books"), QJsonArray{}}};
    QVERIFY(writeFile(path, QJsonDocument(object).toJson()));
    BookListSnapshot restored;
    QVERIFY(BookListCache::load(path, restored).isEmpty());
    QCOMPARE(restored.page, 2);
    auto other = NovelCategory::defaultCategory();
    other.adCode.clear();
    QVERIFY(!BookListCache::load(path, restored, other).isEmpty());
}

void MainWindowTest::pairsDetectedMuMuInstancesByVersion()
{
    QTemporaryDir dir;
    for (const auto &version : {QStringLiteral("15.0"), QStringLiteral("12.0")}) {
        QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("nx_device/%1/hypervisor").arg(version))));
        QVERIFY(writeFile(dir.filePath(QStringLiteral("nx_device/%1/hypervisor/VBoxManage.exe").arg(version)),
                          "synthetic-not-executable"));
        const auto name = QStringLiteral("MuMuPlayer-%1-1").arg(version);
        QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("vms/%1").arg(name))));
        const auto xml = QStringLiteral("<VirtualBox><Machine uuid='{01234567-89ab-cdef-0123-456789abcdef}' name='%1'/></VirtualBox>")
                             .arg(name).toUtf8();
        QVERIFY(writeFile(dir.filePath(QStringLiteral("vms/%1/%1.nemu").arg(name)), xml));
    }
    const auto found = MuMuDiscovery::scanInstallations({dir.path(), dir.path()});
    QCOMPARE(found.size(), 2);
    for (const auto &instance : found) {
        QVERIFY(instance.toolPath.contains(QStringLiteral("nx_device/%1/hypervisor").arg(instance.version)));
        QVERIFY(instance.configPath.contains(QStringLiteral("MuMuPlayer-%1-1").arg(instance.version)));
    }
}

void MainWindowTest::rejectsInvalidMuMuInstances()
{
    QTemporaryDir dir;
    QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("nx_device/15.0/hypervisor"))));
    QVERIFY(writeFile(dir.filePath(QStringLiteral("nx_device/15.0/hypervisor/VBoxManage.exe")), "not-executable"));
    const QList<QPair<QString, QByteArray>> invalid{
        {QStringLiteral("MuMuPlayer-12.0-1"), "<VirtualBox><Machine uuid='{01234567-89ab-cdef-0123-456789abcdef}'/></VirtualBox>"},
        {QStringLiteral("MuMuPlayer-15.0-2"), "<VirtualBox><Machine uuid='invalid'/></VirtualBox>"},
        {QStringLiteral("MuMuPlayer-15.0-3"), "<!DOCTYPE VirtualBox><VirtualBox><Machine uuid='{01234567-89ab-cdef-0123-456789abcdef}'/></VirtualBox>"}
    };
    for (const auto &entry : invalid) {
        QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("vms/%1").arg(entry.first))));
        QVERIFY(writeFile(dir.filePath(QStringLiteral("vms/%1/%1.nemu").arg(entry.first)), entry.second));
    }
    QVERIFY(MuMuDiscovery::scanInstallations({dir.path(), dir.filePath(QStringLiteral("missing"))}).isEmpty());
}

void MainWindowTest::rejectsNonlocalMuMuRoots()
{
    const QStringList rejected{QStringLiteral("//synthetic.invalid/share/MuMuPlayer"),
                                QStringLiteral("\\\\?\\UNC\\synthetic.invalid\\share\\MuMuPlayer"),
                                QStringLiteral("\\\\.\\C:\\MuMuPlayer"),
                                QStringLiteral("C:relative"), QStringLiteral("relative/path"),
                                QStringLiteral("/rooted-without-drive")};
    for (const auto &path : rejected)
        QVERIFY(!MuMuDiscovery::isLocalPath(path));
    QVERIFY(MuMuDiscovery::scanInstallations(rejected).isEmpty());
    QTemporaryDir dir;
    QVERIFY(MuMuDiscovery::isLocalPath(dir.path()));
}

void MainWindowTest::rejectsMuMuRootsWithLinkedAncestor()
{
    QTemporaryDir dir;
    const auto target = dir.filePath(QStringLiteral("actual"));
    const auto link = dir.filePath(QStringLiteral("linked"));
    QVERIFY(QDir().mkpath(target + QStringLiteral("/install")));
    const auto nativeTarget = QDir::toNativeSeparators(target);
    const auto nativeLink = QDir::toNativeSeparators(link);
    if (!CreateSymbolicLinkW(reinterpret_cast<LPCWSTR>(nativeLink.utf16()),
                             reinterpret_cast<LPCWSTR>(nativeTarget.utf16()), 0x1 | 0x2))
        QSKIP("当前 Windows 用户未获目录符号链接创建权限");
    const auto cleanup = qScopeGuard([&] { RemoveDirectoryW(reinterpret_cast<LPCWSTR>(nativeLink.utf16())); });
    QVERIFY(!MuMuDiscovery::isLocalPath(link + QStringLiteral("/install")));
    QVERIFY(MuMuDiscovery::scanInstallations({link + QStringLiteral("/install")}).isEmpty());
    QVERIFY(!MuMuDiscovery::isLocalPath(target + QStringLiteral("/../actual/install")));
}

void MainWindowTest::switchesCategoryCachesWithoutNetwork()
{
    const auto first = NovelCategory::defaultCategory();
    auto second = first;
    second.tagId = QStringLiteral("80");
    second.name = QStringLiteral("第二分类");
    const auto config = BookListCache::categoriesPath();
    const auto previous = readFile(config);
    const auto cleanup = qScopeGuard([&] {
        if (previous.isEmpty())
            QFile::remove(config);
        else
            writeFile(config, previous);
    });
    QVERIFY(BookListCache::saveCategories(config, {first, second}).isEmpty());
    for (const auto &category : {first, second}) {
        BookListSnapshot snapshot;
        snapshot.page = category.tagId == second.tagId ? 4 : 2;
        snapshot.hasMore = true;
        snapshot.updatedAt = QDateTime::currentDateTimeUtc();
        NovelSearchBook book;
        book.id = category.tagId == second.tagId ? QString(32, QLatin1Char('b')) : QString(32, QLatin1Char('a'));
        book.name = category.name;
        snapshot.books.append(book);
        QVERIFY(BookListCache::save(BookListCache::defaultPath(category), snapshot, category).isEmpty());
    }
    BookDownloadController controller;
    QVERIFY(controller.selectCategory(second.key()).isEmpty());
    QVERIFY(!controller.isBusy());
    QCOMPARE(controller.searchPage(), 4);
    QCOMPARE(controller.searchResults().first().name, second.name);
    QVERIFY(controller.selectCategory(first.key()).isEmpty());
    QCOMPARE(controller.searchPage(), 2);
    QVERIFY(controller.canLoadMoreSearch());
    MainWindow window;
    auto *combo = window.findChild<QComboBox *>(QStringLiteral("bookCategoryCombo"));
    auto *add = window.findChild<QPushButton *>(QStringLiteral("captureCategoryButton"));
    QVERIFY(combo && add);
    QCOMPARE(combo->count(), 2);
    combo->setCurrentIndex(1);
    auto *table = window.findChild<QTableWidget *>(QStringLiteral("bookSearchResultsTable"));
    QVERIFY(table);
    QCOMPARE(table->item(0, 0)->text(), second.name);
}

void MainWindowTest::rejectsDuplicateCategories()
{
    BookDownloadController controller;
    const auto count = controller.categories().size();
    QVERIFY(!controller.addCategory(controller.currentCategory()).isEmpty());
    QCOMPARE(controller.categories().size(), count);
    auto invalid = controller.currentCategory();
    invalid.tagId = QStringLiteral("../80");
    QVERIFY(!controller.addCategory(invalid).isEmpty());
    QCOMPARE(controller.categories().size(), count);
}

void MainWindowTest::categoryCaptureKeepsConsentExplicit()
{
    MuMuCaptureDialog dialog(nullptr, true);
    QVERIFY(dialog.windowTitle().contains(QStringLiteral("分类")));
    auto *consent = dialog.findChild<QCheckBox *>(QStringLiteral("mumuCaptureConsentCheck"));
    auto *start = dialog.findChild<QPushButton *>(QStringLiteral("startMuMuCaptureButton"));
    QVERIFY(consent && start);
    QVERIFY(!consent->isChecked());
    QVERIFY(!start->isEnabled());
    NovelCategory category = NovelCategory::defaultCategory();
    QVERIFY(!NovelApi::categoryFromRequest("POST /v1/xs/xq HTTP/1.1\r\n\r\n", category).isEmpty());
    QCOMPARE(category.key(), NovelCategory::defaultCategory().key());
}

void MainWindowTest::identifiesCategoryRequestWithoutChangingSession()
{
    const auto path = QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                          .filePath(QStringLiteral("keys-4.0.9.dat"));
    const bool existed = QFile::exists(path);
    const auto previous = readFile(path);
    const auto restore = qScopeGuard([&] {
        if (existed)
            writeFile(path, previous);
        else
            QFile::remove(path);
    });
    const QByteArray apiKey("0123456789abcdef");
    QByteArray payload = QByteArray("NovDecoder-4.0.9\n") + apiKey + QByteArray("fedcba9876543210");
    DATA_BLOB input{static_cast<DWORD>(payload.size()), reinterpret_cast<BYTE *>(payload.data())};
    DATA_BLOB output{};
    QVERIFY(CryptProtectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output));
    const QByteArray protectedKeys(reinterpret_cast<const char *>(output.pbData), output.cbData);
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    payload.fill('\0');
    QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
    QVERIFY(writeFile(path, protectedKeys));
    SessionProfile profile;
    profile.deviceId = QStringLiteral("synthetic-existing-device");
    profile.token = QStringLiteral("synthetic-existing-token_123");
    profile.headers = {{"version", "4.0.9"}, {"devicetype", "android"}, {"user-agent", "Test/1"}};
    NovelApi api;
    QVERIFY(api.restoreSession(profile).isEmpty());
    QSignalSpy changes(&api, &NovelApi::sessionChanged);
    const QJsonObject data{{QStringLiteral("tag_id"), QStringLiteral("80")}, {QStringLiteral("page"), QStringLiteral("8")}};
    const auto makeRequest = [&](const QJsonObject &conditions) {
        const QJsonObject object{{QStringLiteral("data"), conditions},
                                 {QStringLiteral("token"), QStringLiteral("synthetic-captured-token_456")},
                                 {QStringLiteral("deviceId"), QStringLiteral("synthetic-captured-device")}};
        const auto encrypted = AesEcb::encrypt(QJsonDocument(object).toJson(QJsonDocument::Compact), apiKey);
        return QByteArray("POST /v1/xs/ss HTTP/1.1\r\nHost: yj.kkj234ls.com\r\n"
                          "Content-Type: application/octet-stream\r\nversion: 4.0.9\r\n"
                          "devicetype: android\r\nuser-agent: Test/1\r\ntime: 1\r\nContent-Length: ")
            + QByteArray::number(encrypted.bytes.size()) + "\r\n\r\n" + encrypted.bytes;
    };
    const auto request = makeRequest(data);
    NovelCategory category;
    QVERIFY(NovelApi::categoryFromRequest(request, category).isEmpty());
    QCOMPARE(category.tagId, QStringLiteral("80"));
    QCOMPARE(changes.count(), 0);
    QCOMPARE(api.sessionProfile().token, profile.token);
    auto search = data;
    search.insert(QStringLiteral("keywords"), QStringLiteral("合成关键词"));
    QVERIFY(!NovelApi::categoryFromRequest(makeRequest(search), category).isEmpty());
    QVERIFY(api.importRequest(request).isEmpty());
    QCOMPARE(changes.count(), 1);
    QCOMPARE(api.sessionProfile().token, QStringLiteral("synthetic-captured-token_456"));
}

void MainWindowTest::addsCategoryWithoutStartingNetwork()
{
    const auto path = BookListCache::categoriesPath();
    const bool existed = QFile::exists(path);
    const auto previous = readFile(path);
    const auto restore = qScopeGuard([&] {
        if (existed)
            writeFile(path, previous);
        else
            QFile::remove(path);
    });
    QVERIFY(BookListCache::saveCategories(path, {NovelCategory::defaultCategory()}).isEmpty());
    BookDownloadController controller;
    auto category = NovelCategory::defaultCategory();
    category.name = QStringLiteral("新合成分类");
    category.tagId = QStringLiteral("81");
    QSignalSpy busy(&controller, &BookDownloadController::busyChanged);
    QVERIFY(controller.addCategory(category).isEmpty());
    QCOMPARE(controller.categories().size(), 2);
    QCOMPARE(controller.currentCategory().key(), category.key());
    QCOMPARE(busy.count(), 0);
    QVERIFY(!controller.isBusy());
    QVector<NovelCategory> saved;
    QVERIFY(BookListCache::loadCategories(path, saved).isEmpty());
    QCOMPARE(saved.last().name, category.name);
}

void MainWindowTest::preservesCorruptCategoryConfiguration()
{
    const auto path = BookListCache::categoriesPath();
    const bool existed = QFile::exists(path);
    const auto previous = readFile(path);
    const auto restore = qScopeGuard([&] {
        if (existed)
            writeFile(path, previous);
        else
            QFile::remove(path);
    });
    QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
    QVERIFY(writeFile(path, "{broken-existing-config"));
    BookDownloadController controller;
    auto category = NovelCategory::defaultCategory();
    category.name = QStringLiteral("不会覆盖配置");
    category.tagId = QStringLiteral("82");
    QVERIFY(!controller.addCategory(category).isEmpty());
    QCOMPARE(readFile(path), QByteArray("{broken-existing-config"));
}

void MainWindowTest::renamesCategoryWithoutDiscardingCache_data()
{
    QTest::addColumn<bool>("complete");
    QTest::newRow("partial") << false;
    QTest::newRow("complete") << true;
}

void MainWindowTest::renamesCategoryWithoutDiscardingCache()
{
    QFETCH(bool, complete);
    const auto original = NovelCategory::defaultCategory();
    const auto restore = preserveCategoryFiles({original});
    const auto config = BookListCache::categoriesPath();
    const auto cache = BookListCache::defaultPath(original);
    QVERIFY(BookListCache::saveCategories(config, {original}).isEmpty());
    BookListSnapshot snapshot;
    snapshot.page = 4;
    snapshot.hasMore = !complete;
    snapshot.complete = complete;
    snapshot.updatedAt = QDateTime::currentDateTimeUtc();
    NovelSearchBook book;
    book.id = QString(32, QLatin1Char('a'));
    book.name = QStringLiteral("保留的合成书籍");
    snapshot.books.append(book);
    QVERIFY(BookListCache::save(cache, snapshot, original).isEmpty());
    const auto cachedBytes = readFile(cache);

    BookDownloadController controller;
    QSignalSpy busy(&controller, &BookDownloadController::busyChanged);
    QSignalSpy changes(&controller, &BookDownloadController::categoriesChanged);
    QSignalSpy results(&controller, &BookDownloadController::searchResultsChanged);
    QVERIFY(controller.renameCategory(original.key(), QStringLiteral("  修改后的分类  ")).isEmpty());
    QCOMPARE(controller.currentCategory().name, QStringLiteral("修改后的分类"));
    QCOMPARE(controller.currentCategory().key(), original.key());
    QCOMPARE(controller.currentCategory().parameters(), original.parameters());
    QCOMPARE(BookListCache::defaultPath(controller.currentCategory()), cache);
    QCOMPARE(controller.searchPage(), 4);
    QCOMPARE(controller.canLoadMoreSearch(), !complete);
    QCOMPARE(controller.searchComplete(), complete);
    QCOMPARE(controller.listUpdatedAt(), snapshot.updatedAt);
    QCOMPARE(controller.searchResults().first().id, book.id);
    QCOMPARE(readFile(cache), cachedBytes);
    QCOMPARE(busy.count(), 0);
    QCOMPARE(changes.count(), 1);
    QCOMPARE(results.count(), 1);
    QVERIFY(controller.isGroupList());
    QVERIFY(!controller.isBusy());

    const auto savedBytes = readFile(config);
    QVERIFY(controller.renameCategory(original.key(), QStringLiteral("修改后的分类")).isEmpty());
    QCOMPARE(readFile(config), savedBytes);
    QCOMPARE(changes.count(), 1);
    BookDownloadController restarted;
    QCOMPARE(restarted.currentCategory().name, QStringLiteral("修改后的分类"));
    QCOMPARE(restarted.searchPage(), 4);
    QCOMPARE(restarted.searchComplete(), complete);
}

void MainWindowTest::rejectsInvalidCategoryRenames()
{
    const auto first = NovelCategory::defaultCategory();
    auto second = first;
    second.tagId = QStringLiteral("901");
    second.name = QStringLiteral("可重复名称");
    const auto restore = preserveCategoryFiles({first, second});
    const auto config = BookListCache::categoriesPath();
    QVERIFY(BookListCache::saveCategories(config, {first, second}).isEmpty());
    const auto originalBytes = readFile(config);
    BookDownloadController controller;
    const QStringList invalidNames{QString(), QStringLiteral("   "), QString(129, QLatin1Char('a')),
                                   QStringLiteral("中\n间"), QString(QChar(0)), QStringLiteral("中\t间")};
    for (const auto &name : invalidNames) {
        QVERIFY(!controller.renameCategory(first.key(), name).isEmpty());
        QCOMPARE(controller.currentCategory().name, first.name);
        QCOMPARE(readFile(config), originalBytes);
    }
    QVERIFY(!controller.renameCategory(QStringLiteral("missing"), QStringLiteral("不存在")).isEmpty());
    QCOMPARE(readFile(config), originalBytes);
    QVERIFY(controller.renameCategory(first.key(), second.name).isEmpty());
    QCOMPARE(controller.categories().first().name, controller.categories().last().name);
    QVERIFY(controller.categories().first().key() != controller.categories().last().key());
    QVERIFY(controller.renameCategory(first.key(), QString(128, QLatin1Char('a'))).isEmpty());
    QCOMPARE(controller.currentCategory().name.size(), 128);
}

void MainWindowTest::removesCategoriesWithoutDeletingCaches()
{
    const auto first = NovelCategory::defaultCategory();
    auto second = first;
    second.tagId = QStringLiteral("902");
    second.name = QStringLiteral("第二分类");
    auto third = first;
    third.tagId = QStringLiteral("903");
    third.name = QStringLiteral("第三分类");
    const QVector<NovelCategory> categories{first, second, third};
    const auto restore = preserveCategoryFiles(categories);
    const auto config = BookListCache::categoriesPath();
    QVERIFY(BookListCache::saveCategories(config, categories).isEmpty());
    QMap<QString, QByteArray> cachedBytes;
    for (int i = 0; i < categories.size(); ++i) {
        BookListSnapshot snapshot;
        snapshot.page = i + 2;
        snapshot.hasMore = true;
        snapshot.updatedAt = QDateTime::currentDateTimeUtc();
        NovelSearchBook book;
        book.id = QString(32, QChar('a' + i));
        book.name = categories[i].name;
        snapshot.books.append(book);
        const auto path = BookListCache::defaultPath(categories[i]);
        QVERIFY(BookListCache::save(path, snapshot, categories[i]).isEmpty());
        cachedBytes.insert(path, readFile(path));
    }

    BookDownloadController controller;
    QSignalSpy busy(&controller, &BookDownloadController::busyChanged);
    QSignalSpy results(&controller, &BookDownloadController::searchResultsChanged);
    const auto originalBytes = readFile(config);
    QVERIFY(!controller.removeCategory(QStringLiteral("missing")).isEmpty());
    QCOMPARE(readFile(config), originalBytes);
    QVERIFY(controller.removeCategory(second.key()).isEmpty());
    QCOMPARE(controller.currentCategory().key(), first.key());
    QCOMPARE(controller.searchPage(), 2);
    QCOMPARE(results.count(), 0);
    QVERIFY(controller.addCategory(second).isEmpty());
    QCOMPARE(controller.searchPage(), 3);
    QVERIFY(controller.selectCategory(first.key()).isEmpty());
    QVERIFY(controller.removeCategory(first.key()).isEmpty());
    QCOMPARE(controller.currentCategory().key(), third.key());
    QCOMPARE(controller.searchPage(), 4);
    QVERIFY(controller.selectCategory(second.key()).isEmpty());
    QVERIFY(controller.removeCategory(second.key()).isEmpty());
    QCOMPARE(controller.currentCategory().key(), third.key());
    QCOMPARE(controller.searchPage(), 4);

    QVector<NovelCategory> saved;
    QVERIFY(BookListCache::loadCategories(config, saved).isEmpty());
    QCOMPARE(saved.size(), 1);
    QCOMPARE(saved.first().key(), third.key());
    const auto remainingBytes = readFile(config);
    QVERIFY(!controller.removeCategory(third.key()).isEmpty());
    QCOMPARE(readFile(config), remainingBytes);
    QCOMPARE(controller.categories().size(), 1);
    QCOMPARE(controller.currentCategory().key(), third.key());
    BookDownloadController restarted;
    QCOMPARE(restarted.categories().size(), 1);
    QCOMPARE(restarted.currentCategory().key(), third.key());

    QVERIFY(controller.addCategory(first).isEmpty());
    QCOMPARE(controller.searchPage(), 2);
    QCOMPARE(controller.searchResults().first().name, first.name);
    for (auto it = cachedBytes.cbegin(); it != cachedBytes.cend(); ++it)
        QCOMPARE(readFile(it.key()), it.value());
    QCOMPARE(busy.count(), 0);
    QVERIFY(!controller.isBusy());
}

void MainWindowTest::preservesCategoriesWhenManagementCannotSave_data()
{
    QTest::addColumn<bool>("locked");
    QTest::newRow("corrupt-config") << false;
    QTest::newRow("locked-config") << true;
}

void MainWindowTest::preservesCategoriesWhenManagementCannotSave()
{
    QFETCH(bool, locked);
    const auto first = NovelCategory::defaultCategory();
    auto second = first;
    second.tagId = QStringLiteral("904");
    second.name = QStringLiteral("不能丢失的分类");
    const auto restore = preserveCategoryFiles({first, second});
    const auto config = BookListCache::categoriesPath();
    QVERIFY(BookListCache::saveCategories(config, {first, second}).isEmpty());
    BookDownloadController controller;
    HANDLE handle = INVALID_HANDLE_VALUE;
    const auto unlock = qScopeGuard([&] {
        if (handle != INVALID_HANDLE_VALUE)
            CloseHandle(handle);
    });
    if (locked) {
        handle = CreateFileW(reinterpret_cast<LPCWSTR>(config.utf16()), GENERIC_READ, FILE_SHARE_READ,
                             nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        QVERIFY(handle != INVALID_HANDLE_VALUE);
    } else {
        QVERIFY(writeFile(config, "{broken-management-config"));
    }
    const auto previous = readFile(config);
    QSignalSpy changes(&controller, &BookDownloadController::categoriesChanged);
    QVERIFY(!controller.renameCategory(first.key(), QStringLiteral("未保存的名字")).isEmpty());
    QCOMPARE(controller.currentCategory().name, first.name);
    QVERIFY(!controller.removeCategory(first.key()).isEmpty());
    QCOMPARE(controller.categories().size(), 2);
    QCOMPARE(controller.currentCategory().key(), first.key());
    QCOMPARE(readFile(config), previous);
    QCOMPARE(changes.count(), 0);
}

void MainWindowTest::categoryManagementDialogsPreserveState()
{
    const auto first = NovelCategory::defaultCategory();
    auto second = first;
    second.tagId = QStringLiteral("905");
    second.name = QStringLiteral("第二分类");
    const auto restore = preserveCategoryFiles({first, second});
    const auto config = BookListCache::categoriesPath();
    QVERIFY(BookListCache::saveCategories(config, {first}).isEmpty());
    BookListSnapshot snapshot;
    snapshot.page = 2;
    snapshot.hasMore = true;
    snapshot.updatedAt = QDateTime::currentDateTimeUtc();
    NovelSearchBook book;
    book.id = QString(32, QLatin1Char('a'));
    book.name = QStringLiteral("保留界面选择");
    snapshot.books.append(book);
    const auto cache = BookListCache::defaultPath(first);
    QVERIFY(BookListCache::save(cache, snapshot, first).isEmpty());
    const auto cachedBytes = readFile(cache);

    MainWindow window;
    auto *controller = window.findChild<BookDownloadController *>();
    auto *combo = window.findChild<QComboBox *>(QStringLiteral("bookCategoryCombo"));
    auto *rename = window.findChild<QPushButton *>(QStringLiteral("renameCategoryButton"));
    auto *remove = window.findChild<QPushButton *>(QStringLiteral("deleteCategoryButton"));
    auto *filter = window.findChild<QLineEdit *>(QStringLiteral("bookResultFilterEdit"));
    auto *table = window.findChild<QTableWidget *>(QStringLiteral("bookSearchResultsTable"));
    auto *bookId = window.findChild<QLineEdit *>(QStringLiteral("bookIdEdit"));
    QVERIFY(controller && combo && rename && remove && filter && table && bookId);
    QVERIFY(rename->isEnabled());
    QVERIFY(!remove->isEnabled());
    QVERIFY(remove->toolTip().contains(QStringLiteral("至少")));
    const auto originalBytes = readFile(config);
    QTimer::singleShot(0, &window, [&] {
        auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
        QVERIFY(dialog);
        QCOMPARE(dialog->textValue(), first.name);
        auto *edit = dialog->findChild<QLineEdit *>();
        QVERIFY(edit);
        QCOMPARE(edit->maxLength(), 128);
        dialog->setTextValue(QStringLiteral("取消的名字"));
        dialog->reject();
    });
    rename->click();
    QCOMPARE(readFile(config), originalBytes);
    QCOMPARE(combo->currentData().toString(), first.key());

    table->selectRow(0);
    filter->setText(book.name);
    QTimer::singleShot(0, &window, [] {
        auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
        QVERIFY(dialog);
        dialog->setTextValue(QStringLiteral("<b>新分类名</b>"));
        dialog->accept();
    });
    rename->click();
    QCOMPARE(controller->currentCategory().name, QStringLiteral("<b>新分类名</b>"));
    QVERIFY(combo->currentText().contains(QStringLiteral("<b>新分类名</b>")));
    QCOMPARE(filter->text(), book.name);
    QCOMPARE(bookId->text(), book.id);
    QCOMPARE(table->selectedItems().size(), 5);
    QCOMPARE(controller->searchPage(), 2);
    QCOMPARE(readFile(cache), cachedBytes);
    QVERIFY(!remove->isEnabled());

    QVERIFY(controller->addCategory(second).isEmpty());
    QCOMPARE(combo->count(), 2);
    QVERIFY(remove->isEnabled());
    QCOMPARE(combo->currentData().toString(), second.key());
    combo->setCurrentIndex(combo->findData(first.key()));
    filter->setText(book.name);
    const auto beforeDelete = readFile(config);
    QTimer::singleShot(0, &window, [] {
        auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        QVERIFY(dialog);
        QCOMPARE(dialog->textFormat(), Qt::PlainText);
        QCOMPARE(dialog->defaultButton(), dialog->button(QMessageBox::No));
        QVERIFY(dialog->text().contains(QStringLiteral("<b>新分类名</b>")));
        QVERIFY(dialog->text().contains(QStringLiteral("保留")));
        dialog->button(QMessageBox::No)->click();
    });
    remove->click();
    QCOMPARE(readFile(config), beforeDelete);
    QCOMPARE(controller->currentCategory().key(), first.key());
    QCOMPARE(filter->text(), book.name);

    QTimer::singleShot(0, &window, [] {
        auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        QVERIFY(dialog);
        dialog->button(QMessageBox::Yes)->click();
    });
    remove->click();
    QCOMPARE(combo->count(), 1);
    QCOMPARE(combo->currentData().toString(), second.key());
    QCOMPARE(controller->currentCategory().key(), second.key());
    QCOMPARE(controller->categories().size(), 1);
    QVERIFY(!remove->isEnabled());
    QVERIFY(filter->text().isEmpty());
    QCOMPARE(readFile(cache), cachedBytes);
    QVERIFY(!controller->isBusy());
}

int main(int argc, char **argv)
{
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    app.setApplicationName(QStringLiteral("NovDecoderSyntheticTests"));
    MainWindowTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_mainwindow.moc"
