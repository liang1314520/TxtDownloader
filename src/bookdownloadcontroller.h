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
    bool isAuthenticating() const { return mode == Mode::Authentication; }
    bool hasSession() const { return api->hasSession(); }
    bool hasDeviceIdentity() const { return !api->sessionProfile().deviceId.isEmpty(); }
    bool isAccountSession() const { return api->sessionProfile().accountSession; }
    QString sessionDescription() const;
    void importCapture(const QString &capturePath);
    QString importCapturedRequest(const QByteArray &raw);
    void acquireDeviceSession(bool allowNewIdentity = false);
    void loginAccount(const QString &account, const QString &password, bool allowNewIdentity = false);
    void forgetSession();
    const NovelBook &book() const { return currentBook; }
    const QVector<NovelSearchBook> &searchResults() const { return activeList().books; }
    int searchPage() const { return activeList().page; }
    bool canLoadMoreSearch() const { return activeList().hasMore; }
    bool searchComplete() const { return activeList().complete; }
    bool isGroupList() const { return groupActive; }
    bool hasGroupCache() const { return groupList().page > 0; }
    const QVector<NovelCategory> &categories() const { return categoryList; }
    const NovelCategory &currentCategory() const { return category; }
    QString selectCategory(const QString &key);
    QString addCategory(const NovelCategory &value);
    QString renameCategory(const QString &key, const QString &name);
    QString removeCategory(const QString &key);
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
    void categoriesChanged();
    void previewCatalogReady(const NovelBook &book);
    void previewChapterReady(int index, const QString &text, const QString &status);
    void previewFinished(const QString &message);
    void sessionChanged();
    void chapterChanged(int index, const QUrl &content, const QString &status);
    void busyChanged(bool busy);
    void statusChanged(const QString &message);
    void progressChanged(int completed, int total);
    void downloadProgress(qint64 received, qint64 total);

private:
    enum class Mode { Idle, Catalog, Links, Download, Merge, Search, PreviewCatalog, Preview, Authentication };
    BookListSnapshot &groupList() { return groupLists[category.key()]; }
    const BookListSnapshot &groupList() const { return groupLists.constFind(category.key()).value(); }
    BookListSnapshot &activeList() { return groupActive ? groupList() : keywordList; }
    const BookListSnapshot &activeList() const { return groupActive ? groupList() : keywordList; }
    void setMode(Mode value);
    QString saveCategoryList(const QVector<NovelCategory> &updated) const;
    QString ensureSession(const QString &capturePath);
    void clearBookSessionState();
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
    QVector<NovelCategory> categoryList{NovelCategory::defaultCategory()};
    NovelCategory category = NovelCategory::defaultCategory();
    QMap<QString, BookListSnapshot> groupLists;
    BookListSnapshot keywordList;
    bool groupActive = true;
    QString searchKeywords;
    QString listCapturePath;
    QString cacheWarning;
    QString sessionWarning;
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
