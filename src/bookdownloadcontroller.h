#pragma once

#include "novelapi.h"

#include <QObject>
#include <QTimer>

#include <memory>

class ChapterDownload;
class QTemporaryFile;

class BookDownloadController : public QObject {
    Q_OBJECT

public:
    explicit BookDownloadController(QObject *parent = nullptr);
    ~BookDownloadController() override;

    bool isBusy() const { return mode != Mode::Idle; }
    bool isSearching() const { return mode == Mode::Search; }
    const NovelBook &book() const { return currentBook; }
    const QVector<NovelSearchBook> &searchResults() const { return foundBooks; }
    int searchPage() const { return completedSearchPage; }
    bool canLoadMoreSearch() const { return moreSearchResults; }
    void invalidate();
    void invalidateBook();
    void invalidateSearch();
    void searchBooks(const QString &keywords, const QString &capturePath);
    void loadMoreSearch();
    void fetchBook(const QString &id, const QString &capturePath);
    void fetchLinks();
    void downloadBook(const QString &id, const QString &capturePath, const QString &directory);
    void cancel();

signals:
    void bookChanged(const NovelBook &book);
    void searchResultsChanged();
    void chapterChanged(int index, const QUrl &content, const QString &status);
    void busyChanged(bool busy);
    void statusChanged(const QString &message);
    void progressChanged(int completed, int total);
    void downloadProgress(qint64 received, qint64 total);

private:
    enum class Mode { Idle, Catalog, Links, Download, Merge, Search };
    void setMode(Mode value);
    void requestBook(const QString &id, const QString &capturePath, const QString &directory);
    void startDownload(const QString &directory);
    void startMerge();
    void mergeNextChapter();
    void completeMerge();
    void nextChapter();
    void finishTask(const QString &message);
    void stop(const QString &message);
    void updateChapter(const QString &status);
    QString prepareDirectory(const QString &directory);
    QString checkDirectory() const;
    QString chapterPath(int index) const;

    NovelApi *api;
    ChapterDownload *downloader;
    QTimer next;
    NovelBook currentBook;
    QVector<NovelSearchBook> foundBooks;
    QString searchKeywords;
    int completedSearchPage = 0;
    bool moreSearchResults = false;
    Mode mode = Mode::Idle;
    int current = 0;
    int saved = 0;
    int skipped = 0;
    bool cancelling = false;
    QByteArray decodeKey;
    QString bookDirectory;
    QString pendingDirectory;
    std::unique_ptr<QTemporaryFile> mergedFile;
};
