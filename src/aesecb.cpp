#include "aesecb.h"

#include <windows.h>
#include <bcrypt.h>

#include <limits>

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

AesEcb::Result transform(const QByteArray &input, QByteArray key, bool encrypting)
{
    if (key.size() != 16)
        return {{}, QStringLiteral("AES-128 密钥必须恰好为 16 字节。")};
    if (input.isEmpty() || input.size() > std::numeric_limits<ULONG>::max() - 16)
        return {{}, QStringLiteral("AES 输入为空或超过支持的长度。")};
    if (!encrypting && input.size() % 16 != 0)
        return {{}, QStringLiteral("密文长度不是 16 字节的整数倍。")};

    AesHandles aes;
    auto status = BCryptOpenAlgorithmProvider(&aes.algorithm, BCRYPT_AES_ALGORITHM, nullptr, 0);
    if (status < 0)
        return {{}, QStringLiteral("无法初始化 Windows AES 服务。")};
    status = BCryptSetProperty(aes.algorithm, BCRYPT_CHAINING_MODE,
                              reinterpret_cast<PUCHAR>(const_cast<wchar_t *>(BCRYPT_CHAIN_MODE_ECB)),
                              sizeof(BCRYPT_CHAIN_MODE_ECB), 0);
    if (status < 0)
        return {{}, QStringLiteral("无法设置 AES-ECB 模式。")};
    status = BCryptGenerateSymmetricKey(aes.algorithm, &aes.key, nullptr, 0,
                                       reinterpret_cast<PUCHAR>(key.data()),
                                       static_cast<ULONG>(key.size()), 0);
    SecureZeroMemory(key.data(), static_cast<SIZE_T>(key.size()));
    if (status < 0)
        return {{}, QStringLiteral("无法创建 AES 密钥。")};

    QByteArray output(input.size() + (encrypting ? 16 : 0), Qt::Uninitialized);
    ULONG written = 0;
    const auto operation = encrypting ? BCryptEncrypt : BCryptDecrypt;
    status = operation(aes.key,
                       reinterpret_cast<PUCHAR>(const_cast<char *>(input.constData())),
                       static_cast<ULONG>(input.size()), nullptr, nullptr, 0,
                       reinterpret_cast<PUCHAR>(output.data()), static_cast<ULONG>(output.size()),
                       &written, BCRYPT_BLOCK_PADDING);
    if (status < 0)
        return {{}, encrypting ? QStringLiteral("AES 请求加密失败。")
                              : QStringLiteral("解码或填充校验失败。请检查密钥和文件是否完整。")};
    output.resize(written);
    return {output, {}};
}

}

namespace AesEcb {

Result encrypt(const QByteArray &plaintext, QByteArray key)
{
    return transform(plaintext, std::move(key), true);
}

Result decrypt(const QByteArray &encrypted, QByteArray key)
{
    return transform(encrypted, std::move(key), false);
}

}
