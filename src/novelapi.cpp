#include "novelapi.h"
#include "aesecb.h"
#include "novdecoder.h"
#include "localkeyprofile.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSet>
#include <QUrlQuery>

#include <algorithm>

namespace {

bool boundedText(const QString &text, qsizetype maximum)
{
    return !text.isEmpty() && text.size() <= maximum
        && std::none_of(text.cbegin(), text.cend(), [](QChar ch) { return ch.isNull() || ch.category() == QChar::Other_Control; });
}

bool isContentUrl(const QUrl &url)
{
    return url.isValid() && !url.isRelative() && url.scheme() == QStringLiteral("https")
        && url.host().compare(QStringLiteral("vgo.pkgplud.cn"), Qt::CaseInsensitive) == 0
        && url.userInfo().isEmpty() && (url.port() == -1 || url.port() == 443)
        && !url.hasFragment() && url.path().startsWith(QStringLiteral("/media/novel/"))
        && url.path().endsWith(QStringLiteral(".nov"))
        && !QUrlQuery(url).queryItemValue(QStringLiteral("sign")).isEmpty();
}

}

NovelApi::NovelApi(QObject *parent)
    : QObject(parent), manager(new QNetworkAccessManager(this))
{
    delay.setSingleShot(true);
    delay.setTimerType(Qt::PreciseTimer);
    deadline.setSingleShot(true);
    connect(&delay, &QTimer::timeout, this, &NovelApi::send);
    connect(&deadline, &QTimer::timeout, this, [this] {
        stopWithError(QStringLiteral("章节接口响应超时，已停止；不会自动重试。"));
    });
}

NovelApi::~NovelApi()
{
    cancel();
    clearSession();
}

bool NovelApi::isId(const QString &id)
{
    static const QRegularExpression expression(QStringLiteral("\\A[0-9a-fA-F]{32}\\z"));
    return expression.match(id).hasMatch();
}

void NovelApi::clearSession()
{
    cancel();
    apiKey.fill('\0');
    apiKey.clear();
    contentKey.fill('\0');
    contentKey.clear();
    token.fill(QChar::Null);
    token.clear();
    deviceId.fill(QChar::Null);
    deviceId.clear();
    sessionHeaders.clear();
}

QString NovelApi::importSession(const QString &capturePath)
{
    clearSession();
    QFile file(capturePath);
    if (!file.open(QIODevice::ReadOnly))
        return QStringLiteral("无法读取请求抓包文件。");
    if (file.size() <= 0 || file.size() > 1024 * 1024)
        return QStringLiteral("请求抓包为空或超过 1 MiB 限制。");
    const auto raw = file.read(1024 * 1024 + 1);
    if (file.error() != QFileDevice::NoError || raw.size() > 1024 * 1024)
        return QStringLiteral("请求抓包读取失败或超过限制。");
    const auto separator = raw.indexOf("\r\n\r\n");
    if (separator <= 0 || separator > 16 * 1024)
        return QStringLiteral("请选择包含 HTTP 请求头和原始 Body 的 .hcy 请求抓包。");
    const auto lines = raw.first(separator).split('\n');
    const auto requestLine = lines.first().trimmed();
    if (requestLine != "POST /v1/xs/xq HTTP/1.1" && requestLine != "POST /v1/xs/zjxq HTTP/1.1")
        return QStringLiteral("仅支持本应用书籍详情或章节详情的 POST 请求抓包。");
    QMap<QByteArray, QByteArray> headers;
    for (qsizetype i = 1; i < lines.size(); ++i) {
        const auto line = lines[i].trimmed();
        const auto colon = line.indexOf(':');
        if (colon <= 0)
            return QStringLiteral("抓包请求头格式不完整。");
        const auto name = line.first(colon).toLower();
        const auto value = line.sliced(colon + 1).trimmed();
        if (headers.contains(name) || value.size() > 2048
            || std::any_of(value.cbegin(), value.cend(), [](unsigned char ch) { return ch < 0x20 || ch == 0x7f; }))
            return QStringLiteral("抓包包含重复或不安全的请求头。");
        headers.insert(name, value);
    }
    if (headers.value("host").toLower() != "yj.kkj234ls.com"
        || headers.value("content-type").toLower() != "application/octet-stream"
        || headers.contains("transfer-encoding"))
        return QStringLiteral("抓包的服务器或二进制请求格式不受支持。");
    const auto body = raw.sliced(separator + 4);
    if (headers.contains("content-length")) {
        bool valid = false;
        const auto length = headers.value("content-length").toLongLong(&valid);
        if (!valid || length != body.size())
            return QStringLiteral("抓包 Body 长度与 Content-Length 不一致。");
    }
    const auto profileError = LocalKeyProfile::load(QString::fromLatin1(headers.value("version")), apiKey, contentKey);
    if (!profileError.isEmpty())
        return profileError;
    const auto decoded = AesEcb::decrypt(body, apiKey);
    if (!decoded.ok())
        return QStringLiteral("抓包解密失败，请确认请求来自已配置的 App 4.0.9，且原始文件完整。");
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(decoded.bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return QStringLiteral("抓包解密结果不是有效的请求 JSON。");
    const auto object = document.object();
    const auto importedToken = object.value(QStringLiteral("token")).toString();
    const auto importedDevice = object.value(QStringLiteral("deviceId")).toString();
    if (!boundedText(importedToken, 1024) || !boundedText(importedDevice, 256)
        || !object.value(QStringLiteral("data")).isObject())
        return QStringLiteral("抓包缺少有效的 token、deviceId 或 data。");
    for (const auto &name : {QByteArray("user-agent"), QByteArray("devicetype"), QByteArray("version"), QByteArray("time")}) {
        if (headers.value(name).isEmpty())
            return QStringLiteral("抓包缺少必要的应用请求头。");
        sessionHeaders.insert(name, headers.value(name));
    }
    token = importedToken;
    deviceId = importedDevice;
    return {};
}

void NovelApi::fetchBook(const QString &id)
{
    prepare(RequestKind::Book, id);
}

void NovelApi::fetchChapter(const QString &id)
{
    prepare(RequestKind::Chapter, id);
}

void NovelApi::prepare(RequestKind kind, const QString &id)
{
    if (active)
        return;
    if (!hasSession() || !isId(id)) {
        emit failed(QStringLiteral("请先导入有效会话，并填写 32 位十六进制 ID。"));
        return;
    }
    requestedId = id.toLower();
    requestKind = kind;
    const QJsonObject object{
        {QStringLiteral("data"), QJsonObject{{QStringLiteral("id"), requestedId}}},
        {QStringLiteral("deviceId"), deviceId}, {QStringLiteral("token"), token}};
    const auto encrypted = AesEcb::encrypt(QJsonDocument(object).toJson(QJsonDocument::Compact), apiKey);
    if (!encrypted.ok()) {
        emit failed(encrypted.error);
        return;
    }
    pendingBody = encrypted.bytes;
    responseBody.clear();
    responseError.clear();
    active = true;
    const auto remaining = lastCompletion.isValid() ? qMax<qint64>(0, 3000 - lastCompletion.elapsed()) : 0;
    delay.start(static_cast<int>(remaining));
}

void NovelApi::send()
{
    const auto path = requestKind == RequestKind::Book ? QStringLiteral("/v1/xs/xq") : QStringLiteral("/v1/xs/zjxq");
    QNetworkRequest request(QUrl(QStringLiteral("http://yj.kkj234ls.com") + path));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(30000);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/octet-stream"));
    for (auto it = sessionHeaders.cbegin(); it != sessionHeaders.cend(); ++it)
        request.setRawHeader(it.key(), it.value());
    reply = manager->post(request, pendingBody);
    pendingBody.clear();
    reply->setReadBufferSize(64 * 1024);
    deadline.start(30000);
    connect(reply, &QNetworkReply::metaDataChanged, this, [this] {
        if (reply->header(QNetworkRequest::ContentLengthHeader).toLongLong() > NovDecoder::MaxFileSize)
            stopWithError(QStringLiteral("接口响应超过 32 MiB 限制，已停止。"));
    });
    connect(reply, &QIODevice::readyRead, this, &NovelApi::readData);
    connect(reply, &QNetworkReply::finished, this, &NovelApi::finish);
}

void NovelApi::readData()
{
    if (!responseError.isEmpty())
        return;
    while (reply->bytesAvailable() > 0) {
        const auto bytes = reply->read(64 * 1024);
        if (bytes.isEmpty())
            break;
        if (responseBody.size() + bytes.size() > NovDecoder::MaxFileSize) {
            stopWithError(QStringLiteral("接口响应超过 32 MiB 限制，已停止。"));
            return;
        }
        responseBody.append(bytes);
    }
}

void NovelApi::stopWithError(const QString &message)
{
    if (!responseError.isEmpty())
        return;
    responseError = message;
    if (reply && !reply->isFinished())
        reply->abort();
}

void NovelApi::cancel()
{
    delay.stop();
    deadline.stop();
    if (reply) {
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
        reply = nullptr;
        lastCompletion.restart();
    }
    active = false;
    pendingBody.clear();
    responseBody.clear();
}

QString NovelApi::parseBook(const QJsonObject &data, NovelBook &book) const
{
    book.id = data.value(QStringLiteral("id")).toString();
    book.name = data.value(QStringLiteral("name")).toString();
    if (book.id != requestedId || !boundedText(book.name, 256)
        || !data.value(QStringLiteral("chapter")).isArray())
        return QStringLiteral("书籍响应的 ID、名称或目录格式不正确。");
    const auto entries = data.value(QStringLiteral("chapter")).toArray();
    if (entries.isEmpty() || entries.size() > 10000)
        return QStringLiteral("目录为空或超过 10000 条记录限制。");
    const auto countValue = data.value(QStringLiteral("chapter_count"));
    bool validCount = false;
    const auto declaredCount = countValue.isString()
        ? countValue.toString().toInt(&validCount) : countValue.toInt(-1);
    validCount = countValue.isString() ? validCount : declaredCount > 0;
    if (!validCount || declaredCount != entries.size())
        return QStringLiteral("目录记录数与服务器声明不一致，可能未取全；已停止。");
    QSet<QString> ids;
    QVector<qint64> numbers;
    static const QRegularExpression numberExpression(QStringLiteral("^第\\s*(\\d+)"));
    for (const auto &entry : entries) {
        if (!entry.isObject())
            return QStringLiteral("目录包含无效记录。");
        const auto object = entry.toObject();
        NovelChapter chapter;
        chapter.id = object.value(QStringLiteral("id")).toString();
        chapter.name = object.value(QStringLiteral("name")).toString();
        if (!isId(chapter.id) || ids.contains(chapter.id) || !boundedText(chapter.name, 256))
            return QStringLiteral("目录包含无效或重复的章节 ID，或无效的章节名。");
        ids.insert(chapter.id);
        chapter.canRead = object.value(QStringLiteral("type")).toString() == QStringLiteral("free")
            && object.value(QStringLiteral("can_view")).toString() == QStringLiteral("y");
        chapter.status = chapter.canRead ? QStringLiteral("待处理") : QStringLiteral("无权阅读（将停止）");
        book.chapters.append(chapter);
        const auto match = numberExpression.match(chapter.name);
        bool validNumber = false;
        const auto number = match.captured(1).toLongLong(&validNumber);
        numbers.append(validNumber ? number : -1);
    }
    const bool numeric = std::all_of(numbers.cbegin(), numbers.cend(), [](qint64 n) { return n >= 0; });
    const bool descending = numeric && numbers.size() > 1 && numbers.first() > numbers.last()
        && std::is_sorted(numbers.crbegin(), numbers.crend());
    if (descending)
        std::reverse(book.chapters.begin(), book.chapters.end());
    return {};
}

void NovelApi::finish()
{
    deadline.stop();
    if (responseError.isEmpty())
        readData();
    const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto networkError = reply->error();
    reply->deleteLater();
    reply = nullptr;
    lastCompletion.restart();
    active = false;
    QString error = responseError;
    if (error.isEmpty() && status != 200)
        error = QStringLiteral("接口返回 HTTP %1，已停止，不自动重试。").arg(status);
    if (error.isEmpty() && networkError != QNetworkReply::NoError)
        error = QStringLiteral("接口网络错误 %1，已停止。").arg(static_cast<int>(networkError));
    if (!error.isEmpty()) {
        responseBody.clear();
        emit failed(error);
        return;
    }
    const auto decrypted = AesEcb::decrypt(responseBody, apiKey);
    responseBody.clear();
    if (!decrypted.ok()) {
        emit failed(QStringLiteral("接口响应解密失败，请检查 API 密钥与服务版本。"));
        return;
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(decrypted.bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        emit failed(QStringLiteral("接口响应不是有效的 JSON 对象。"));
        return;
    }
    const auto object = document.object();
    const auto code = object.value(QStringLiteral("code")).toInt(-1);
    if (code != 200) {
        const auto errorCode = object.value(QStringLiteral("errorCode")).toInt(-1);
        emit failed(errorCode == 2002
                        ? QStringLiteral("会话授权已过期，请在 App 中正常刷新并重新导入抓包。")
                        : QStringLiteral("接口业务错误（code %1 / errorCode %2），已停止。").arg(code).arg(errorCode));
        return;
    }
    if (!object.value(QStringLiteral("data")).isObject()) {
        emit failed(QStringLiteral("接口响应缺少 data 对象。"));
        return;
    }
    const auto data = object.value(QStringLiteral("data")).toObject();
    if (requestKind == RequestKind::Book) {
        NovelBook book;
        error = parseBook(data, book);
        if (!error.isEmpty())
            emit failed(error);
        else
            emit bookReady(book);
        return;
    }
    if (data.value(QStringLiteral("type")).toString() != QStringLiteral("free")
        || data.value(QStringLiteral("can_view")).toString() != QStringLiteral("y")
        || !data.value(QStringLiteral("error_tips")).toString().isEmpty()) {
        emit failed(QStringLiteral("当前章节不是免费可阅读状态，已停止，不绕过阅读权限。"));
        return;
    }
    const auto chapter = data.value(QStringLiteral("chapter")).toObject();
    const auto content = chapter.value(QStringLiteral("content")).toString();
    const QUrl url(content, QUrl::StrictMode);
    if (chapter.value(QStringLiteral("id")).toString() != requestedId
        || !boundedText(content, 8192) || !isContentUrl(url)) {
        emit failed(QStringLiteral("返回的章节 ID 或正文链接不符合预期，已停止。"));
        return;
    }
    emit chapterReady(requestedId, url);
}
