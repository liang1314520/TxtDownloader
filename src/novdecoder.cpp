#include "novdecoder.h"

#include <windows.h>
#include <bcrypt.h>

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStringDecoder>

#include <algorithm>

namespace {

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

namespace NovDecoder {

DecodeResult decode(const QByteArray &encrypted, QByteArray key)
{
    if (key.size() != 16)
        return {{}, QStringLiteral("AES-128 密钥必须恰好为 16 字节。"), 0};
    if (encrypted.isEmpty())
        return {{}, QStringLiteral("文件为空，没有可解码的数据。"), 0};
    if (encrypted.size() > MaxFileSize)
        return {{}, QStringLiteral("文件超过 32 MiB 限制，请选择单章资源文件。"), 0};
    if (encrypted.size() % 16 != 0)
        return {{}, QStringLiteral("文件长度不是 16 字节的整数倍，可能未下载完整或不是受支持的章节文件。"), 0};

    AesHandles aes;
    auto status = BCryptOpenAlgorithmProvider(&aes.algorithm, BCRYPT_AES_ALGORITHM, nullptr, 0);
    if (status < 0)
        return {{}, QStringLiteral("无法初始化 Windows AES 服务。"), 0};

    status = BCryptSetProperty(aes.algorithm, BCRYPT_CHAINING_MODE,
                              reinterpret_cast<PUCHAR>(const_cast<wchar_t *>(BCRYPT_CHAIN_MODE_ECB)),
                              sizeof(BCRYPT_CHAIN_MODE_ECB), 0);
    if (status < 0)
        return {{}, QStringLiteral("无法设置 AES-ECB 模式。"), 0};

    status = BCryptGenerateSymmetricKey(aes.algorithm, &aes.key, nullptr, 0,
                                        reinterpret_cast<PUCHAR>(key.data()),
                                        static_cast<ULONG>(key.size()), 0);
    SecureZeroMemory(key.data(), static_cast<SIZE_T>(key.size()));
    if (status < 0)
        return {{}, QStringLiteral("无法创建章节解码密钥。"), 0};

    QByteArray plaintext(encrypted.size(), Qt::Uninitialized);
    ULONG bytesWritten = 0;
    status = BCryptDecrypt(aes.key,
                           reinterpret_cast<PUCHAR>(const_cast<char *>(encrypted.constData())),
                           static_cast<ULONG>(encrypted.size()), nullptr, nullptr, 0,
                           reinterpret_cast<PUCHAR>(plaintext.data()),
                           static_cast<ULONG>(plaintext.size()), &bytesWritten, BCRYPT_BLOCK_PADDING);
    if (status < 0)
        return {{}, QStringLiteral("解码或填充校验失败。文件可能损坏、格式不支持，或并非当前版本的章节资源。"), 0};
    plaintext.resize(bytesWritten);
    if (plaintext.isEmpty())
        return {{}, QStringLiteral("解码结果为空，没有正文内容。"), 0};

    QStringDecoder decoder(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
    const QString text = decoder(plaintext);
    if (decoder.hasError() || text.contains(QChar::Null))
        return {{}, QStringLiteral("解码结果不是有效的 UTF-8 文本，未保存输出文件。"), 0};

    const auto characters = std::count_if(text.cbegin(), text.cend(),
                                          [](QChar ch) { return !ch.isLowSurrogate(); });
    return {plaintext, {}, static_cast<qsizetype>(characters)};
}

SaveResult convertFile(const QString &inputPath, const QString &outputPath,
                       const QByteArray &key, bool allowOverwrite)
{
    if (inputPath.isEmpty() || outputPath.isEmpty())
        return {QStringLiteral("请选择输入文件和 TXT 保存位置。"), 0, 0};

    const QFileInfo inputInfo(inputPath);
    const QFileInfo outputInfo(outputPath);
    if (!inputInfo.isFile())
        return {QStringLiteral("输入文件不存在或不是普通文件。"), 0, 0};
    if (outputInfo.isSymLink())
        return {QStringLiteral("输出位置不能是符号链接或快捷方式，请另选一个 TXT 路径。"), 0, 0};
    if (inputInfo.absoluteFilePath().compare(outputInfo.absoluteFilePath(), Qt::CaseInsensitive) == 0
        || (outputInfo.exists()
            && inputInfo.canonicalFilePath().compare(outputInfo.canonicalFilePath(),
                                                      Qt::CaseInsensitive) == 0)) {
        return {QStringLiteral("TXT 输出路径不能与源文件相同，原文件不会被覆盖。"), 0, 0};
    }
    if (outputInfo.exists() && !allowOverwrite)
        return {QStringLiteral("输出文件已存在，需要确认覆盖。"), 0, 0};

    QFile input(inputPath);
    if (!input.open(QIODevice::ReadOnly))
        return {QStringLiteral("无法读取输入文件：%1").arg(input.errorString()), 0, 0};
    if (input.size() > MaxFileSize)
        return {QStringLiteral("文件超过 32 MiB 限制，请选择单章资源文件。"), 0, 0};
    const auto encrypted = input.read(MaxFileSize + 1);
    if (input.error() != QFileDevice::NoError)
        return {QStringLiteral("读取文件失败：%1").arg(input.errorString()), 0, 0};
    input.close();

    const auto result = decode(encrypted, key);
    if (!result.ok())
        return {result.error, 0, 0};

    QSaveFile output(outputPath);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly))
        return {QStringLiteral("无法创建 TXT 文件：%1").arg(output.errorString()), 0, 0};
    if (output.write(result.utf8) != result.utf8.size())
        return {QStringLiteral("写入 TXT 失败：%1").arg(output.errorString()), 0, 0};
    if (!allowOverwrite && QFileInfo::exists(outputPath))
        return {QStringLiteral("保存期间出现同名文件，已取消保存以避免覆盖。"), 0, 0};
    if (!output.commit())
        return {QStringLiteral("保存 TXT 失败：%1").arg(output.errorString()), 0, 0};
    return {{}, result.utf8.size(), result.characters};
}

}
