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
#include <QUuid>
#include <QScopeGuard>

#include <algorithm>

namespace {

bool boundedText(const QString &text, qsizetype maximum)
{
    return !text.isEmpty() && text.size() <= maximum
        && std::none_of(text.cbegin(), text.cend(), [](QChar ch) { return ch.isNull() || ch.category() == QChar::Other_Control; });
}

bool descriptionText(const QJsonValue &value)
{
    if (value.isUndefined() || value.isNull())
        return true;
    const auto text = value.toString();
    return value.isString() && text.size() <= 65536
        && std::none_of(text.cbegin(), text.cend(), [](QChar ch) {
            return ch.isNull() || (ch.category() == QChar::Other_Control
                && ch != QLatin1Char('\n') && ch != QLatin1Char('\r') && ch != QLatin1Char('\t'));
        });
}

qint64 chapterNumber(const QString &title)
{
    static const QRegularExpression expression(
        QStringLiteral("^第\\s*([0-9]+|[零〇一二两三四五六七八九十百千万]+)"));
    const auto match = expression.match(title.trimmed());
    if (!match.hasMatch())
        return -1;
    const auto text = match.captured(1);
    bool valid = false;
    const auto arabic = text.toLongLong(&valid);
    if (valid)
        return arabic;
    if (text.front().isDigit())
        return -1;
    if (std::none_of(text.cbegin(), text.cend(), [](QChar ch) {
            return QStringLiteral("十百千万").contains(ch);
        })) {
        QString digits;
        for (const auto ch : text) {
            const auto digit = QStringLiteral("零一二三四五六七八九").indexOf(ch);
            digits.append(QChar(static_cast<char16_t>(u'0' + (digit >= 0 ? digit : ch == QChar(u'两') ? 2 : 0))));
        }
        const auto positional = digits.toLongLong(&valid);
        return valid ? positional : -1;
    }
    qint64 total = 0;
    qint64 section = 0;
    qint64 number = 0;
    for (const auto ch : text) {
        const auto digit = QStringLiteral("零一二三四五六七八九").indexOf(ch);
        if (digit >= 0 || ch == QChar(u'〇') || ch == QChar(u'两')) {
            number = digit >= 0 ? digit : ch == QChar(u'两') ? 2 : 0;
            continue;
        }
        if (ch == QChar(u'万')) {
            total += (section + number) * 10000;
            section = 0;
        } else {
            const auto unit = ch == QChar(u'十') ? 10 : ch == QChar(u'百') ? 100 : 1000;
            section += (number == 0 ? 1 : number) * unit;
        }
        number = 0;
    }
    return total + section + number;
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

void NovelBook::normalizeChapterOrder()
{
    QVector<qint64> numbers;
    for (const auto &chapter : chapters) {
        const auto number = chapterNumber(chapter.name);
        if (number >= 0)
            numbers.append(number);
    }
    // 非编号条目不参与方向判断，但与整份倒序目录一起翻转，避免序章、尾声被挪到末尾或开头。
    if (numbers.size() > 1 && numbers.first() > numbers.last()
        && std::is_sorted(numbers.crbegin(), numbers.crend()))
        std::reverse(chapters.begin(), chapters.end());
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

bool NovelApi::isSearchKeywords(const QString &keywords)
{
    return boundedText(keywords, 256) && !keywords.trimmed().isEmpty();
}

void NovelApi::clearSession()
{
    cancel();
    apiKey.fill('\0');
    apiKey.clear();
    contentKey.fill('\0');
    contentKey.clear();
    session.clearCredentials();
    session.deviceId.clear();
    session.headers.clear();
}

QString NovelApi::restoreSession(const SessionProfile &profile)
{
    if (active)
        return QStringLiteral("请先结束当前网络任务。");
    const auto invalid = SessionStore::validate(profile);
    if (!invalid.isEmpty())
        return invalid;
    const SessionProfile restored = profile;
    clearSession();
    session = restored;
    emit sessionChanged();
    return {};
}

QString NovelApi::loadSessionKeys()
{
    if (apiKey.size() == 16 && contentKey.size() == 16)
        return {};
    return LocalKeyProfile::load(QStringLiteral("4.0.9"), apiKey, contentKey);
}

QString NovelApi::prepareSessionRequest(bool allowNewIdentity)
{
    if (session.deviceId.isEmpty()) {
        if (!allowNewIdentity)
            return QStringLiteral("尚无设备身份，请先导入一次自己的抓包，或明确选择建立本机独立身份。");
        session.deviceId = QUuid::createUuid().toString(QUuid::Id128) + QLatin1Char('P');
        session.headers = {{"version", "4.0.9"}, {"devicetype", "android"},
                           {"user-agent", "Mozilla/5.0 (Linux; U; Android 8.1.0; zh-CN; EML-AL00 Build/HUAWEIEML-AL00) AppleWebKit/537.36 (KHTML, like Gecko) Version/4.0 Chrome/57.0.2987.108 Lucky/11.9.4.974 UWS/2.13.1.48 Mobile Safari/537.36 AliApp(DingTalk/4.5.11)  Channel/227200 language/zh-CN"}};
    }
    const auto invalid = SessionStore::validate(session);
    if (!invalid.isEmpty())
        return invalid;
    // 即使密钥尚未就绪也先保留用户已确认的固定身份，避免下次操作生成另一个游客身份。
    emit sessionChanged();
    return loadSessionKeys();
}

void NovelApi::acquireDeviceSession(bool allowNewIdentity)
{
    if (active)
        return;
    const auto error = prepareSessionRequest(allowNewIdentity);
    if (!error.isEmpty()) {
        emit failed(error);
        return;
    }
    enqueue(RequestKind::DeviceSession, QJsonObject{{QStringLiteral("device_id"), session.rawDeviceId()}});
}

void NovelApi::loginAccount(const QString &account, const QString &password, bool allowNewIdentity)
{
    if (active)
        return;
    if (!boundedText(account, 256) || account.trimmed().isEmpty() || !boundedText(password, 256)) {
        emit failed(QStringLiteral("请输入有效的账号和密码（最多 256 字符，不含控制字符）。"));
        return;
    }
    const auto error = prepareSessionRequest(allowNewIdentity);
    if (!error.isEmpty()) {
        emit failed(error);
        return;
    }
    enqueue(RequestKind::AccountLogin, QJsonObject{
        {QStringLiteral("account_name"), account.trimmed()},
        {QStringLiteral("account_password"), password}, {QStringLiteral("type"), QStringLiteral("login")}});
}

QString NovelApi::parseSessionToken(const QJsonObject &data, SessionProfile &profile, bool accountSession)
{
    const auto rawToken = data.value(QStringLiteral("token"));
    const auto userId = data.value(QStringLiteral("user_id"));
    if (!rawToken.isString() || !userId.isString() || !boundedText(rawToken.toString(), 768)
        || !boundedText(userId.toString(), 128))
        return QStringLiteral("登录响应缺少有效的 token 或 user_id；原会话未替换。");
    SessionProfile loaded = profile;
    loaded.token = rawToken.toString() + QLatin1Char('_') + userId.toString();
    loaded.accountSession = accountSession;
    loaded.expiresAt = {};
    // APK 未使用 expired_at 推断有效期；沿用服务器的 2002 失效信号，不猜测其时间格式。
    const auto invalid = SessionStore::validate(loaded);
    if (!invalid.isEmpty())
        return invalid;
    profile.clearCredentials();
    profile = std::move(loaded);
    return {};
}

QString NovelApi::importSession(const QString &capturePath)
{
    if (active)
        return QStringLiteral("请先结束当前网络任务。");
    QFile file(capturePath);
    if (!file.open(QIODevice::ReadOnly))
        return QStringLiteral("无法读取请求抓包文件。");
    if (file.size() <= 0 || file.size() > 1024 * 1024)
        return QStringLiteral("请求抓包为空或超过 1 MiB 限制。");
    const auto raw = file.read(1024 * 1024 + 1);
    if (file.error() != QFileDevice::NoError || raw.size() > 1024 * 1024)
        return QStringLiteral("请求抓包读取失败或超过限制。");
    return importRequest(raw);
}

namespace {
QString decodeCapturedRequest(const QByteArray &raw, SessionProfile &session,
                              QByteArray &apiKey, QByteArray &contentKey, QJsonObject &data)
{
    if (raw.isEmpty() || raw.size() > 1024 * 1024)
        return QStringLiteral("请求抓包为空或超过 1 MiB 限制。");
    const auto separator = raw.indexOf("\r\n\r\n");
    if (separator <= 0 || separator > 16 * 1024)
        return QStringLiteral("请选择包含 HTTP 请求头和原始 Body 的 .hcy 请求抓包。");
    const auto lines = raw.first(separator).split('\n');
    const auto requestLine = lines.first().trimmed();
    if (requestLine != "POST /v1/xs/xq HTTP/1.1" && requestLine != "POST /v1/xs/zjxq HTTP/1.1"
        && requestLine != "POST /v1/xs/ss HTTP/1.1")
        return QStringLiteral("仅支持本应用小说搜索、书籍详情或章节详情的 POST 请求抓包。");
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
    auto decoded = AesEcb::decrypt(body, apiKey);
    const auto clearDecoded = qScopeGuard([&decoded] { decoded.bytes.fill('\0'); });
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
        session.headers.insert(name, headers.value(name));
    }
    session.token = importedToken;
    session.deviceId = importedDevice;
    // 抓包未提供可靠的账号类型，刷新前按可能的账号会话提醒用户。
    session.accountSession = true;
    const auto invalid = SessionStore::validate(session);
    if (!invalid.isEmpty())
        return invalid;
    data = object.value(QStringLiteral("data")).toObject();
    return {};
}
}

QString NovelApi::importRequest(const QByteArray &raw)
{
    if (active)
        return QStringLiteral("请先结束当前网络任务。");
    SessionProfile imported;
    QByteArray importedApiKey;
    QByteArray importedContentKey;
    const auto clearImported = qScopeGuard([&] {
        imported.clearCredentials();
        importedApiKey.fill('\0');
        importedContentKey.fill('\0');
    });
    QJsonObject data;
    const auto error = decodeCapturedRequest(raw, imported, importedApiKey, importedContentKey, data);
    if (!error.isEmpty())
        return error;
    clearSession();
    session = imported;
    apiKey = importedApiKey;
    contentKey = importedContentKey;
    emit sessionChanged();
    return {};
}

QString NovelApi::categoryFromRequest(const QByteArray &raw, NovelCategory &category)
{
    if (!raw.startsWith("POST /v1/xs/ss HTTP/1.1\r\n"))
        return QStringLiteral("请在 App 中打开小说分类列表，而不是书籍或章节详情。");
    SessionProfile profile;
    QByteArray requestKey;
    QByteArray chapterKey;
    const auto clearSecrets = qScopeGuard([&] {
        profile.clearCredentials();
        requestKey.fill('\0');
        chapterKey.fill('\0');
    });
    QJsonObject data;
    const auto error = decodeCapturedRequest(raw, profile, requestKey, chapterKey, data);
    return error.isEmpty() ? NovelCategory::fromRequestData(data, category) : error;
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
    enqueue(kind, QJsonObject{{QStringLiteral("id"), requestedId}});
}

void NovelApi::searchBooks(const QString &keywords, int page, const NovelCategory &category)
{
    if (active)
        return;
    if (!hasSession() || (!keywords.isEmpty() && !isSearchKeywords(keywords)) || page < 1 || page > 10000) {
        emit failed(QStringLiteral("请先导入有效会话，填写 1–256 字符的搜索关键词，页码须在 1–10000 之间。"));
        return;
    }
    requestedPage = page;
    QJsonObject data{{QStringLiteral("page"), QString::number(page)}};
    if (keywords.isEmpty()) {
        if (!category.isValid()) {
            emit failed(QStringLiteral("当前分类条件无效，未发送请求。"));
            return;
        }
        data = category.parameters();
        data.insert(QStringLiteral("page"), QString::number(page));
    } else {
        data.insert(QStringLiteral("keywords"), keywords.trimmed());
        data.insert(QStringLiteral("position"), QStringLiteral("novel"));
    }
    enqueue(RequestKind::Search, data);
}

void NovelApi::enqueue(RequestKind kind, const QJsonObject &data)
{
    requestKind = kind;
    const auto keyError = loadSessionKeys();
    if (!keyError.isEmpty()) {
        emit failed(keyError);
        return;
    }
    const bool auth = kind == RequestKind::DeviceSession || kind == RequestKind::AccountLogin;
    const QJsonObject object{
        {QStringLiteral("data"), data},
        {QStringLiteral("deviceId"), auth ? session.requestDeviceId() : session.deviceId},
        {QStringLiteral("token"), session.token.isEmpty() ? QStringLiteral("null_null") : session.token}};
    auto plain = QJsonDocument(object).toJson(QJsonDocument::Compact);
    const auto encrypted = AesEcb::encrypt(plain, apiKey);
    plain.fill('\0');
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
    QString path;
    switch (requestKind) {
    case RequestKind::Book:
        path = QStringLiteral("/v1/xs/xq");
        break;
    case RequestKind::Chapter:
        path = QStringLiteral("/v1/xs/zjxq");
        break;
    case RequestKind::Search:
        path = QStringLiteral("/v1/xs/ss");
        break;
    case RequestKind::DeviceSession:
        path = QStringLiteral("/v1/yh/dl");
        break;
    case RequestKind::AccountLogin:
        path = QStringLiteral("/v1/yh/mmzh");
        break;
    }
    QNetworkRequest request(QUrl(QStringLiteral("http://yj.kkj234ls.com") + path));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(30000);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/octet-stream"));
    for (auto it = session.headers.cbegin(); it != session.headers.cend(); ++it)
        request.setRawHeader(it.key(), it.value());
    request.setRawHeader("time", QDateTime::currentDateTimeUtc().addSecs(8 * 3600)
                                     .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")).toLatin1());
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
    const auto description = data.value(QStringLiteral("description"));
    if (!descriptionText(description))
        return QStringLiteral("书籍简介格式不正确或超过长度限制。");
    book.description = description.toString();
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
    }
    book.normalizeChapterOrder();
    return {};
}

QString NovelApi::parseSearch(const QJsonArray &data, QVector<NovelSearchBook> &books) const
{
    if (data.size() > 10000)
        return QStringLiteral("单页搜索结果超过 10000 条限制，已停止。");
    QSet<QString> ids;
    for (const auto &entry : data) {
        if (!entry.isObject())
            return QStringLiteral("搜索结果包含无效记录。");
        const auto object = entry.toObject();
        if (!object.value(QStringLiteral("type")).isString())
            return QStringLiteral("搜索结果缺少有效的内容类型。");
        if (object.value(QStringLiteral("type")).toString() != QStringLiteral("novel"))
            continue;
        NovelSearchBook book;
        book.id = object.value(QStringLiteral("id")).toString().toLower();
        book.name = object.value(QStringLiteral("name")).toString();
        if (!isId(book.id) || !boundedText(book.name, 256))
            return QStringLiteral("搜索结果包含无效书籍 ID 或书名。");
        const auto author = object.value(QStringLiteral("author"));
        const auto count = object.value(QStringLiteral("chapter_count"));
        const auto status = object.value(QStringLiteral("status_text"));
        for (const auto &field : {author, count, status}) {
            if (!field.isUndefined() && !field.isNull()
                && (!field.isString() || (!field.toString().isEmpty() && !boundedText(field.toString(), 256))))
                return QStringLiteral("搜索结果的作者、章节数或状态字段不符合预期。");
        }
        book.author = author.toString();
        book.chapterCount = count.toString();
        book.status = status.toString();
        const auto description = object.value(QStringLiteral("description"));
        if (!descriptionText(description))
            return QStringLiteral("搜索结果的简介格式不正确或超过长度限制。");
        book.description = description.toString();
        if (!ids.contains(book.id)) {
            ids.insert(book.id);
            books.append(book);
        }
    }
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
    auto bytes = responseBody;
    responseBody.clear();
    QJsonParseError parseError;
    auto document = bytes.startsWith('{') ? QJsonDocument::fromJson(bytes, &parseError) : QJsonDocument();
    const bool plaintextError = document.isObject();
    if (!plaintextError) {
        auto decrypted = AesEcb::decrypt(bytes, apiKey);
        bytes.fill('\0');
        if (!decrypted.ok()) {
            emit failed(QStringLiteral("接口响应解密失败，请检查 API 密钥与服务版本。"));
            return;
        }
        document = QJsonDocument::fromJson(decrypted.bytes, &parseError);
        decrypted.bytes.fill('\0');
    }
    bytes.fill('\0');
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        emit failed(QStringLiteral("接口响应不是有效的 JSON 对象。"));
        return;
    }
    const auto object = document.object();
    const auto code = object.value(QStringLiteral("code")).toInt(-1);
    if (plaintextError && code == 200) {
        emit failed(QStringLiteral("接口返回了未加密的成功响应，已拒绝使用；原会话未替换。"));
        return;
    }
    if (code != 200) {
        const auto errorCode = object.value(QStringLiteral("errorCode")).toInt(-1);
        if (errorCode == 2002 && !session.token.isEmpty()) {
            session.expiresAt = QDateTime::currentDateTimeUtc().addSecs(-1);
            emit sessionChanged();
        }
        emit failed(errorCode == 2002
                        ? QStringLiteral("会话授权已过期，请主动获取／刷新设备会话或重新账号登录；不会自动切换身份。")
                        : QStringLiteral("接口业务错误（code %1 / errorCode %2），已停止。若要求验证码或设备校验，请在 App 中正常完成后导入；不会跳过验证。").arg(code).arg(errorCode));
        return;
    }
    if (requestKind == RequestKind::DeviceSession || requestKind == RequestKind::AccountLogin) {
        if (!object.value(QStringLiteral("data")).isObject()) {
            emit failed(QStringLiteral("会话响应缺少 data 对象，原会话未替换。"));
            return;
        }
        error = parseSessionToken(object.value(QStringLiteral("data")).toObject(), session,
                                  requestKind == RequestKind::AccountLogin);
        if (!error.isEmpty()) {
            emit failed(error);
            return;
        }
        emit sessionChanged();
        emit sessionReady();
        return;
    }
    if (requestKind == RequestKind::Search) {
        if (!object.value(QStringLiteral("data")).isArray()) {
            emit failed(QStringLiteral("搜索响应缺少 data 数组。"));
            return;
        }
        const auto entries = object.value(QStringLiteral("data")).toArray();
        QVector<NovelSearchBook> books;
        error = parseSearch(entries, books);
        if (!error.isEmpty())
            emit failed(error);
        else
            emit searchReady(books, requestedPage, !entries.isEmpty());
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
