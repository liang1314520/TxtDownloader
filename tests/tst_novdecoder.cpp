#include "novdecoder.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

namespace {

// 公开合成数据专用，不对应任何真实章节的密钥。
QByteArray testKey()
{
    return QByteArrayLiteral("0123456789abcdef");
}

QByteArray encryptedSample()
{
    return QByteArray::fromHex(
        "9d1a2433cc11719bbceca20e6b8cc9abe9fab02d3b530f4cf2b3f4b69aa8c48c"
        "17633df7684baa527dcd5c1f28e417b4");
}

QByteArray plainSample()
{
    return QStringLiteral("离线解码测试\r\n第二行。\r\n").toUtf8();
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

class NovDecoderTest : public QObject {
    Q_OBJECT

private slots:
    void decodesKnownCiphertext();
    void rejectsInvalidData_data();
    void rejectsInvalidData();
    void savesUtf8AndPreservesSource();
    void refusesOverwriteWithoutPermission();
    void overwritesAfterPermission();
    void refusesSourceAsOutput();
    void failedDecodeLeavesOutputUntouched();
    void failedDecodeCreatesNoOutput();
    void reportsMissingInput();
    void reportsMissingOutputDirectory();
    void rejectsOversizedFile();
};

void NovDecoderTest::decodesKnownCiphertext()
{
    const auto result = NovDecoder::decode(encryptedSample(), testKey());
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.utf8, plainSample());
    QCOMPARE(result.characters, qsizetype(14));
}

void NovDecoderTest::rejectsInvalidData_data()
{
    QTest::addColumn<QByteArray>("ciphertext");
    QTest::newRow("empty") << QByteArray();
    QTest::newRow("truncated") << encryptedSample().chopped(1);
    auto damaged = encryptedSample();
    damaged[damaged.size() - 1] ^= 0x01;
    QTest::newRow("bad-padding") << damaged;
    QTest::newRow("wrong-key-ciphertext") << QByteArray::fromHex(
        "50f34669cce85b7a020ccdf5bc0fde3b0e04b606da986b3d28ae43e8a1abe706f"
        "737972ddeb3da5ff9d66386a9bd013e");
    QTest::newRow("incomplete-utf8")
        << QByteArray::fromHex("a4c60ebd7835b2cd433be26ded206d10");
    QTest::newRow("nul-byte")
        << QByteArray::fromHex("81fd4251cddb6f7874697ebb9029fa8c");
    QTest::newRow("empty-plaintext")
        << QByteArray::fromHex("377222e061a924c591cd9c27ea163ed4");
}

void NovDecoderTest::rejectsInvalidData()
{
    QFETCH(QByteArray, ciphertext);
    const auto result = NovDecoder::decode(ciphertext, testKey());
    QVERIFY(!result.ok());
    QVERIFY(!result.error.isEmpty());
    QVERIFY(result.utf8.isEmpty());
}

void NovDecoderTest::savesUtf8AndPreservesSource()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto input = dir.filePath(QStringLiteral("章节.nov"));
    const auto output = dir.filePath(QStringLiteral("章节.txt"));
    QVERIFY(writeFile(input, encryptedSample()));

    const auto result = NovDecoder::convertFile(input, output, testKey());
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.bytesWritten, qint64(plainSample().size()));
    QCOMPARE(readFile(output), plainSample());
    QCOMPARE(readFile(input), encryptedSample());
}

void NovDecoderTest::refusesOverwriteWithoutPermission()
{
    QTemporaryDir dir;
    const auto input = dir.filePath("chapter.bin");
    const auto output = dir.filePath("chapter.txt");
    QVERIFY(writeFile(input, encryptedSample()));
    QVERIFY(writeFile(output, "existing content"));

    const auto result = NovDecoder::convertFile(input, output, testKey());
    QVERIFY(!result.ok());
    QCOMPARE(readFile(output), QByteArray("existing content"));
}

void NovDecoderTest::overwritesAfterPermission()
{
    QTemporaryDir dir;
    const auto input = dir.filePath("chapter.nov");
    const auto output = dir.filePath("chapter.txt");
    QVERIFY(writeFile(input, encryptedSample()));
    QVERIFY(writeFile(output, "existing content"));

    const auto result = NovDecoder::convertFile(input, output, testKey(), true);
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(readFile(output), plainSample());
}

void NovDecoderTest::refusesSourceAsOutput()
{
    QTemporaryDir dir;
    const auto input = dir.filePath("chapter.nov");
    QVERIFY(writeFile(input, encryptedSample()));
    const auto result = NovDecoder::convertFile(input, input.toUpper(), testKey(), true);
    QVERIFY(!result.ok());
    QCOMPARE(readFile(input), encryptedSample());
}

void NovDecoderTest::failedDecodeLeavesOutputUntouched()
{
    QTemporaryDir dir;
    const auto input = dir.filePath("broken.nov");
    const auto output = dir.filePath("chapter.txt");
    QVERIFY(writeFile(input, "invalid data"));
    QVERIFY(writeFile(output, "existing content"));
    QVERIFY(!NovDecoder::convertFile(input, output, testKey(), true).ok());
    QCOMPARE(readFile(output), QByteArray("existing content"));
}

void NovDecoderTest::failedDecodeCreatesNoOutput()
{
    QTemporaryDir dir;
    const auto input = dir.filePath("broken.nov");
    const auto output = dir.filePath("chapter.txt");
    QVERIFY(writeFile(input, "invalid data"));
    QVERIFY(!NovDecoder::convertFile(input, output, testKey()).ok());
    QVERIFY(!QFile::exists(output));
}

void NovDecoderTest::reportsMissingInput()
{
    QTemporaryDir dir;
    const auto output = dir.filePath("chapter.txt");
    QVERIFY(!NovDecoder::convertFile(dir.filePath("missing.nov"), output, testKey()).ok());
    QVERIFY(!QFile::exists(output));
}

void NovDecoderTest::reportsMissingOutputDirectory()
{
    QTemporaryDir dir;
    const auto input = dir.filePath("chapter.nov");
    QVERIFY(writeFile(input, encryptedSample()));
    QVERIFY(!NovDecoder::convertFile(input, dir.filePath("missing/chapter.txt"), testKey()).ok());
    QCOMPARE(readFile(input), encryptedSample());
}

void NovDecoderTest::rejectsOversizedFile()
{
    QTemporaryDir dir;
    const auto input = dir.filePath("large.nov");
    QFile file(input);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.resize(32 * 1024 * 1024 + 1));
    file.close();
    QVERIFY(!NovDecoder::convertFile(input, dir.filePath("large.txt"), testKey()).ok());
    QVERIFY(!QFile::exists(dir.filePath("large.txt")));
}

QTEST_GUILESS_MAIN(NovDecoderTest)
#include "tst_novdecoder.moc"
