#include "mainwindow.h"
#include "novdecoder.h"

#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace {

bool isDownloadUrl(const QUrl &url)
{
    return url.isValid() && !url.isRelative() && !url.host().isEmpty()
        && (url.scheme() == QStringLiteral("http") || url.scheme() == QStringLiteral("https"))
        && url.userInfo().isEmpty();
}

}

MainWindow::MainWindow(QWidget *parent) : QWidget(parent)
{
    setWindowTitle(QStringLiteral("NOV 下载与解码工具"));
    resize(760, 430);
    setMinimumWidth(580);

    auto *layout = new QVBoxLayout(this);
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

    networkManager = new QNetworkAccessManager(this);
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
        if (downloadReply) {
            cancelButton->setEnabled(false);
            failDownload(QStringLiteral("已取消下载，未写入 TXT 文件。"));
        }
    });
}

MainWindow::~MainWindow()
{
    if (downloadReply) {
        downloadReply->disconnect(this);
        downloadReply->abort();
    }
}

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
        if (!isDownloadUrl(url)) {
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
        ? isDownloadUrl(QUrl(downloadUrlEdit->text().trimmed(), QUrl::StrictMode))
        : !inputPathEdit->text().isEmpty();
    const auto key = keyEdit->text();
    const bool hasKey = key.size() == 16
        && std::all_of(key.cbegin(), key.cend(), [](QChar ch) {
            return ch.unicode() >= 0x20 && ch.unicode() <= 0x7e;
        });
    decodeButton->setEnabled(!downloadReply && hasInput && hasKey && !outputPathEdit->text().isEmpty());
}

void MainWindow::decodeAndSave()
{
    const auto outputPath = outputPathEdit->text();
    const bool fromUrl = inputMode->currentIndex() == 1;
    const QUrl url(downloadUrlEdit->text().trimmed(), QUrl::StrictMode);
    if (fromUrl && !isDownloadUrl(url)) {
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
    statusLabel->setText(QStringLiteral("已保存：%1\nUTF-8 · %2 字节 · %3 个字符（含换行与标点）。")
                             .arg(QDir::toNativeSeparators(QFileInfo(outputPath).absoluteFilePath()))
                             .arg(result.bytesWritten)
                             .arg(result.characters));
}

void MainWindow::startDownload(const QUrl &url, const QString &outputPath, bool allowOverwrite)
{
    downloadFile = new QTemporaryFile(QDir::temp().filePath(QStringLiteral("NovDecoder-XXXXXX.nov")), this);
    if (!downloadFile->open()) {
        statusLabel->setText(QStringLiteral("未保存：无法创建下载临时文件：%1").arg(downloadFile->errorString()));
        delete downloadFile;
        downloadFile = nullptr;
        return;
    }

    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::UserVerifiedRedirectPolicy);
    request.setMaximumRedirectsAllowed(5);
    request.setTransferTimeout(30000);
    downloadError.clear();
    downloadReply = networkManager->get(request);
    downloadReply->setReadBufferSize(64 * 1024);
    pathsWidget->setEnabled(false);
    decodeButton->setEnabled(false);
    cancelButton->setEnabled(true);
    cancelButton->setVisible(true);
    downloadProgress->setRange(0, 0);
    downloadProgress->setVisible(true);
    statusLabel->setText(QStringLiteral("正在下载章节资源…"));

    connect(downloadReply, &QNetworkReply::redirected, this,
            [this, previousUrl = url](const QUrl &redirectUrl) mutable {
        const auto target = previousUrl.resolved(redirectUrl);
        if (!isDownloadUrl(target)
            || (previousUrl.scheme() == QStringLiteral("https")
                && target.scheme() != QStringLiteral("https"))) {
            failDownload(QStringLiteral("已拒绝不安全的重定向，未写入 TXT 文件。"));
            return;
        }
        previousUrl = target;
        downloadReply->redirectAllowed();
    });
    connect(downloadReply, &QNetworkReply::metaDataChanged, this, [this] {
        if (downloadReply->header(QNetworkRequest::ContentLengthHeader).toLongLong() > NovDecoder::MaxFileSize)
            failDownload(QStringLiteral("下载文件超过 32 MiB 限制，已取消下载。"));
    });
    connect(downloadReply, &QIODevice::readyRead, this, &MainWindow::readDownloadData);
    connect(downloadReply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        if (!downloadError.isEmpty())
            return;
        if (received > NovDecoder::MaxFileSize || total > NovDecoder::MaxFileSize) {
            failDownload(QStringLiteral("下载文件超过 32 MiB 限制，已取消下载。"));
            return;
        }
        if (total > 0) {
            downloadProgress->setRange(0, 100);
            downloadProgress->setValue(static_cast<int>(received * 100 / total));
        } else {
            downloadProgress->setRange(0, 0);
        }
        statusLabel->setText(QStringLiteral("正在下载：%1 KiB%2")
                                 .arg(received / 1024)
                                 .arg(total > 0 ? QStringLiteral(" / %1 KiB").arg(total / 1024) : QString()));
    });
    connect(downloadReply, &QNetworkReply::finished, this, [this, outputPath, allowOverwrite] {
        finishDownload(outputPath, allowOverwrite);
    });
}

void MainWindow::readDownloadData()
{
    if (!downloadError.isEmpty())
        return;
    const bool isFileResponse = downloadReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200;
    while (downloadReply->bytesAvailable() > 0) {
        const auto data = downloadReply->read(64 * 1024);
        if (data.isEmpty())
            break;
        if (!isFileResponse)
            continue;
        if (downloadFile->size() + data.size() > NovDecoder::MaxFileSize) {
            failDownload(QStringLiteral("下载文件超过 32 MiB 限制，已取消下载。"));
            return;
        }
        if (downloadFile->write(data) != data.size()) {
            failDownload(QStringLiteral("无法写入下载临时文件：%1").arg(downloadFile->errorString()));
            return;
        }
    }
}

void MainWindow::failDownload(const QString &error)
{
    if (!downloadError.isEmpty())
        return;
    downloadError = error;
    if (!downloadReply->isFinished())
        downloadReply->abort();
}

void MainWindow::finishDownload(const QString &outputPath, bool allowOverwrite)
{
    if (downloadError.isEmpty()) {
        const auto status = downloadReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status >= 400) {
            downloadError = QStringLiteral("下载失败（HTTP %1）。签名链接可能已过期或无权访问，请重新获取有效链接。")
                                .arg(status);
        } else if (downloadReply->error() != QNetworkReply::NoError) {
            downloadError = QStringLiteral("下载失败（网络错误 %1）。请检查网络和链接；HTTPS 连接还需有效证书。")
                                .arg(static_cast<int>(downloadReply->error()));
        } else if (status != 200) {
            downloadError = QStringLiteral("服务器未返回完整文件（HTTP %1），未转换 TXT。").arg(status);
        } else {
            readDownloadData();
        }
    }
    if (downloadError.isEmpty() && !downloadFile->flush())
        downloadError = QStringLiteral("无法保存下载临时文件：%1").arg(downloadFile->errorString());

    const auto inputPath = downloadFile->fileName();
    downloadFile->close();
    downloadReply->deleteLater();
    downloadReply = nullptr;
    cancelButton->setVisible(false);
    downloadProgress->setVisible(false);
    if (downloadError.isEmpty())
        convertAndReport(inputPath, outputPath, allowOverwrite);
    else
        statusLabel->setText(QStringLiteral("未保存：%1").arg(downloadError));
    delete downloadFile;
    downloadFile = nullptr;
    pathsWidget->setEnabled(true);
    updateAction();
}
