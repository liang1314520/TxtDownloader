#pragma once

#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class ChapterDownload;
class BookDownloadWidget;
class VideoCatalogWidget;
class QButtonGroup;
class QStackedWidget;
class QProgressBar;
class QPushButton;
class QUrl;

class MainWindow : public QWidget {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private:
    enum class Module { Novel, Video };
    void setModule(Module module);
    void setVideoPage(int index);
    void updateModuleActivity();
    void setWorkspacePage(int index);
    void setBookNavigationEnabled(bool enabled);
    void chooseInput();
    void chooseOutput();
    void updatePaths();
    void updateAction();
    void decodeAndSave();
    void convertAndReport(const QString &inputPath, const QString &outputPath, bool allowOverwrite);
    void startDownload(const QUrl &url, const QString &outputPath, bool allowOverwrite);
    void reportSaved(const QString &outputPath, qint64 bytes, qsizetype characters);

    Module currentModule = Module::Novel;
    int novelWorkspaceIndex = 0;
    bool novelBusy = false;
    bool videoBusy = false;
    QButtonGroup *modules = nullptr;
    QButtonGroup *videoNavigation = nullptr;
    QWidget *novelNavigationPanel = nullptr;
    QWidget *videoNavigationPanel = nullptr;
    VideoCatalogWidget *videoWidget = nullptr;
    QStackedWidget *workspace = nullptr;
    QButtonGroup *navigation = nullptr;
    QLabel *breadcrumb = nullptr;
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
