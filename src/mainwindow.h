#pragma once

#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class ChapterDownload;
class BookDownloadWidget;
class QTabWidget;
class QProgressBar;
class QPushButton;
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
    void reportSaved(const QString &outputPath, qint64 bytes, qsizetype characters);

    QTabWidget *tabs = nullptr;
    BookDownloadWidget *bookWidget = nullptr;
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
    ChapterDownload *downloader = nullptr;
};
