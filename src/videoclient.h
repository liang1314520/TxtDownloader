#pragma once

#include "videostore.h"

#include <QHostAddress>
#include <QObject>
#include <QPointer>

#include <functional>
#include <memory>

class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;

class VideoClient : public QObject {
    Q_OBJECT
public:
    enum class Operation { Categories, List, Search, Details, Session };
    using AddressCallback = std::function<void(const QList<QHostAddress> &)>;
    using Resolver = std::function<void(const QString &, QObject *, AddressCallback)>;
    explicit VideoClient(const VideoStoragePaths &paths, QObject *parent = nullptr);
    VideoClient(const VideoStoragePaths &paths, QNetworkAccessManager *network,
                Resolver resolver, QObject *parent = nullptr);
    ~VideoClient() override;
    static QUrl defaultOrigin();
    static bool isAllowedOrigin(const QUrl &origin);
    static bool isPublicAddress(const QHostAddress &address);
    void setConsentGranted(bool granted);
    quint64 request(Operation operation, const VideoApi::Query &query, int page,
                    const QString &detailId = {});
    void cancel(quint64 requestId);
    void cancelAll();
signals:
    void finished(quint64 requestId, const QByteArray &body, const QString &error);
    void sessionChanged();
private:
    struct Pending;
    void begin(quint64 id);
    void resolve(quint64 id);
    void send(quint64 id, const QHostAddress &address, bool registration);
    void read(quint64 id, QNetworkReply *reply);
    void received(quint64 id, QNetworkReply *reply);
    void complete(const QByteArray &body, const QString &error);
    QString lockRegistration();
    VideoStoragePaths paths;
    QPointer<QNetworkAccessManager> network;
    Resolver resolver;
    std::unique_ptr<Pending> pending;
    VideoSession unsavedSession;
    quint64 nextId = 0;
    bool consentGranted = false;
};
