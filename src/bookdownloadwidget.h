#pragma once

#include <QWidget>

class BookDownloadController;
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
    void invalidateSession();
    bool confirmNetwork();

    BookDownloadController *controller;
    QWidget *settings;
    QLineEdit *bookId;
    QLineEdit *capturePath;
    QLineEdit *outputDirectory;
    QPushButton *catalogButton;
    QPushButton *linksButton;
    QPushButton *downloadButton;
    QPushButton *cancelButton;
    QPushButton *copyButton;
    QLabel *bookLabel;
    QLabel *statusLabel;
    QLabel *fileProgress;
    QProgressBar *progress;
    QTableWidget *table;
    bool networkConfirmed = false;
};
