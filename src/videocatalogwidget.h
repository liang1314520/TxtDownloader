#pragma once

#include "videostore.h"

#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QListView;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QTabWidget;
class VideoCatalogController;
class VideoCoverCache;
class VideoListModel;

class VideoCatalogWidget : public QWidget {
    Q_OBJECT
public:
    enum class Page { Categories, Favorites, Connection };
    explicit VideoCatalogWidget(QWidget *parent = nullptr);
    VideoCatalogWidget(const VideoStoragePaths &paths, QWidget *parent);
    void setPage(Page page);
signals:
    void busyChanged(bool busy);
private:
    void updateCategories();
    void updateChildren(const QString &selected = {});
    void selectCachedCategory();
    void updateResults(bool search);
    void updateFavorites();
    void updateDetails();
    void showCurrentDetails();
    void updateConnection();
    void updateActions();
    void showOrderDialog();
    void toggleFavorite(const VideoApi::Summary &item);
    void showError(const QString &message);
    VideoCatalogController *controller;
    VideoCoverCache *covers;
    VideoListModel *categoryModel;
    VideoListModel *searchModel;
    VideoListModel *favoritesModel;
    QStackedWidget *pages;
    QComboBox *parents;
    QComboBox *children;
    QComboBox *parts;
    QLineEdit *searchEdit;
    QLineEdit *favoritesFilter;
    QTabWidget *results;
    QListView *categoryView;
    QListView *searchView;
    QListView *favoritesView;
    QPlainTextEdit *detailsView;
    QLabel *status;
    QLabel *categoryStatus;
    QLabel *searchStatus;
    QLabel *connectionStatus;
    QPushButton *refreshCategoriesButton;
    QPushButton *loadCategoryButton;
    QPushButton *orderButton;
    QPushButton *searchButton;
    QPushButton *loadMoreButton;
    QPushButton *refreshDetailsButton;
    QPushButton *cancelButton;
    QPushButton *retrySessionButton;
    QPushButton *clearSessionButton;
};
