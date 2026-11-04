#include "chapterdownload.h"
#include "novdecoder.h"

#include <QDir>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTemporaryFile>

namespace {

bool isNovelUrl(const QUrl &url)
{
    return ChapterDownload::isDownloadUrl(url)
        && url.scheme() == QStringLiteral("https")
        && url.host().compare(QStringLiteral("vgo.pkgplud.cn"), Qt::CaseInsensitive) == 0
        && (url.port() == -1 || url.port() == 443);
}

}

ChapterDownload::ChapterDownload(QObject *parent)
    : QObject(parent), manager(new QNetworkAccessManager(this))
{
}

ChapterDownload::~ChapterDownload()
{
    if (reply) {
        reply->disconnect(this);
        reply->abort();
    }
    decodeKey.fill('\0');
}

bool ChapterDownload::isDownloadUrl(const QUrl &url)
{
    return url.isValid() && !url.isRelative() && !url.host().isEmpty()
        && (url.scheme() == QStringLiteral("http") || url.scheme() == QStringLiteral("https"))
        && url.userInfo().isEmpty();
}

void ChapterDownload::start(const QUrl &url, const QString &outputPath, const QByteArray &key,
                            bool allowOverwrite, bool restrictToNovelCdn)
{
    begin(url, outputPath, key, allowOverwrite, restrictToNovelCdn, false);
}

void ChapterDownload::startPreview(const QUrl &url, const QByteArray &key)
{
    begin(url, {}, key, false, true, true);
}

void ChapterDownload::begin(const QUrl &url, const QString &outputPath, const QByteArray &key,
                            bool allowOverwrite, bool restrictToNovelCdn, bool preview)
{
    if (isBusy())
        return;
    if (!isDownloadUrl(url) || (restrictToNovelCdn && !isNovelUrl(url))) {
        emit finished(false, QStringLiteral("下载链接无效或不属于允许的正文服务器。"), 0, 0);
        return;
    }
    if (key.size() != 16) {
        emit finished(false, QStringLiteral("正文解码密钥必须恰好为 16 字节。"), 0, 0);
        return;
    }
    previewMode = preview;
    previewBody.clear();
    expectedDirectory = restrictToNovelCdn && !preview ? QFileInfo(outputPath).dir().canonicalPath() : QString();
    if (restrictToNovelCdn && !preview && expectedDirectory.isEmpty()) {
        emit finished(false, QStringLiteral("书籍保存目录不存在，当前章节未下载。"), 0, 0);
        return;
    }
    if (!preview) {
        temporary = std::make_unique<QTemporaryFile>(
            QDir::temp().filePath(QStringLiteral("NovDecoder-XXXXXX.nov")));
        if (!temporary->open()) {
            temporary.reset();
            emit finished(false, QStringLiteral("无法创建章节下载临时文件。"), 0, 0);
            return;
        }
    }
    output = outputPath;
    decodeKey = key;
    overwrite = allowOverwrite;
    error.clear();

    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::UserVerifiedRedirectPolicy);
    request.setMaximumRedirectsAllowed(5);
    request.setTransferTimeout(30000);
    reply = manager->get(request);
    reply->setReadBufferSize(64 * 1024);
    connect(reply, &QNetworkReply::redirected, this,
            [this, previous = url, restrictToNovelCdn](const QUrl &redirect) mutable {
        const auto target = previous.resolved(redirect);
        if (!isDownloadUrl(target) || (restrictToNovelCdn && !isNovelUrl(target))
            || (previous.scheme() == QStringLiteral("https") && target.scheme() != QStringLiteral("https"))) {
            fail(QStringLiteral("已拒绝不安全的章节重定向。"));
            return;
        }
        previous = target;
        reply->redirectAllowed();
    });
    connect(reply, &QNetworkReply::metaDataChanged, this, [this] {
        if (reply->header(QNetworkRequest::ContentLengthHeader).toLongLong() > NovDecoder::MaxFileSize)
            fail(QStringLiteral("下载文件超过 32 MiB 限制。"));
    });
    connect(reply, &QIODevice::readyRead, this, &ChapterDownload::readData);
    connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        if (!error.isEmpty())
            return;
        if (received > NovDecoder::MaxFileSize || total > NovDecoder::MaxFileSize) {
            fail(QStringLiteral("下载文件超过 32 MiB 限制。"));
            return;
        }
        emit progress(received, total);
    });
    connect(reply, &QNetworkReply::finished, this, &ChapterDownload::complete);
}

void ChapterDownload::readData()
{
    if (!error.isEmpty())
        return;
    const bool fileResponse = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200;
    while (reply->bytesAvailable() > 0) {
        const auto bytes = reply->read(64 * 1024);
        if (bytes.isEmpty())
            break;
        if (!fileResponse)
            continue;
        const auto size = previewMode ? previewBody.size() : temporary->size();
        if (size + bytes.size() > NovDecoder::MaxFileSize) {
            fail(QStringLiteral("下载文件超过 32 MiB 限制。"));
            return;
        }
        if (previewMode) {
            previewBody.append(bytes);
        } else if (temporary->write(bytes) != bytes.size()) {
            fail(QStringLiteral("无法写入章节下载临时文件。"));
            return;
        }
    }
}

void ChapterDownload::fail(const QString &message)
{
    if (!error.isEmpty())
        return;
    error = message;
    if (reply && !reply->isFinished())
        reply->abort();
}

void ChapterDownload::cancel()
{
    if (isBusy())
        fail(QStringLiteral("已取消下载，当前章节未保存。"));
}

void ChapterDownload::complete()
{
    const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (error.isEmpty()) {
        if (status >= 400)
            error = QStringLiteral("下载失败（HTTP %1）。链接可能过期或无权访问，请重新获取。").arg(status);
        else if (reply->error() != QNetworkReply::NoError)
            error = QStringLiteral("下载失败（网络错误 %1）。").arg(static_cast<int>(reply->error()));
        else if (status != 200)
            error = QStringLiteral("服务器未返回完整章节（HTTP %1）。").arg(status);
        else
            readData();
    }
    if (error.isEmpty() && !expectedDirectory.isEmpty()) {
        const auto directory = QFileInfo(output).dir().absolutePath();
        const QFileInfo info(directory);
        if (!info.isDir() || info.isSymLink() || info.isJunction()
            || info.canonicalFilePath().compare(expectedDirectory, Qt::CaseInsensitive) != 0)
            error = QStringLiteral("下载期间保存目录发生变化，当前章节未写入。");
    }
    reply->deleteLater();
    reply = nullptr;
    NovDecoder::SaveResult result;
    QString previewText;
    if (previewMode) {
        if (error.isEmpty()) {
            const auto decoded = NovDecoder::decode(previewBody, decodeKey);
            result.error = decoded.error;
            result.characters = decoded.characters;
            if (decoded.ok())
                previewText = QString::fromUtf8(decoded.utf8);
        } else {
            result.error = error;
        }
        previewBody.clear();
    } else {
        if (error.isEmpty() && !temporary->flush())
            error = QStringLiteral("无法保存章节下载临时文件。");
        const auto input = temporary->fileName();
        temporary->close();
        if (error.isEmpty())
            result = NovDecoder::convertFile(input, output, decodeKey, overwrite);
        else
            result.error = error;
        temporary.reset();
    }
    decodeKey.fill('\0');
    decodeKey.clear();
    if (previewMode && result.ok())
        emit previewReady(previewText);
    emit finished(result.ok(), result.error, result.bytesWritten, result.characters);
}
