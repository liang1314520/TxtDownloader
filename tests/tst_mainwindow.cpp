#include "mainwindow.h"

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

int main(int argc, char **argv)
{
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    MainWindowTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_mainwindow.moc"
