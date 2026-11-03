#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QMap>
#include <QTimer>
#include <QUrl>
#include <QVector>

class QNetworkAccessManager;
class QNetworkReply;

struct NovelChapter {
    QString id;
    QString name;
    bool canRead = false;
    QUrl content;
    QString status;
};

struct NovelBook {
    QString id;
    QString name;
    QVector<NovelChapter> chapters;
};

class NovelApi : public QObject {
    Q_OBJECT

public:
    explicit NovelApi(QObject *parent = nullptr);
    ~NovelApi() override;

    QString importSession(const QString &capturePath);
    const QByteArray &chapterKey() const { return contentKey; }
    void clearSession();
    bool hasSession() const { return !token.isEmpty(); }
    bool isBusy() const { return active; }
    void fetchBook(const QString &id);
    void fetchChapter(const QString &id);
    void cancel();
    static bool isId(const QString &id);

signals:
    void bookReady(const NovelBook &book);
    void chapterReady(const QString &id, const QUrl &content);
    void failed(const QString &message);

private:
    enum class RequestKind { Book, Chapter };
    void prepare(RequestKind kind, const QString &id);
    void send();
    void readData();
    void finish();
    void stopWithError(const QString &message);
    QString parseBook(const QJsonObject &data, NovelBook &book) const;

    QNetworkAccessManager *manager;
    QNetworkReply *reply = nullptr;
    QTimer delay;
    QTimer deadline;
    QElapsedTimer lastCompletion;
    bool active = false;
    RequestKind requestKind = RequestKind::Book;
    QString requestedId;
    QByteArray pendingBody;
    QByteArray responseBody;
    QByteArray apiKey;
    QByteArray contentKey;
    QString token;
    QString deviceId;
    QMap<QByteArray, QByteArray> sessionHeaders;
    QString responseError;
};
