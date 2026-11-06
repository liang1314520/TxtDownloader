#include "mainwindow.h"
#include "novdecoder.h"
#include "chapterdownload.h"
#include "bookdownloadwidget.h"
#include "workbenchtheme.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QScrollArea>
#include <QStackedWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

MainWindow::MainWindow(QWidget *parent) : QWidget(parent)
{
    setWindowTitle(QStringLiteral("NovDecoder · NOV 下载与解码工具"));
    setObjectName(QStringLiteral("workbenchWindow"));
    setStyleSheet(WorkbenchTheme::styleSheet());
    resize(1240, 840);
    setMinimumSize(980, 700);

    auto *outer = new QHBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    auto *sidebar = new QWidget(this);
    sidebar->setObjectName(QStringLiteral("workbenchSidebar"));
    sidebar->setFixedWidth(188);
    auto *sideLayout = new QVBoxLayout(sidebar);
    sideLayout->setContentsMargins(14, 28, 14, 20);
    sideLayout->setSpacing(8);
    auto *brand = new QHBoxLayout;
    auto *logo = new QLabel(sidebar);
    logo->setPixmap(WorkbenchTheme::icon(WorkbenchTheme::Symbol::Book).pixmap(30, 30));
    brand->addWidget(logo);
    brand->addWidget(WorkbenchTheme::label(QStringLiteral("NovDecoder"), "brand", sidebar));
    sideLayout->addLayout(brand);
    sideLayout->addWidget(WorkbenchTheme::label(QStringLiteral("让文字，留在身边"), "muted", sidebar));
    sideLayout->addSpacing(26);
    sideLayout->addWidget(WorkbenchTheme::label(QStringLiteral("工作空间"), "muted", sidebar));
    navigation = new QButtonGroup(this);
    navigation->setExclusive(true);
    const QStringList titles{QStringLiteral("小说书库"), QStringLiteral("我的收藏"), QStringLiteral("整书下载"),
        QStringLiteral("单章转换"), QStringLiteral("会话与连接")};
    const QStringList names{QStringLiteral("libraryNavigationButton"), QStringLiteral("favoritesNavigationButton"),
        QStringLiteral("downloadNavigationButton"), QStringLiteral("convertNavigationButton"), QStringLiteral("sessionNavigationButton")};
    const WorkbenchTheme::Symbol symbols[]{WorkbenchTheme::Symbol::Library, WorkbenchTheme::Symbol::Favorite,
        WorkbenchTheme::Symbol::Download, WorkbenchTheme::Symbol::Convert, WorkbenchTheme::Symbol::Connection};
    const int pageIds[]{0, 4, 1, 2, 3};
    for (int index = 0; index < titles.size(); ++index) {
        if (pageIds[index] == 3) {
            sideLayout->addSpacing(20);
            sideLayout->addWidget(WorkbenchTheme::label(QStringLiteral("连接与配置"), "muted", sidebar));
        }
        auto *button = new QPushButton(WorkbenchTheme::icon(symbols[index]), titles[index], sidebar);
        button->setObjectName(names[index]);
        button->setProperty("nav", true);
        button->setCheckable(true);
        button->setIconSize(QSize(18, 18));
        navigation->addButton(button, pageIds[index]);
        sideLayout->addWidget(button);
    }
    sideLayout->addStretch();
    sideLayout->addWidget(WorkbenchTheme::label(QStringLiteral("本地处理 · 按需联网\n适配 App 4.0.9"), "muted", sidebar));
    outer->addWidget(sidebar);

    auto *workarea = new QWidget(this);
    auto *workLayout = new QVBoxLayout(workarea);
    workLayout->setContentsMargins(0, 0, 0, 0);
    workLayout->setSpacing(0);
    auto *toolbar = new QFrame(workarea);
    toolbar->setObjectName(QStringLiteral("workbenchToolbar"));
    auto *toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(24, 14, 24, 14);
    breadcrumb = WorkbenchTheme::label({}, "muted", toolbar);
    toolbarLayout->addWidget(breadcrumb, 1);
    toolbarLayout->addWidget(WorkbenchTheme::label(QStringLiteral("本地配置受当前 Windows 用户保护"), "muted", toolbar));
    workLayout->addWidget(toolbar);
    workspace = new QStackedWidget(workarea);
    workspace->setObjectName(QStringLiteral("workspaceStack"));
    bookWidget = new BookDownloadWidget(workspace);
    workspace->addWidget(bookWidget);
    auto *singlePage = new QWidget(workspace);
    singlePage->setProperty("pageSurface", true);
    auto *layout = new QVBoxLayout(singlePage);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(16);
    layout->addWidget(WorkbenchTheme::label(QStringLiteral("SIMPLE, LOCAL, YOURS / 单章转换"), "eyebrow", singlePage));
    layout->addWidget(WorkbenchTheme::label(QStringLiteral("一章文字，一份轻巧的 TXT"), "pageTitle", singlePage));
    layout->addWidget(WorkbenchTheme::label(QStringLiteral("本地章节转换或单章链接下载，校验后保存为 UTF-8 TXT。源文件不会修改。"), "muted", singlePage));
    auto *columns = new QHBoxLayout;
    columns->setSpacing(16);
    auto *formCard = WorkbenchTheme::panel(singlePage);
    auto *formLayout = new QVBoxLayout(formCard);
    formLayout->setContentsMargins(22, 22, 22, 22);
    formLayout->setSpacing(16);
    formLayout->addWidget(WorkbenchTheme::label(QStringLiteral("转换工作台"), "sectionTitle", formCard));
    pathsWidget = new QWidget(formCard);
    auto *paths = new QGridLayout(pathsWidget);
    paths->setContentsMargins(0, 0, 0, 0);
    paths->setHorizontalSpacing(10);
    paths->setVerticalSpacing(10);
    inputMode = new QComboBox(pathsWidget);
    inputMode->setObjectName("inputMode");
    inputMode->addItems({QStringLiteral("本地文件"), QStringLiteral("链接下载")});
    paths->addWidget(WorkbenchTheme::label(QStringLiteral("01 / 选择来源"), "muted", pathsWidget), 0, 0, 1, 2);
    paths->addWidget(inputMode, 1, 0, 1, 2);
    auto *inputLabel = WorkbenchTheme::label(QStringLiteral("输入文件"), "muted", pathsWidget);
    paths->addWidget(inputLabel, 2, 0, 1, 2);
    inputPathEdit = new QLineEdit(pathsWidget);
    inputPathEdit->setObjectName("inputPathEdit");
    inputPathEdit->setPlaceholderText(QStringLiteral("选择 .nov 或 response.bin 文件"));
    inputPathEdit->setClearButtonEnabled(true);
    auto *inputButton = new QPushButton(QStringLiteral("选择文件…"), pathsWidget);
    inputButton->setObjectName("chooseInputButton");
    paths->addWidget(inputPathEdit, 3, 0);
    paths->addWidget(inputButton, 3, 1);
    downloadUrlEdit = new QLineEdit(pathsWidget);
    downloadUrlEdit->setObjectName("downloadUrlEdit");
    downloadUrlEdit->setPlaceholderText(QStringLiteral("完整 HTTP(S) 章节链接，保留签名参数"));
    downloadUrlEdit->setClearButtonEnabled(true);
    downloadUrlEdit->setVisible(false);
    paths->addWidget(downloadUrlEdit, 3, 0, 1, 2);
    paths->addWidget(WorkbenchTheme::label(QStringLiteral("02 / 输出 TXT"), "muted", pathsWidget), 4, 0, 1, 2);
    outputPathEdit = new QLineEdit(pathsWidget);
    outputPathEdit->setObjectName("outputPathEdit");
    outputPathEdit->setPlaceholderText(QStringLiteral("选择 TXT 保存位置"));
    outputPathEdit->setClearButtonEnabled(true);
    auto *outputButton = new QPushButton(QStringLiteral("保存位置…"), pathsWidget);
    outputButton->setObjectName("chooseOutputButton");
    paths->addWidget(outputPathEdit, 5, 0);
    paths->addWidget(outputButton, 5, 1);
    paths->addWidget(WorkbenchTheme::label(QStringLiteral("03 / 本次解码密钥"), "muted", pathsWidget), 6, 0, 1, 2);
    keyEdit = new QLineEdit(pathsWidget);
    keyEdit->setObjectName("keyEdit");
    keyEdit->setEchoMode(QLineEdit::Password);
    keyEdit->setMaxLength(16);
    keyEdit->setPlaceholderText(QStringLiteral("16 个可打印 ASCII 字符，不保存或上传"));
    keyEdit->setClearButtonEnabled(true);
    paths->addWidget(keyEdit, 7, 0, 1, 2);
    paths->setColumnStretch(0, 1);
    formLayout->addWidget(pathsWidget);
    formLayout->addWidget(WorkbenchTheme::label(QStringLiteral("仅支持当前已验证的章节格式，单文件上限 32 MiB。本地文件不会上传。"), "muted", formCard));
    auto *actions = new QHBoxLayout;
    actions->addStretch();
    cancelButton = new QPushButton(QStringLiteral("取消下载"), formCard);
    cancelButton->setObjectName("cancelDownloadButton");
    cancelButton->setVisible(false);
    actions->addWidget(cancelButton);
    decodeButton = new QPushButton(QStringLiteral("解密并保存 TXT"), formCard);
    decodeButton->setObjectName("decodeButton");
    decodeButton->setProperty("primary", true);
    decodeButton->setEnabled(false);
    actions->addWidget(decodeButton);
    formLayout->addLayout(actions);
    downloadProgress = new QProgressBar(formCard);
    downloadProgress->setObjectName("downloadProgress");
    downloadProgress->setVisible(false);
    formLayout->addWidget(downloadProgress);
    statusLabel = WorkbenchTheme::label(QStringLiteral("等待选择文件或输入链接。单文件上限 32 MiB。"), "status", formCard);
    statusLabel->setObjectName("statusLabel");
    statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    formLayout->addWidget(statusLabel);
    formLayout->addStretch();
    columns->addWidget(formCard, 3);
    auto *guide = WorkbenchTheme::panel(singlePage);
    guide->setMaximumWidth(270);
    auto *guideLayout = new QVBoxLayout(guide);
    guideLayout->setContentsMargins(20, 22, 20, 22);
    guideLayout->setSpacing(14);
    guideLayout->addWidget(WorkbenchTheme::label(QStringLiteral("三个清晰的步骤"), "sectionTitle", guide));
    guideLayout->addWidget(WorkbenchTheme::label(QStringLiteral("01 / 选择来源"), "eyebrow", guide));
    guideLayout->addWidget(WorkbenchTheme::label(QStringLiteral("本地章节资源，或你有权使用的单章链接。"), "muted", guide));
    guideLayout->addWidget(WorkbenchTheme::label(QStringLiteral("02 / 设置输出"), "eyebrow", guide));
    guideLayout->addWidget(WorkbenchTheme::label(QStringLiteral("指定 TXT 保存位置。已有文件需确认覆盖，源文件保持不变。"), "muted", guide));
    guideLayout->addWidget(WorkbenchTheme::label(QStringLiteral("03 / 转换与保存"), "eyebrow", guide));
    guideLayout->addWidget(WorkbenchTheme::label(QStringLiteral("使用本次密钥校验并转换；出错时不会发布不完整 TXT。"), "muted", guide));
    guideLayout->addStretch();
    guideLayout->addWidget(WorkbenchTheme::label(QStringLiteral("整本书的目录查询和合并，请使用“整书下载”。"), "muted", guide));
    columns->addWidget(guide, 1);
    layout->addLayout(columns, 1);
    layout->addWidget(WorkbenchTheme::label(QStringLiteral("仅处理你有权保存的内容，不绕过账号或阅读权限。签名链接过期时请重新获取。"), "notice", singlePage));
    auto *singleScroll = new QScrollArea(workspace);
    singleScroll->setWidgetResizable(true);
    singleScroll->setWidget(singlePage);
    workspace->addWidget(singleScroll);
    workLayout->addWidget(workspace, 1);
    outer->addWidget(workarea, 1);

    downloader = new ChapterDownload(this);
    connect(downloader, &ChapterDownload::progress, this, [this](qint64 received, qint64 total) {
        downloadProgress->setRange(0, total > 0 ? 100 : 0);
        if (total > 0)
            downloadProgress->setValue(static_cast<int>(received * 100 / total));
        statusLabel->setText(QStringLiteral("正在下载：%1 KiB%2")
                                 .arg(received / 1024)
                                 .arg(total > 0 ? QStringLiteral(" / %1 KiB").arg(total / 1024) : QString()));
    });
    connect(downloader, &ChapterDownload::finished, this,
            [this](bool success, const QString &message, qint64 bytes, qsizetype characters) {
        cancelButton->setVisible(false);
        downloadProgress->setVisible(false);
        pathsWidget->setEnabled(true);
        setBookNavigationEnabled(true);
        if (success)
            reportSaved(outputPathEdit->text(), bytes, characters);
        else
            statusLabel->setText(QStringLiteral("未保存：%1").arg(message));
        updateAction();
    });
    connect(bookWidget, &BookDownloadWidget::busyChanged, this, [this](bool busy) {
        navigation->button(2)->setEnabled(!busy);
    });
    connect(navigation, &QButtonGroup::idClicked, this, &MainWindow::setWorkspacePage);
    connect(bookWidget, &BookDownloadWidget::downloadPageRequested, this, [this] {
        setWorkspacePage(1);
    });
    connect(inputButton, &QPushButton::clicked, this, &MainWindow::chooseInput);
    connect(outputButton, &QPushButton::clicked, this, &MainWindow::chooseOutput);
    connect(inputMode, &QComboBox::currentIndexChanged, this,
            [this, inputButton, inputLabel](int index) {
        const bool fromUrl = index == 1;
        inputPathEdit->setVisible(!fromUrl);
        inputButton->setVisible(!fromUrl);
        downloadUrlEdit->setVisible(fromUrl);
        inputLabel->setText(fromUrl ? QStringLiteral("下载链接") : QStringLiteral("输入文件"));
        decodeButton->setText(fromUrl ? QStringLiteral("下载并转换 TXT") : QStringLiteral("解密并保存 TXT"));
        updatePaths();
    });
    connect(inputPathEdit, &QLineEdit::textChanged, this, &MainWindow::updatePaths);
    connect(downloadUrlEdit, &QLineEdit::textChanged, this, &MainWindow::updatePaths);
    connect(outputPathEdit, &QLineEdit::textChanged, this, &MainWindow::updateAction);
    connect(keyEdit, &QLineEdit::textChanged, this, &MainWindow::updateAction);
    connect(decodeButton, &QPushButton::clicked, this, &MainWindow::decodeAndSave);
    connect(cancelButton, &QPushButton::clicked, this, [this] {
        cancelButton->setEnabled(false);
        downloader->cancel();
    });
    setWorkspacePage(0);
}

MainWindow::~MainWindow() = default;

void MainWindow::setWorkspacePage(int index)
{
    auto *button = navigation->button(index);
    if (!button || !button->isEnabled())
        return;
    button->setChecked(true);
    if (index == 2) {
        workspace->setCurrentIndex(1);
    } else {
        workspace->setCurrentIndex(0);
        bookWidget->setPage(index == 0 ? BookDownloadWidget::Page::Library
            : index == 4 ? BookDownloadWidget::Page::Favorites
            : index == 1 ? BookDownloadWidget::Page::Download : BookDownloadWidget::Page::Session);
    }
    breadcrumb->setText(QStringLiteral("工作空间  /  %1").arg(button->text()));
}

void MainWindow::setBookNavigationEnabled(bool enabled)
{
    for (const auto index : {0, 4, 1, 3})
        navigation->button(index)->setEnabled(enabled);
}

void MainWindow::chooseInput()
{
    const auto path = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择章节资源文件"), inputPathEdit->text(),
        QStringLiteral("章节资源 (*.nov *.bin);;所有文件 (*)"));
    if (!path.isEmpty())
        inputPathEdit->setText(path);
}

void MainWindow::chooseOutput()
{
    const auto path = QFileDialog::getSaveFileName(
        this, QStringLiteral("保存解码后的 TXT"), outputPathEdit->text(),
        QStringLiteral("文本文件 (*.txt)"), nullptr, QFileDialog::DontConfirmOverwrite);
    if (!path.isEmpty())
        outputPathEdit->setText(path);
}

void MainWindow::updatePaths()
{
    if (inputMode->currentIndex() == 1) {
        const QUrl url(downloadUrlEdit->text().trimmed(), QUrl::StrictMode);
        if (!ChapterDownload::isDownloadUrl(url)) {
            outputPathEdit->clear();
        } else {
            auto name = QFileInfo(url.fileName()).completeBaseName().left(120);
            name.replace(QRegularExpression(QStringLiteral(R"([<>:"/\\|?*\x00-\x1F])")), QStringLiteral("_"));
            name.remove(QRegularExpression(QStringLiteral("[. ]+$")));
            const QRegularExpression reservedName(
                QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\\..*)?$"),
                QRegularExpression::CaseInsensitiveOption);
            if (name.isEmpty() || reservedName.match(name).hasMatch())
                name = QStringLiteral("chapter");
            auto directory = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
            if (directory.isEmpty())
                directory = QDir::homePath();
            outputPathEdit->setText(QDir(directory).filePath(name + ".txt"));
        }
        statusLabel->setText(QStringLiteral("仅在点击下载后访问链接；下载成功并校验通过后才会保存 TXT。"));
    } else {
        const QFileInfo input(inputPathEdit->text());
        if (inputPathEdit->text().isEmpty())
            outputPathEdit->clear();
        else
            outputPathEdit->setText(input.dir().filePath(input.completeBaseName() + ".txt"));
        statusLabel->setText(QStringLiteral("就绪。解码成功后才会写入 TXT，源文件将保留。"));
    }
    updateAction();
}

void MainWindow::updateAction()
{
    const bool hasInput = inputMode->currentIndex() == 1
        ? ChapterDownload::isDownloadUrl(QUrl(downloadUrlEdit->text().trimmed(), QUrl::StrictMode))
        : !inputPathEdit->text().isEmpty();
    const auto key = keyEdit->text();
    const bool hasKey = key.size() == 16
        && std::all_of(key.cbegin(), key.cend(), [](QChar ch) {
            return ch.unicode() >= 0x20 && ch.unicode() <= 0x7e;
        });
    decodeButton->setEnabled(!downloader->isBusy() && hasInput && hasKey && !outputPathEdit->text().isEmpty());
}

void MainWindow::decodeAndSave()
{
    const auto outputPath = outputPathEdit->text();
    const bool fromUrl = inputMode->currentIndex() == 1;
    const QUrl url(downloadUrlEdit->text().trimmed(), QUrl::StrictMode);
    if (fromUrl && !ChapterDownload::isDownloadUrl(url)) {
        statusLabel->setText(QStringLiteral("请输入完整的 HTTP(S) 下载链接，不支持包含账号密码的链接。"));
        return;
    }

    bool allowOverwrite = false;
    if (QFileInfo::exists(outputPath)) {
        allowOverwrite = QMessageBox::question(
            this, QStringLiteral("确认覆盖 TXT"),
            QStringLiteral("输出文件已存在：\n%1\n\n是否覆盖？").arg(QDir::toNativeSeparators(outputPath)),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
        if (!allowOverwrite) {
            statusLabel->setText(QStringLiteral("已取消，未修改任何文件。"));
            return;
        }
    }

    if (fromUrl)
        startDownload(url, outputPath, allowOverwrite);
    else
        convertAndReport(inputPathEdit->text(), outputPath, allowOverwrite);
}

void MainWindow::convertAndReport(const QString &inputPath, const QString &outputPath, bool allowOverwrite)
{
    decodeButton->setEnabled(false);
    const auto result = NovDecoder::convertFile(inputPath, outputPath, keyEdit->text().toLatin1(), allowOverwrite);
    updateAction();
    if (!result.ok()) {
        statusLabel->setText(QStringLiteral("未保存：%1").arg(result.error));
        return;
    }
    reportSaved(outputPath, result.bytesWritten, result.characters);
}

void MainWindow::reportSaved(const QString &outputPath, qint64 bytes, qsizetype characters)
{
    statusLabel->setText(QStringLiteral("已保存：%1\nUTF-8 · %2 字节 · %3 个字符（含换行与标点）。")
                             .arg(QDir::toNativeSeparators(QFileInfo(outputPath).absoluteFilePath()))
                             .arg(bytes).arg(characters));
}

void MainWindow::startDownload(const QUrl &url, const QString &outputPath, bool allowOverwrite)
{
    pathsWidget->setEnabled(false);
    setBookNavigationEnabled(false);
    decodeButton->setEnabled(false);
    cancelButton->setEnabled(true);
    cancelButton->setVisible(true);
    downloadProgress->setRange(0, 0);
    downloadProgress->setVisible(true);
    statusLabel->setText(QStringLiteral("正在下载章节资源…"));
    downloader->start(url, outputPath, keyEdit->text().toLatin1(), allowOverwrite);
}
