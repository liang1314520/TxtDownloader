#pragma once

#include "videostore.h"

#include <QObject>

class VideoClient;
class VideoCatalogController : public QObject {
    Q_OBJECT
public:
    explicit VideoCatalogController(const VideoStoragePaths &paths = VideoStoragePaths::defaults(),
                                    QObject *parent = nullptr);
    ~VideoCatalogController() override;
    const QVector<VideoApi::Category> &categories() const { return sourceCategories; }
    const QVector<VideoApi::Summary> &favorites() const { return savedFavorites; }
    const VideoCategoryOrder &categoryOrder() const { return order; }
    const VideoSnapshot &categorySnapshot() const { return categoryState; }
    const VideoSnapshot &searchSnapshot() const { return searchState; }
    const VideoApi::Details &details() const { return currentDetails; }
    QString startupError() const { return initialError; }
    bool isBusy() const { return busy; }
    bool isFavorite(const QString &id) const;
    bool hasSession() const;
    QString selectCachedCategory(const QString &parentId, const QString &childId);
    QString setFavorite(const VideoApi::Summary &item, bool favorite);
    QString saveOrder(const VideoCategoryOrder &newOrder);
    QString clearSession();
    void showSavedDetails(const VideoApi::Summary &item);
    void retrySession();
    void refreshCategories();
    void loadCategory(const QString &parentId, const QString &childId);
    void search(const QString &keyword, VideoApi::SearchPart part);
    void loadMore(VideoApi::QueryKind kind);
    void refreshDetails(const QString &id);
    void grantConsent(bool granted);
    void cancel();
signals:
    void categoriesChanged();
    void categoryResultsChanged();
    void searchResultsChanged();
    void favoritesChanged();
    void detailsChanged();
    void busyChanged(bool busy);
    void consentRequired();
    void connectionChanged();
    void errorOccurred(const QString &message);
private:
    friend class VideoCatalogTest;
    enum class Operation { Categories, List, Search, Details, Session };
    struct Pending {
        Operation operation = Operation::Categories;
        VideoApi::Query query;
        int page = 1;
        QString detailId;
        quint64 requestId = 0;
        quint64 generation = 0;
    };
    void start(Pending request);
    void dispatch(Pending request);
    void receive(quint64 requestId, const QByteArray &body, const QString &error);
    void setBusy(bool value);
    QString categoryText(const VideoApi::Query &query) const;

    VideoStoragePaths paths;
    QVector<VideoApi::Category> sourceCategories;
    QVector<VideoApi::Summary> savedFavorites;
    VideoCategoryOrder order;
    VideoSnapshot categoryState;
    VideoSnapshot searchState;
    VideoApi::Details currentDetails;
    QString initialError;
    VideoClient *client = nullptr;
    Pending pending;
    Pending consentRequest;
    quint64 generation = 0;
    bool awaitingConsent = false;
    bool consentGranted = false;
    bool busy = false;
};
