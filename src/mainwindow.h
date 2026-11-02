#pragma once

#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QNetworkAccessManager;
class QNetworkReply;
class QProgressBar;
class QPushButton;
class QTemporaryFile;
class QUrl;

class MainWindow : public QWidget {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private:
    void chooseInput();
    void chooseOutput();
    void updatePaths();
    void updateAction();
    void decodeAndSave();
    void convertAndReport(const QString &inputPath, const QString &outputPath, bool allowOverwrite);
    void startDownload(const QUrl &url, const QString &outputPath, bool allowOverwrite);
    void readDownloadData();
    void failDownload(const QString &error);
    void finishDownload(const QString &outputPath, bool allowOverwrite);

    QWidget *pathsWidget = nullptr;
    QComboBox *inputMode = nullptr;
    QLineEdit *inputPathEdit = nullptr;
    QLineEdit *downloadUrlEdit = nullptr;
    QLineEdit *outputPathEdit = nullptr;
    QLineEdit *keyEdit = nullptr;
    QPushButton *decodeButton = nullptr;
    QPushButton *cancelButton = nullptr;
    QProgressBar *downloadProgress = nullptr;
    QLabel *statusLabel = nullptr;
    QNetworkAccessManager *networkManager = nullptr;
    QNetworkReply *downloadReply = nullptr;
    QTemporaryFile *downloadFile = nullptr;
    QString downloadError;
};
