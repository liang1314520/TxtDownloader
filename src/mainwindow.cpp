#include "mainwindow.h"
#include "novdecoder.h"
#include "chapterdownload.h"
#include "bookdownloadwidget.h"

#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

MainWindow::MainWindow(QWidget *parent) : QWidget(parent)
{
    setWindowTitle(QStringLiteral("NOV 下载与解码工具"));
    resize(1100, 820);
    setMinimumSize(820, 680);

    auto *outer = new QVBoxLayout(this);
    tabs = new QTabWidget(this);
    auto *singlePage = new QWidget(tabs);
    tabs->addTab(singlePage, QStringLiteral("单章转换"));
    bookWidget = new BookDownloadWidget(tabs);
    tabs->addTab(bookWidget, QStringLiteral("整书下载"));
    outer->addWidget(tabs);
    auto *layout = new QVBoxLayout(singlePage);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(16);

    auto *title = new QLabel(QStringLiteral("NOV → TXT"), this);
    auto titleFont = title->font();
    titleFont.setPointSize(20);
    titleFont.setBold(true);
    title->setFont(titleFont);
    layout->addWidget(title);

    auto *description = new QLabel(
        QStringLiteral("选择本地章节文件，或粘贴单个章节下载链接，自动转换为 UTF-8 TXT。\n"
                       "仅支持当前已验证的章节格式；本地文件不会上传，源文件不会修改。"), this);
    description->setWordWrap(true);
    layout->addWidget(description);

    pathsWidget = new QWidget(this);
    auto *paths = new QGridLayout(pathsWidget);
    paths->setContentsMargins(0, 0, 0, 0);
    paths->setHorizontalSpacing(12);
    paths->setVerticalSpacing(12);
    inputMode = new QComboBox(this);
    inputMode->setObjectName("inputMode");
    inputMode->addItems({QStringLiteral("本地文件"), QStringLiteral("链接下载")});
    inputPathEdit = new QLineEdit(this);
    inputPathEdit->setObjectName("inputPathEdit");
    inputPathEdit->setPlaceholderText(QStringLiteral("选择 .nov 或 response.bin 文件"));
    inputPathEdit->setClearButtonEnabled(true);
    downloadUrlEdit = new QLineEdit(this);
    downloadUrlEdit->setObjectName("downloadUrlEdit");
    downloadUrlEdit->setPlaceholderText(QStringLiteral("粘贴完整的 http:// 或 https:// 章节下载链接（含签名参数）"));
    downloadUrlEdit->setClearButtonEnabled(true);
    downloadUrlEdit->setVisible(false);
    outputPathEdit = new QLineEdit(this);
    outputPathEdit->setObjectName("outputPathEdit");
    outputPathEdit->setPlaceholderText(QStringLiteral("解码后的 TXT 保存位置"));
    outputPathEdit->setClearButtonEnabled(true);
    auto *inputButton = new QPushButton(QStringLiteral("选择文件…"), this);
    inputButton->setObjectName("chooseInputButton");
    auto *outputButton = new QPushButton(QStringLiteral("保存位置…"), this);
    outputButton->setObjectName("chooseOutputButton");
    auto *inputLabel = new QLabel(QStringLiteral("输入文件"), this);
    paths->addWidget(new QLabel(QStringLiteral("输入方式"), this), 0, 0);
    paths->addWidget(inputMode, 0, 1, 1, 2);
    paths->addWidget(inputLabel, 1, 0);
    paths->addWidget(inputPathEdit, 1, 1);
    paths->addWidget(inputButton, 1, 2);
    paths->addWidget(downloadUrlEdit, 1, 1, 1, 2);
    paths->addWidget(new QLabel(QStringLiteral("输出 TXT"), this), 2, 0);
    paths->addWidget(outputPathEdit, 2, 1);
    paths->addWidget(outputButton, 2, 2);
    keyEdit = new QLineEdit(this);
    keyEdit->setObjectName("keyEdit");
    keyEdit->setEchoMode(QLineEdit::Password);
    keyEdit->setMaxLength(16);
    keyEdit->setPlaceholderText(QStringLiteral("输入 16 个可打印 ASCII 字符；仅本次运行有效，不保存或上传"));
    keyEdit->setClearButtonEnabled(true);
    paths->addWidget(new QLabel(QStringLiteral("解码密钥"), this), 3, 0);
    paths->addWidget(keyEdit, 3, 1, 1, 2);
    paths->setColumnStretch(1, 1);
    layout->addWidget(pathsWidget);

    auto *actions = new QHBoxLayout;
    actions->addStretch();
    cancelButton = new QPushButton(QStringLiteral("取消下载"), this);
    cancelButton->setObjectName("cancelDownloadButton");
    cancelButton->setMinimumHeight(38);
    cancelButton->setVisible(false);
    actions->addWidget(cancelButton);
    decodeButton = new QPushButton(QStringLiteral("解密并保存 TXT"), this);
    decodeButton->setObjectName("decodeButton");
    decodeButton->setMinimumHeight(38);
    decodeButton->setEnabled(false);
    actions->addWidget(decodeButton);
    layout->addLayout(actions);

    downloadProgress = new QProgressBar(this);
    downloadProgress->setObjectName("downloadProgress");
    downloadProgress->setVisible(false);
    layout->addWidget(downloadProgress);
    statusLabel = new QLabel(QStringLiteral("等待选择文件或输入链接。单文件上限 32 MiB。"), this);
    statusLabel->setObjectName("statusLabel");
    statusLabel->setTextFormat(Qt::PlainText);
    statusLabel->setWordWrap(true);
    statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(statusLabel);
    layout->addStretch();

    auto *notice = new QLabel(
        QStringLiteral("仅下载和处理你有权保存的单章内容，不绕过账号或阅读权限。签名链接过期时请重新获取。"), this);
    notice->setWordWrap(true);
    layout->addWidget(notice);

    downloader = new ChapterDownload(this);
    connect(downloader, &ChapterDownload::progress, this, [this](qint64 received, qint64 total) {
        downloadProgress->setRange(0, total > 0 ? 100 : 0);
        if (total > 0)
            downloadProgress->setValue(static_cast<int>(received * 100 / total));
        statusLabel->setText(QStringLiteral("正在下载：%1 KiB%2")
                                 .arg(received / 1024)
                                 .arg(total > 0 ? QStringLiteral(" / %1 KiB").arg(total / 1024) : QString()));
    });
    connect(downloader, &ChapterDownload::finished, this,
            [this](bool success, const QString &message, qint64 bytes, qsizetype characters) {
        cancelButton->setVisible(false);
        downloadProgress->setVisible(false);
        pathsWidget->setEnabled(true);
        tabs->setTabEnabled(1, true);
        if (success)
            reportSaved(outputPathEdit->text(), bytes, characters);
        else
            statusLabel->setText(QStringLiteral("未保存：%1").arg(message));
        updateAction();
    });
    connect(bookWidget, &BookDownloadWidget::busyChanged, this, [this](bool busy) {
        tabs->setTabEnabled(0, !busy);
    });
    connect(inputButton, &QPushButton::clicked, this, &MainWindow::chooseInput);
    connect(outputButton, &QPushButton::clicked, this, &MainWindow::chooseOutput);
    connect(inputMode, &QComboBox::currentIndexChanged, this,
            [this, inputButton, inputLabel](int index) {
        const bool fromUrl = index == 1;
        inputPathEdit->setVisible(!fromUrl);
        inputButton->setVisible(!fromUrl);
        downloadUrlEdit->setVisible(fromUrl);
        inputLabel->setText(fromUrl ? QStringLiteral("下载链接") : QStringLiteral("输入文件"));
        decodeButton->setText(fromUrl ? QStringLiteral("下载并转换 TXT") : QStringLiteral("解密并保存 TXT"));
        updatePaths();
    });
    connect(inputPathEdit, &QLineEdit::textChanged, this, &MainWindow::updatePaths);
    connect(downloadUrlEdit, &QLineEdit::textChanged, this, &MainWindow::updatePaths);
    connect(outputPathEdit, &QLineEdit::textChanged, this, &MainWindow::updateAction);
    connect(keyEdit, &QLineEdit::textChanged, this, &MainWindow::updateAction);
    connect(decodeButton, &QPushButton::clicked, this, &MainWindow::decodeAndSave);
    connect(cancelButton, &QPushButton::clicked, this, [this] {
        cancelButton->setEnabled(false);
        downloader->cancel();
    });
}

MainWindow::~MainWindow() = default;

void MainWindow::chooseInput()
{
    const auto path = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择章节资源文件"), inputPathEdit->text(),
        QStringLiteral("章节资源 (*.nov *.bin);;所有文件 (*)"));
    if (!path.isEmpty())
        inputPathEdit->setText(path);
}

void MainWindow::chooseOutput()
{
    const auto path = QFileDialog::getSaveFileName(
        this, QStringLiteral("保存解码后的 TXT"), outputPathEdit->text(),
        QStringLiteral("文本文件 (*.txt)"), nullptr, QFileDialog::DontConfirmOverwrite);
    if (!path.isEmpty())
        outputPathEdit->setText(path);
}

void MainWindow::updatePaths()
{
    if (inputMode->currentIndex() == 1) {
        const QUrl url(downloadUrlEdit->text().trimmed(), QUrl::StrictMode);
        if (!ChapterDownload::isDownloadUrl(url)) {
            outputPathEdit->clear();
        } else {
            auto name = QFileInfo(url.fileName()).completeBaseName().left(120);
            name.replace(QRegularExpression(QStringLiteral(R"([<>:"/\\|?*\x00-\x1F])")), QStringLiteral("_"));
            name.remove(QRegularExpression(QStringLiteral("[. ]+$")));
            const QRegularExpression reservedName(
                QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\\..*)?$"),
                QRegularExpression::CaseInsensitiveOption);
            if (name.isEmpty() || reservedName.match(name).hasMatch())
                name = QStringLiteral("chapter");
            auto directory = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
            if (directory.isEmpty())
                directory = QDir::homePath();
            outputPathEdit->setText(QDir(directory).filePath(name + ".txt"));
        }
        statusLabel->setText(QStringLiteral("仅在点击下载后访问链接；下载成功并校验通过后才会保存 TXT。"));
    } else {
        const QFileInfo input(inputPathEdit->text());
        if (inputPathEdit->text().isEmpty())
            outputPathEdit->clear();
        else
            outputPathEdit->setText(input.dir().filePath(input.completeBaseName() + ".txt"));
        statusLabel->setText(QStringLiteral("就绪。解码成功后才会写入 TXT，源文件将保留。"));
    }
    updateAction();
}

void MainWindow::updateAction()
{
    const bool hasInput = inputMode->currentIndex() == 1
        ? ChapterDownload::isDownloadUrl(QUrl(downloadUrlEdit->text().trimmed(), QUrl::StrictMode))
        : !inputPathEdit->text().isEmpty();
    const auto key = keyEdit->text();
    const bool hasKey = key.size() == 16
        && std::all_of(key.cbegin(), key.cend(), [](QChar ch) {
            return ch.unicode() >= 0x20 && ch.unicode() <= 0x7e;
        });
    decodeButton->setEnabled(!downloader->isBusy() && hasInput && hasKey && !outputPathEdit->text().isEmpty());
}

void MainWindow::decodeAndSave()
{
    const auto outputPath = outputPathEdit->text();
    const bool fromUrl = inputMode->currentIndex() == 1;
    const QUrl url(downloadUrlEdit->text().trimmed(), QUrl::StrictMode);
    if (fromUrl && !ChapterDownload::isDownloadUrl(url)) {
        statusLabel->setText(QStringLiteral("请输入完整的 HTTP(S) 下载链接，不支持包含账号密码的链接。"));
        return;
    }

    bool allowOverwrite = false;
    if (QFileInfo::exists(outputPath)) {
        allowOverwrite = QMessageBox::question(
            this, QStringLiteral("确认覆盖 TXT"),
            QStringLiteral("输出文件已存在：\n%1\n\n是否覆盖？").arg(QDir::toNativeSeparators(outputPath)),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
        if (!allowOverwrite) {
            statusLabel->setText(QStringLiteral("已取消，未修改任何文件。"));
            return;
        }
    }

    if (fromUrl)
        startDownload(url, outputPath, allowOverwrite);
    else
        convertAndReport(inputPathEdit->text(), outputPath, allowOverwrite);
}

void MainWindow::convertAndReport(const QString &inputPath, const QString &outputPath, bool allowOverwrite)
{
    decodeButton->setEnabled(false);
    const auto result = NovDecoder::convertFile(inputPath, outputPath, keyEdit->text().toLatin1(), allowOverwrite);
    updateAction();
    if (!result.ok()) {
        statusLabel->setText(QStringLiteral("未保存：%1").arg(result.error));
        return;
    }
    reportSaved(outputPath, result.bytesWritten, result.characters);
}

void MainWindow::reportSaved(const QString &outputPath, qint64 bytes, qsizetype characters)
{
    statusLabel->setText(QStringLiteral("已保存：%1\nUTF-8 · %2 字节 · %3 个字符（含换行与标点）。")
                             .arg(QDir::toNativeSeparators(QFileInfo(outputPath).absoluteFilePath()))
                             .arg(bytes).arg(characters));
}

void MainWindow::startDownload(const QUrl &url, const QString &outputPath, bool allowOverwrite)
{
    pathsWidget->setEnabled(false);
    tabs->setTabEnabled(1, false);
    decodeButton->setEnabled(false);
    cancelButton->setEnabled(true);
    cancelButton->setVisible(true);
    downloadProgress->setRange(0, 0);
    downloadProgress->setVisible(true);
    statusLabel->setText(QStringLiteral("正在下载章节资源…"));
    downloader->start(url, outputPath, keyEdit->text().toLatin1(), allowOverwrite);
}
