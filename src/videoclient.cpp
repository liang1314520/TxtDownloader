#include "videoclient.h"

#include <QDir>
#include <QFileInfo>
#include <QHostInfo>
#include <QJsonDocument>
#include <QLockFile>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QTimer>
#include <QUuid>

#include <algorithm>

namespace {
void resolveHost(const QString &host, QObject *context, VideoClient::AddressCallback callback)
{
    QHostInfo::lookupHost(host, context, [callback](const QHostInfo &info) {
        callback(info.error() == QHostInfo::NoError ? info.addresses() : QList<QHostAddress>());
    });
}
}

struct VideoClient::Pending {
    quint64 id = 0;
    Operation operation = Operation::Categories;
    VideoApi::Query query;
    int page = 1;
    QString detailId;
    VideoSession session;
    QPointer<QNetworkReply> reply;
    QPointer<QTimer> timer;
    std::unique_ptr<QLockFile> registrationLock;
    QByteArray body;
    bool registering = false;
};

VideoClient::VideoClient(const VideoStoragePaths &storage, QObject *parent)
    : VideoClient(storage, nullptr, resolveHost, parent)
{
}
VideoClient::VideoClient(const VideoStoragePaths &storage, QNetworkAccessManager *manager,
                         Resolver addressResolver, QObject *parent)
    : QObject(parent), paths(storage), network(manager), resolver(std::move(addressResolver))
{
    if (!network) {
        network = new QNetworkAccessManager(this);
        network->setProxy(QNetworkProxy::NoProxy);
    }
    if (!resolver)
        resolver = resolveHost;
}
VideoClient::~VideoClient()
{
    cancelAll();
    unsavedSession.token.fill('\0');
}
QUrl VideoClient::defaultOrigin() { return QUrl(QStringLiteral("https://0826.aiyejikou0001.com")); }
bool VideoClient::isAllowedOrigin(const QUrl &origin)
{
    return origin.isValid() && origin.scheme() == QStringLiteral("https")
        && origin.userInfo().isEmpty() && !origin.hasQuery() && !origin.hasFragment()
        && (origin.path().isEmpty() || origin.path() == QStringLiteral("/")) && origin.port(443) == 443
        && (origin.host() == QStringLiteral("0826.aiyejikou0001.com")
            || origin.host() == QStringLiteral("0826.aiyejikou0002.com"));
}
bool VideoClient::isPublicAddress(const QHostAddress &address)
{
    if (address.protocol() == QAbstractSocket::IPv4Protocol) {
        static const QStringList excluded{"0.0.0.0/8", "10.0.0.0/8", "100.64.0.0/10", "127.0.0.0/8",
            "169.254.0.0/16", "172.16.0.0/12", "192.0.0.0/24", "192.0.2.0/24", "192.88.99.0/24",
            "192.168.0.0/16", "198.18.0.0/15", "198.51.100.0/24", "203.0.113.0/24", "224.0.0.0/3"};
        return std::none_of(excluded.cbegin(), excluded.cend(), [&address](const QString &subnet) {
            return address.isInSubnet(QHostAddress::parseSubnet(subnet));
        });
    }
    if (address.protocol() != QAbstractSocket::IPv6Protocol || !address.scopeId().isEmpty()
        || !address.isInSubnet(QHostAddress(QStringLiteral("2000::")), 3))
        return false;
    static const QStringList excluded{"2001::/23", "2001:db8::/32", "2002::/16", "3fff::/20"};
    return std::none_of(excluded.cbegin(), excluded.cend(), [&address](const QString &subnet) {
        return address.isInSubnet(QHostAddress::parseSubnet(subnet));
    });
}
void VideoClient::setConsentGranted(bool granted)
{
    consentGranted = granted;
    if (!granted) {
        cancelAll();
        unsavedSession.token.fill('\0');
        unsavedSession = {};
    }
}
quint64 VideoClient::request(Operation operation, const VideoApi::Query &query, int page, const QString &detailId)
{
    cancelAll();
    pending = std::make_unique<Pending>();
    pending->id = ++nextId;
    pending->operation = operation;
    pending->query = query;
    pending->page = page;
    pending->detailId = detailId;
    const auto id = pending->id;
    QTimer::singleShot(0, this, [this, id] { begin(id); });
    return id;
}
void VideoClient::cancel(quint64 id)
{
    if (!pending || pending->id != id)
        return;
    auto cancelled = std::move(pending);
    if (cancelled->timer) {
        cancelled->timer->stop();
        cancelled->timer->deleteLater();
    }
    if (cancelled->reply) {
        disconnect(cancelled->reply, nullptr, this, nullptr);
        cancelled->reply->abort();
        cancelled->reply->deleteLater();
    }
    cancelled->session.token.fill('\0');
}
void VideoClient::cancelAll()
{
    if (pending)
        cancel(pending->id);
}
void VideoClient::complete(const QByteArray &body, const QString &error)
{
    if (!pending)
        return;
    const auto id = pending->id;
    cancel(id);
    emit finished(id, body, error);
}
void VideoClient::begin(quint64 id)
{
    if (!pending || pending->id != id)
        return;
    if (!consentGranted) {
        complete({}, QStringLiteral("尚未同意视频游客访问，本次未联网。"));
        return;
    }
    QString error;
    switch (pending->operation) {
    case Operation::Categories:
    case Operation::Session: break;
    case Operation::List:
    case Operation::Search:
        error = VideoApi::validateQuery(pending->query, pending->page);
        if (error.isEmpty() && ((pending->operation == Operation::List)
            != (pending->query.kind == VideoApi::QueryKind::Category)))
            error = QStringLiteral("视频操作与查询类型不一致。 ");
        break;
    case Operation::Details: error = VideoApi::validateId(pending->detailId); break;
    default: error = QStringLiteral("不支持的视频操作。 "); break;
    }
    if (error.isEmpty())
        error = VideoStore::loadSession(paths, pending->session);
    if (!error.isEmpty()) {
        complete({}, error);
        return;
    }
    if (pending->session.origin.isEmpty())
        pending->session.origin = defaultOrigin();
    if (!isAllowedOrigin(pending->session.origin)) {
        complete({}, QStringLiteral("视频会话来源不在已核实范围，未发送凭证。"));
        return;
    }
    if (!unsavedSession.token.isEmpty()) {
        if (pending->operation != Operation::Session || pending->session.uuid != unsavedSession.uuid) {
            complete({}, QStringLiteral("游客会话尚未保存，请在连接页主动重试保存；不会再次登记。"));
            return;
        }
        error = lockRegistration();
        if (error.isEmpty())
            error = VideoStore::saveSession(paths, unsavedSession);
        if (error.isEmpty()) {
            unsavedSession.token.fill('\0');
            unsavedSession = {};
            emit sessionChanged();
        }
        complete({}, error);
        return;
    }
    if (pending->operation == Operation::Session && !pending->session.token.isEmpty()) {
        complete({}, {});
        return;
    }
    pending->timer = new QTimer(this);
    pending->timer->setSingleShot(true);
    connect(pending->timer, &QTimer::timeout, this, [this, id] {
        if (pending && pending->id == id)
            complete({}, QStringLiteral("视频请求超过 30 秒，已取消；未自动重试。"));
    });
    pending->timer->start(30000);
    resolve(id);
}
QString VideoClient::lockRegistration()
{
    const auto path = VideoStore::sessionPath(paths) + QStringLiteral(".registration.lock");
    if (!VideoStore::safeLocation(path) || !QDir().mkpath(QFileInfo(path).absolutePath())
        || !VideoStore::safeLocation(path))
        return QStringLiteral("无法创建安全的视频登记锁，本次未联网。 ");
    pending->registrationLock = std::make_unique<QLockFile>(path);
    if (!pending->registrationLock->tryLock(0))
        return QStringLiteral("其他窗口正在初始化视频游客，请稍后主动重试。 ");
    return {};
}
void VideoClient::resolve(quint64 id)
{
    const auto host = pending->session.origin.host();
    QPointer<VideoClient> self(this);
    resolver(host, this, [self, id, host](const QList<QHostAddress> &addresses) {
        if (!self || !self->pending || self->pending->id != id)
            return;
        auto &request = *self->pending;
        if (addresses.isEmpty() || addresses.size() > 64
            || !std::all_of(addresses.cbegin(), addresses.cend(), isPublicAddress)) {
            self->complete({}, QStringLiteral("视频服务解析失败或包含非公开地址，未连接。"));
            return;
        }
        if (request.session.token.isEmpty()) {
            auto error = self->lockRegistration();
            if (error.isEmpty())
                error = VideoStore::loadSession(self->paths, request.session);
            if (!error.isEmpty()) {
                self->complete({}, error);
                return;
            }
            if (request.session.origin.isEmpty())
                request.session.origin = defaultOrigin();
            if (!isAllowedOrigin(request.session.origin)) {
                self->complete({}, QStringLiteral("视频身份来源不在已核实范围，未继续。"));
                return;
            }
            if (request.session.origin.host() != host) {
                self->complete({}, QStringLiteral("视频会话来源发生变化，请主动重试。"));
                return;
            }
            if (request.session.token.isEmpty()) {
                if (request.session.registrationAttempted && request.operation != Operation::Session) {
                    self->complete({}, QStringLiteral("此身份已尝试登记。请在连接页主动重试，不会自动更换身份。"));
                    return;
                }
                if (request.session.uuid.isEmpty())
                    request.session.uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
                request.session.registrationAttempted = true;
                error = VideoStore::saveSession(self->paths, request.session);
                if (!error.isEmpty()) {
                    self->complete({}, error);
                    return;
                }
                emit self->sessionChanged();
                self->send(id, addresses.first(), true);
                return;
            }
            request.registrationLock.reset();
        }
        if (request.operation == Operation::Session)
            self->complete({}, {});
        else
            self->send(id, addresses.first(), false);
    });
}
void VideoClient::send(quint64 id, const QHostAddress &address, bool registration)
{
    if (!pending || pending->id != id)
        return;
    if (!network) {
        complete({}, QStringLiteral("视频网络组件已不可用。"));
        return;
    }
    QUrl url = pending->session.origin;
    if (registration) {
        url.setPath(QStringLiteral("/api/user/registeruuid"));
    } else {
        switch (pending->operation) {
        case Operation::Categories: url.setPath(QStringLiteral("/api/vod/type")); break;
        case Operation::List: url.setPath(QStringLiteral("/api/vod/list")); url.setQuery(pending->query.parameters(pending->page)); break;
        case Operation::Search: url.setPath(QStringLiteral("/api/vod/clever")); url.setQuery(pending->query.parameters(pending->page)); break;
        case Operation::Details: {
            url.setPath(QStringLiteral("/api/vod/info"));
            QUrlQuery query;
            query.addQueryItem(QStringLiteral("id"), pending->detailId);
            url.setQuery(query);
            break;
        }
        case Operation::Session: return;
        }
    }
    const auto host = url.host();
    url.setHost(address.toString());
    QNetworkRequest request(url);
    auto tls = QSslConfiguration::defaultConfiguration();
    tls.setPeerVerifyMode(QSslSocket::VerifyPeer);
    tls.setProtocol(QSsl::TlsV1_2OrLater);
    request.setSslConfiguration(tls);
    request.setPeerVerifyName(host);
    request.setRawHeader("Host", host.toLatin1());
    request.setRawHeader("Accept", "application/json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    request.setTransferTimeout(30000);
    pending->registering = registration;
    pending->body.clear();
    QNetworkReply *reply = nullptr;
    if (registration) {
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        reply = network->post(request, QJsonDocument(QJsonObject{{"uuid", pending->session.uuid}}).toJson(QJsonDocument::Compact));
    } else {
        if (pending->session.token.isEmpty() || !isAllowedOrigin(pending->session.origin)) {
            complete({}, QStringLiteral("没有可安全使用的同源视频会话，未发送请求。"));
            return;
        }
        request.setRawHeader("x-token", pending->session.token);
        reply = network->get(request);
    }
    pending->reply = reply;
    reply->setReadBufferSize(64 * 1024);
    connect(reply, &QNetworkReply::readyRead, this, [this, id, reply] { read(id, reply); });
    connect(reply, &QNetworkReply::finished, this, [this, id, reply] { received(id, reply); });
    connect(reply, &QNetworkReply::metaDataChanged, this, [this, id, reply] {
        if (!pending || pending->id != id || pending->reply != reply)
            return;
        const auto length = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
        const auto maximum = pending->registering ? 64 * 1024 : VideoApi::MaxJsonBytes;
        if (length > maximum)
            complete({}, QStringLiteral("视频响应超过大小限制，已停止。"));
    });
}
void VideoClient::read(quint64 id, QNetworkReply *reply)
{
    if (!pending || pending->id != id || pending->reply != reply)
        return;
    const auto maximum = pending->registering ? 64 * 1024 : VideoApi::MaxJsonBytes;
    pending->body.append(reply->read(maximum - pending->body.size() + 1));
    if (pending->body.size() > maximum)
        complete({}, QStringLiteral("视频响应超过大小限制，已停止。"));
}
void VideoClient::received(quint64 id, QNetworkReply *reply)
{
    if (!pending || pending->id != id || pending->reply != reply)
        return;
    read(id, reply);
    if (!pending || pending->id != id)
        return;
    if (reply->error() != QNetworkReply::NoError
        || reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200) {
        complete({}, QStringLiteral("视频 HTTPS 请求失败、证书异常或返回非 200 状态；未重试或跟随重定向。"));
        return;
    }
    if (!pending->registering) {
        const auto body = pending->body;
        complete(body, {});
        return;
    }
    QString token;
    auto error = VideoApi::parseGuestToken(pending->body, token);
    if (!error.isEmpty()) {
        complete({}, error);
        return;
    }
    pending->session.token = token.toLatin1();
    token.fill(QChar());
    error = VideoStore::saveSession(paths, pending->session);
    if (!error.isEmpty()) {
        unsavedSession = pending->session;
        complete({}, QStringLiteral("游客已返回会话，但安全保存失败。已停止后续请求，请在连接页重试保存；不会重新登记。"));
        return;
    }
    emit sessionChanged();
    pending->registrationLock.reset();
    disconnect(reply, nullptr, this, nullptr);
    reply->deleteLater();
    pending->reply = nullptr;
    pending->body.clear();
    if (pending->operation == Operation::Session)
        complete({}, {});
    else
        resolve(id);
}
