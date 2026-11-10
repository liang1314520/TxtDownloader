#include "videocover.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>

namespace {
constexpr qsizetype MaxImageBytes = 8 * 1024 * 1024;
constexpr qint64 DiskBudget = 256 * 1024 * 1024;
bool validKey(const QString &key)
{
    static const QRegularExpression pattern(QStringLiteral("^[0-9a-f]{64}$"));
    return pattern.match(key).hasMatch();
}
bool validSize(const QSize &size)
{
    return size.width() > 0 && size.height() > 0 && size.width() <= 8192 && size.height() <= 8192
        && qint64(size.width()) * size.height() <= 16000000;
}
VideoCover::Result readImage(QByteArray bytes)
{
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    const auto format = reader.format().toLower();
    if ((format != "png" && format != "jpeg" && format != "jpg" && format != "webp")
        || !validSize(reader.size()) || reader.supportsAnimation())
        return {{}, QStringLiteral("封面格式或尺寸不受支持。 ")};
    const auto image = reader.read();
    if (image.isNull() || !validSize(image.size()))
        return {{}, QStringLiteral("封面图片无效。 ")};
    return {image.scaled(image.size().boundedTo(QSize(360, 240)), Qt::KeepAspectRatio, Qt::SmoothTransformation), {}};
}
struct AesHandles {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_KEY_HANDLE key = nullptr;
    ~AesHandles()
    {
        if (key)
            BCryptDestroyKey(key);
        if (algorithm)
            BCryptCloseAlgorithmProvider(algorithm, 0);
    }
};
}
QByteArray VideoCover::deriveKey(const QString &originalUrl)
{
    const auto stem = originalUrl.split(QLatin1Char('/'), Qt::KeepEmptyParts).last()
                          .section(QLatin1Char('.'), 0, 0).toUtf8();
    auto input = stem;
    if (originalUrl.contains(QStringLiteral("vlog")))
        input = QCryptographicHash::hash(input, QCryptographicHash::Sha1).toHex();
    return QCryptographicHash::hash(input, QCryptographicHash::Md5);
}
VideoCover::Result VideoCover::decode(const QString &originalUrl, const QByteArray &payload)
{
    if (payload.isEmpty() || payload.size() > MaxImageBytes)
        return {{}, QStringLiteral("封面为空或超过 8 MiB。 ")};
    const auto plain = readImage(payload);
    if (plain.error.isEmpty())
        return plain;
    if (originalUrl.size() < 27 || payload.size() % 16 != 0)
        return {{}, QStringLiteral("封面编码无效，使用占位图。 ")};
    auto key = deriveKey(originalUrl);
    auto iv = key;
    AesHandles aes;
    auto status = BCryptOpenAlgorithmProvider(&aes.algorithm, BCRYPT_AES_ALGORITHM, nullptr, 0);
    if (status >= 0)
        status = BCryptSetProperty(aes.algorithm, BCRYPT_CHAINING_MODE,
            reinterpret_cast<PUCHAR>(const_cast<wchar_t *>(BCRYPT_CHAIN_MODE_CBC)), sizeof(BCRYPT_CHAIN_MODE_CBC), 0);
    if (status >= 0)
        status = BCryptGenerateSymmetricKey(aes.algorithm, &aes.key, nullptr, 0,
            reinterpret_cast<PUCHAR>(key.data()), static_cast<ULONG>(key.size()), 0);
    SecureZeroMemory(key.data(), key.size());
    if (status < 0) {
        SecureZeroMemory(iv.data(), iv.size());
        return {{}, QStringLiteral("无法初始化封面转换。 ")};
    }
    QByteArray decoded(payload.size(), Qt::Uninitialized);
    ULONG written = 0;
    status = BCryptDecrypt(aes.key, reinterpret_cast<PUCHAR>(const_cast<char *>(payload.constData())),
        static_cast<ULONG>(payload.size()), nullptr, reinterpret_cast<PUCHAR>(iv.data()), static_cast<ULONG>(iv.size()),
        reinterpret_cast<PUCHAR>(decoded.data()), static_cast<ULONG>(decoded.size()), &written, BCRYPT_BLOCK_PADDING);
    SecureZeroMemory(iv.data(), iv.size());
    if (status < 0)
        return {{}, QStringLiteral("封面转换或填充校验失败，使用占位图。 ")};
    decoded.resize(written);
    return readImage(decoded);
}
VideoCoverCache::VideoCoverCache(const VideoStoragePaths &storage, QObject *parent)
    : QObject(parent), paths(storage)
{
}
QString VideoCoverCache::pathFor(const QString &key) const
{
    return QDir(paths.cacheRoot).filePath(QStringLiteral("thumbnails/%1.png").arg(key));
}
QImage VideoCoverCache::cached(const QString &key) const
{
    if (!validKey(key))
        return {};
    if (auto *image = memory.object(key))
        return *image;
    const auto path = pathFor(key);
    if (!VideoStore::safeLocation(path))
        return {};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > MaxImageBytes)
        return {};
    const auto result = VideoCover::decode({}, file.read(MaxImageBytes + 1));
    if (!result.error.isEmpty())
        return {};
    file.setFileTime(QDateTime::currentDateTimeUtc(), QFileDevice::FileAccessTime);
    const auto cost = static_cast<int>(result.image.sizeInBytes());
    memory.insert(key, new QImage(result.image), cost);
    return result.image;
}
QString VideoCoverCache::store(const QString &key, const QImage &source)
{
    if (!validKey(key) || !validSize(source.size()))
        return QStringLiteral("封面缓存键或尺寸无效。 ");
    const auto image = source.scaled(QSize(360, 240), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "PNG") || bytes.size() > MaxImageBytes)
        return QStringLiteral("无法生成有界封面缩略图。 ");
    const auto path = pathFor(key);
    const auto lockPath = QDir(paths.cacheRoot).filePath("thumbnails.lock");
    if (!VideoStore::safeLocation(path) || !VideoStore::safeLocation(lockPath)
        || !QDir().mkpath(QFileInfo(path).absolutePath()) || !VideoStore::safeLocation(path)
        || !VideoStore::safeLocation(lockPath))
        return QStringLiteral("封面缓存位置不安全。 ");
    QLockFile lock(lockPath);
    if (!lock.tryLock(0))
        return QStringLiteral("封面缓存正在被其他窗口使用。 ");
    QDir directory(QFileInfo(path).absolutePath());
    auto files = directory.entryInfoList({QStringLiteral("*.png")}, QDir::Files | QDir::NoSymLinks);
    std::sort(files.begin(), files.end(), [](const QFileInfo &a, const QFileInfo &b) {
        return a.lastRead() < b.lastRead();
    });
    qint64 total = bytes.size();
    for (const auto &file : files) {
        if (validKey(file.completeBaseName()) && file.absoluteFilePath() != path)
            total += file.size();
    }
    for (const auto &file : files) {
        if (total <= DiskBudget)
            break;
        if (validKey(file.completeBaseName()) && file.absoluteFilePath() != path
            && VideoStore::safeLocation(file.absoluteFilePath()) && QFile::remove(file.absoluteFilePath()))
            total -= file.size();
    }
    if (total > DiskBudget)
        return QStringLiteral("封面缓存达到 256 MiB 且无法安全淘汰。 ");
    QSaveFile output(path);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit())
        return QStringLiteral("无法保存封面缩略图。 ");
    memory.insert(key, new QImage(image), static_cast<int>(image.sizeInBytes()));
    emit thumbnailReady(key);
    return {};
}
