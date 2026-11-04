#include "mainwindow.h"
#include "booklistcache.h"
#include "bookdownloadcontroller.h"
#include "bookdetaildialog.h"

#include <QTableWidget>

#include <QComboBox>
#include <QPlainTextEdit>
#include <QStandardPaths>

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
    void searchDoesNotDiscardGroupCache();
    void cachedChapterCountsSortNumerically();
    void rejectsOtherGroupCache();
    void searchesOnlyCachedGroupByDefault();
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
    QCOMPARE(object.size(), 8);
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
