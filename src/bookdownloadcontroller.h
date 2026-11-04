#pragma once

#include "booklistcache.h"

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
    bool isPreviewing() const { return mode == Mode::PreviewCatalog || mode == Mode::Preview; }
    const NovelBook &book() const { return currentBook; }
    const QVector<NovelSearchBook> &searchResults() const { return activeList().books; }
    int searchPage() const { return activeList().page; }
    bool canLoadMoreSearch() const { return activeList().hasMore; }
    bool searchComplete() const { return activeList().complete; }
    bool isGroupList() const { return groupActive; }
    bool hasGroupCache() const { return groupList.page > 0; }
    QDateTime listUpdatedAt() const { return activeList().updatedAt; }
    void invalidate(const QString &capturePath);
    void invalidateBook();
    void invalidateSearch();
    void browseGroup(const QString &capturePath, bool refresh = false);
    void searchBooks(const QString &keywords, const QString &capturePath);
    void loadMoreSearch();
    void fetchBook(const QString &id, const QString &capturePath);
    void previewBook(const QString &id, const QString &capturePath);
    void fetchLinks();
    void downloadBook(const QString &id, const QString &capturePath, const QString &directory);
    void cancel();

signals:
    void bookChanged(const NovelBook &book);
    void searchResultsChanged();
    void previewCatalogReady(const NovelBook &book);
    void previewChapterReady(int index, const QString &text, const QString &status);
    void previewFinished(const QString &message);
    void chapterChanged(int index, const QUrl &content, const QString &status);
    void busyChanged(bool busy);
    void statusChanged(const QString &message);
    void progressChanged(int completed, int total);
    void downloadProgress(qint64 received, qint64 total);

private:
    enum class Mode { Idle, Catalog, Links, Download, Merge, Search, PreviewCatalog, Preview };
    BookListSnapshot &activeList() { return groupActive ? groupList : keywordList; }
    const BookListSnapshot &activeList() const { return groupActive ? groupList : keywordList; }
    void setMode(Mode value);
    void requestBook(const QString &id, const QString &capturePath, const QString &directory);
    void startDownload(const QString &directory);
    void startMerge();
    void mergeNextChapter();
    void completeMerge();
    void nextChapter();
    void nextPreviewChapter();
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
    NovelBook previewBookData;
    int previewIndex = 0;
    BookListSnapshot groupList;
    BookListSnapshot keywordList;
    bool groupActive = true;
    QString searchKeywords;
    QString listCapturePath;
    QString cacheWarning;
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
