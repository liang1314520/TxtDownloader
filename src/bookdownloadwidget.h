#pragma once

#include <QWidget>

class BookDownloadController;
class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QTableWidget;
struct NovelBook;

class BookDownloadWidget : public QWidget {
    Q_OBJECT

public:
    explicit BookDownloadWidget(QWidget *parent = nullptr);

signals:
    void busyChanged(bool busy);

private:
    void updateActions();
    void showBook(const NovelBook &book);
    void showSearchResults();
    void applyResultFilter();
    void searchGroupBooks();
    void selectSearchBook();
    void showBookDetails();
    void showAccountLogin();
    void showMuMuCapture(bool categoryOnly = false);
    void updateCategories();
    void renameCurrentCategory();
    void removeCurrentCategory();
    void refreshDeviceSession();
    void updateSessionStatus();
    void invalidateSession();
    bool confirmNetwork();
    bool confirmDeviceIdentity();
    bool hasSessionSource() const;

    BookDownloadController *controller;
    QWidget *settings;
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
    QPushButton *groupButton;
    QPushButton *refreshGroupButton;
    QPushButton *allButton;
    QLineEdit *resultFilter;
};
