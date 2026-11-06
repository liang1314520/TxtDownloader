#pragma once

#include <QWidget>

class BookDownloadController;
class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QPlainTextEdit;
class QStackedWidget;
class QTableWidget;
struct NovelBook;
struct NovelSearchBook;

class BookDownloadWidget : public QWidget {
    Q_OBJECT

public:
    enum class Page { Library, Download, Session, Favorites };

    explicit BookDownloadWidget(QWidget *parent = nullptr);
    void setPage(Page page);

signals:
    void busyChanged(bool busy);
    void downloadPageRequested();

private:
    void buildLibraryPage();
    void buildDownloadPage();
    void buildSessionPage();
    void buildFavoritesPage();
    const NovelSearchBook *selectedSearchBook() const;
    const NovelSearchBook *selectedFavoriteBook() const;
    void updateSelectedBook();
    void updateSelectedFavorite();
    void showFavorites();
    void applyFavoritesFilter();
    void updateFavoriteMarkers();
    void changeFavorite(const NovelSearchBook &book, bool favorite);
    void updateActions();
    void showBook(const NovelBook &book);
    void showSearchResults();
    void applyResultFilter();
    void searchGroupBooks();
    void selectSearchBook();
    void showBookDetails();
    void showBookDetailsFor(const NovelSearchBook &book);
    void showAccountLogin();
    void showMuMuCapture(bool categoryOnly = false);
    void updateCategories();
    void renameCurrentCategory();
    void removeCurrentCategory();
    void reorderCurrentCategories();
    void refreshDeviceSession();
    void updateSessionStatus();
    void invalidateSession();
    bool confirmNetwork();
    bool confirmDeviceIdentity();
    bool hasSessionSource() const;

    BookDownloadController *controller;
    QStackedWidget *pages;
    QWidget *downloadSettings;
    QWidget *sessionSettings;
    QPushButton *captureFileButton;
    QPushButton *directoryButton;
    QPushButton *openDownloadButton;
    QLabel *selectedTitle;
    QLabel *selectedMetadata;
    QLabel *selectedId;
    QPlainTextEdit *selectedDescription;
    QLabel *catalogMetric;
    QLabel *completedMetric;
    QTableWidget *favoritesTable;
    QLineEdit *favoritesFilter;
    QLabel *favoritesSummary;
    QLabel *favoriteTitle;
    QLabel *favoriteMetadata;
    QLabel *favoriteId;
    QPlainTextEdit *favoriteDescription;
    QPushButton *removeFavoriteButton;
    QPushButton *favoriteDetailButton;
    QPushButton *favoriteDownloadButton;
    QLineEdit *bookId;
    QLineEdit *keywords;
    QLineEdit *capturePath;
    QLineEdit *outputDirectory;
    QPushButton *catalogButton;
    QPushButton *linksButton;
    QPushButton *downloadButton;
    QPushButton *cancelButton;
    QPushButton *copyButton;
    QPushButton *searchButton;
    QPushButton *globalSearchButton;
    QPushButton *moreSearchButton;
    QPushButton *detailButton;
    QLabel *sessionLabel;
    QPushButton *importSessionButton;
    QPushButton *deviceSessionButton;
    QPushButton *accountLoginButton;
    QPushButton *captureSessionButton;
    QPushButton *forgetSessionButton;
    QLabel *searchLabel;
    QLabel *bookLabel;
    QLabel *statusLabel;
    QLabel *fileProgress;
    QProgressBar *progress;
    QTableWidget *table;
    QTableWidget *searchTable;
    bool networkConfirmed = false;
    bool loadAll = false;
    bool pageSucceeded = false;
    QComboBox *categoryCombo;
    QPushButton *captureCategoryButton;
    QPushButton *renameCategoryButton;
    QPushButton *deleteCategoryButton;
    QPushButton *reorderCategoriesButton;
    QPushButton *groupButton;
    QPushButton *refreshGroupButton;
    QPushButton *allButton;
    QLineEdit *resultFilter;
};
