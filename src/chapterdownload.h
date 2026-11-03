#pragma once

#include <QObject>
#include <QByteArray>
#include <QUrl>

#include <memory>

class QNetworkAccessManager;
class QNetworkReply;
class QTemporaryFile;

class ChapterDownload : public QObject {
    Q_OBJECT

public:
    explicit ChapterDownload(QObject *parent = nullptr);
    ~ChapterDownload() override;

    bool isBusy() const { return reply != nullptr; }
    void start(const QUrl &url, const QString &outputPath, const QByteArray &key,
               bool allowOverwrite = false, bool restrictToNovelCdn = false);
    void cancel();
    static bool isDownloadUrl(const QUrl &url);

signals:
    void progress(qint64 received, qint64 total);
    void finished(bool success, const QString &message, qint64 bytes, qsizetype characters);

private:
    void readData();
    void fail(const QString &message);
    void complete();

    QNetworkAccessManager *manager;
    QNetworkReply *reply = nullptr;
    std::unique_ptr<QTemporaryFile> temporary;
    QByteArray decodeKey;
    QString output;
    QString error;
    QString expectedDirectory;
    bool overwrite = false;
};
