#include "bookdownloadwidget.h"
#include "bookdownloadcontroller.h"
#include "bookdetaildialog.h"
#include "mumucapturedialog.h"

#include <algorithm>

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QInputDialog>
#include <QJsonDocument>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QScrollBar>
#include <QTimer>
#include <QStandardPaths>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
class ChapterCountItem : public QTableWidgetItem {
public:
    explicit ChapterCountItem(const QString &text) : QTableWidgetItem(text) {}

    bool operator<(const QTableWidgetItem &other) const override
    {
        bool leftValid = false;
        bool rightValid = false;
        const auto left = text().toLongLong(&leftValid);
        const auto right = other.text().toLongLong(&rightValid);
        leftValid = leftValid && left >= 0;
        rightValid = rightValid && right >= 0;
        if (leftValid != rightValid)
            return !leftValid;
        return leftValid ? left < right : text() < other.text();
    }
};
}

BookDownloadWidget::BookDownloadWidget(QWidget *parent)
    : QWidget(parent), controller(new BookDownloadController(this))
{
    auto *layout = new QVBoxLayout(this);
    auto *notice = new QLabel(QStringLiteral(
        "默认在指定分类内筛选书名／作者；跨分类查找请使用“全站搜索”。单击选书、双击查看简介与预览。\n"
        "支持直接获取设备会话或账号登录，会话由 Windows DPAPI 加密复用；不保存密码，仅处理当前会话免费可阅读的章节。"), this);
    notice->setWordWrap(true);
    layout->addWidget(notice);
    settings = new QWidget(this);
    auto *grid = new QGridLayout(settings);
    grid->setContentsMargins(0, 0, 0, 0);
    bookId = new QLineEdit(settings);
    bookId->setObjectName(QStringLiteral("bookIdEdit"));
    bookId->setMaxLength(32);
    bookId->setPlaceholderText(QStringLiteral("32 位书籍 ID，可手动填写或从搜索结果选取，不是章节 ID"));
    keywords = new QLineEdit(settings);
    keywords->setObjectName(QStringLiteral("bookSearchKeywordsEdit"));
    keywords->setMaxLength(256);
    keywords->setPlaceholderText(QStringLiteral("输入书名／作者，默认仅在当前分类中查找"));
    searchButton = new QPushButton(QStringLiteral("分类内查找"), settings);
    searchButton->setObjectName(QStringLiteral("searchBooksButton"));
    searchButton->setAutoDefault(false);
    globalSearchButton = new QPushButton(QStringLiteral("全站搜索"), settings);
    globalSearchButton->setObjectName(QStringLiteral("globalBookSearchButton"));
    globalSearchButton->setToolTip(QStringLiteral("服务器关键词搜索，不限定分类；结果不代表属于当前分类。"));
    globalSearchButton->setAutoDefault(false);
    capturePath = new QLineEdit(settings);
    capturePath->setObjectName(QStringLiteral("sessionCaptureEdit"));
    capturePath->setPlaceholderText(QStringLiteral("备用：选择自己的 http_req_*.hcy，点击导入后可跨重启复用"));
    auto *captureButton = new QPushButton(QStringLiteral("选择抓包…"), settings);
    importSessionButton = new QPushButton(QStringLiteral("导入并记住"), settings);
    importSessionButton->setObjectName(QStringLiteral("importSessionButton"));
    outputDirectory = new QLineEdit(settings);
    outputDirectory->setObjectName(QStringLiteral("bookOutputDirectoryEdit"));
    outputDirectory->setText(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
    auto *directoryButton = new QPushButton(QStringLiteral("保存目录…"), settings);
    grid->addWidget(new QLabel(QStringLiteral("书名查找"), settings), 0, 0);
    grid->addWidget(keywords, 0, 1);
    auto *searchActions = new QHBoxLayout;
    searchActions->addWidget(searchButton);
    searchActions->addWidget(globalSearchButton);
    grid->addLayout(searchActions, 0, 2);
    grid->addWidget(new QLabel(QStringLiteral("书籍 ID"), settings), 1, 0);
    grid->addWidget(bookId, 1, 1, 1, 2);
    grid->addWidget(new QLabel(QStringLiteral("备用请求抓包"), settings), 2, 0);
    grid->addWidget(capturePath, 2, 1);
    auto *captureActions = new QHBoxLayout;
    captureActions->addWidget(captureButton);
    captureActions->addWidget(importSessionButton);
    grid->addLayout(captureActions, 2, 2);
    grid->addWidget(new QLabel(QStringLiteral("TXT 保存目录"), settings), 3, 0);
    grid->addWidget(outputDirectory, 3, 1);
    grid->addWidget(directoryButton, 3, 2);
    auto *sessionActions = new QHBoxLayout;
    deviceSessionButton = new QPushButton(QStringLiteral("获取／刷新设备会话"), settings);
    deviceSessionButton->setObjectName(QStringLiteral("deviceSessionButton"));
    accountLoginButton = new QPushButton(QStringLiteral("账号登录…"), settings);
    accountLoginButton->setObjectName(QStringLiteral("accountLoginButton"));
    captureSessionButton = new QPushButton(QStringLiteral("MuMu 自动采集…"), settings);
    captureSessionButton->setObjectName(QStringLiteral("captureSessionButton"));
    forgetSessionButton = new QPushButton(QStringLiteral("清除会话"), settings);
    forgetSessionButton->setObjectName(QStringLiteral("forgetSessionButton"));
    sessionActions->addWidget(deviceSessionButton);
    sessionActions->addWidget(accountLoginButton);
    sessionActions->addWidget(captureSessionButton);
    sessionActions->addWidget(forgetSessionButton);
    sessionActions->addStretch();
    grid->addWidget(new QLabel(QStringLiteral("直接会话"), settings), 4, 0);
    grid->addLayout(sessionActions, 4, 1, 1, 2);
    sessionLabel = new QLabel(controller->sessionDescription(), settings);
    sessionLabel->setObjectName(QStringLiteral("sessionStateLabel"));
    sessionLabel->setTextFormat(Qt::PlainText);
    sessionLabel->setWordWrap(true);
    grid->addWidget(sessionLabel, 5, 0, 1, 3);
    grid->setColumnStretch(1, 1);
    layout->addWidget(settings);

    auto *categoryRow = new QHBoxLayout;
    categoryCombo = new QComboBox(this);
    categoryCombo->setObjectName(QStringLiteral("bookCategoryCombo"));
    captureCategoryButton = new QPushButton(QStringLiteral("采集添加分类…"), this);
    captureCategoryButton->setObjectName(QStringLiteral("captureCategoryButton"));
    renameCategoryButton = new QPushButton(QStringLiteral("重命名…"), this);
    renameCategoryButton->setObjectName(QStringLiteral("renameCategoryButton"));
    deleteCategoryButton = new QPushButton(QStringLiteral("删除…"), this);
    deleteCategoryButton->setObjectName(QStringLiteral("deleteCategoryButton"));
    categoryRow->addWidget(new QLabel(QStringLiteral("小说分类"), this));
    categoryRow->addWidget(categoryCombo, 1);
    categoryRow->addWidget(renameCategoryButton);
    categoryRow->addWidget(deleteCategoryButton);
    categoryRow->addWidget(captureCategoryButton);
    layout->addLayout(categoryRow);
    auto *searchRow = new QHBoxLayout;
    searchLabel = new QLabel(QStringLiteral("搜索结果：单击书籍自动填入 ID，不会立即下载。"), this);
    searchLabel->setTextFormat(Qt::PlainText);
    searchLabel->setWordWrap(true);
    moreSearchButton = new QPushButton(QStringLiteral("加载更多结果"), this);
    moreSearchButton->setObjectName(QStringLiteral("loadMoreBooksButton"));
    groupButton = new QPushButton(QStringLiteral("浏览当前分类"), this);
    groupButton->setObjectName(QStringLiteral("browseGroupButton"));
    refreshGroupButton = new QPushButton(QStringLiteral("刷新分组"), this);
    refreshGroupButton->setObjectName(QStringLiteral("refreshGroupButton"));
    allButton = new QPushButton(QStringLiteral("加载全部"), this);
    searchRow->addWidget(groupButton);
    searchRow->addWidget(refreshGroupButton);
    searchRow->addWidget(allButton);
    searchRow->addWidget(searchLabel, 1);
    searchRow->addWidget(moreSearchButton);
    layout->addLayout(searchRow);
    searchTable = new QTableWidget(0, 5, this);
    searchTable->setObjectName(QStringLiteral("bookSearchResultsTable"));
    searchTable->setHorizontalHeaderLabels({QStringLiteral("书名"), QStringLiteral("作者"),
                                             QStringLiteral("章节数"), QStringLiteral("状态"), QStringLiteral("书籍 ID")});
    searchTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    searchTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    searchTable->setSelectionMode(QAbstractItemView::SingleSelection);
    searchTable->setWordWrap(false);
    searchTable->setTextElideMode(Qt::ElideRight);
    searchTable->verticalHeader()->hide();
    searchTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    searchTable->horizontalHeader()->setStretchLastSection(true);
    searchTable->setColumnWidth(0, 260);
    searchTable->setColumnWidth(1, 130);
    searchTable->setColumnWidth(2, 80);
    searchTable->setColumnWidth(3, 100);
    searchTable->setMinimumHeight(110);
    searchTable->setMaximumHeight(200);
    layout->addWidget(searchTable);
    resultFilter = new QLineEdit(this);
    resultFilter->setObjectName(QStringLiteral("bookResultFilterEdit"));
    resultFilter->setPlaceholderText(QStringLiteral("筛选本列表书名／作者；非完整列表可能遗漏未加载书籍"));
    auto *filterRow = new QHBoxLayout;
    detailButton = new QPushButton(QStringLiteral("查看详情／预览"), this);
    detailButton->setObjectName(QStringLiteral("bookDetailsButton"));
    filterRow->addWidget(resultFilter, 1);
    filterRow->addWidget(detailButton);
    layout->addLayout(filterRow);
    searchTable->setSortingEnabled(true);
    connect(resultFilter, &QLineEdit::textChanged, this, &BookDownloadWidget::applyResultFilter);
    connect(groupButton, &QPushButton::clicked, this, [this] {
        if (controller->hasGroupCache() || confirmNetwork()) {
            loadAll = false;
            resultFilter->clear();
            controller->browseGroup(capturePath->text().trimmed());
        }
    });
    connect(refreshGroupButton, &QPushButton::clicked, this, [this] {
        if (confirmNetwork()) {
            loadAll = false;
            resultFilter->clear();
            controller->browseGroup(capturePath->text().trimmed(), true);
        }
    });
    connect(allButton, &QPushButton::clicked, this, [this] {
        if (loadAll) {
            loadAll = false;
            controller->cancel();
        } else if (confirmNetwork()) {
            loadAll = true;
            controller->loadMoreSearch();
        }
        updateActions();
    });
    connect(searchTable->horizontalHeader(), &QHeaderView::sectionClicked, this, [this](int column) {
        applyResultFilter();
        if (column != 2 || controller->isBusy() || !hasSessionSource()
            || !controller->canLoadMoreSearch())
            return;
        if (confirmNetwork()) {
            loadAll = true;
            controller->loadMoreSearch();
            searchLabel->setText(QStringLiteral("正在加载剩余分页，完成后按章节数统一排序；可点击停止加载。"));
            updateActions();
        }
    });
    connect(searchTable->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        if (value > 0 && value >= searchTable->verticalScrollBar()->maximum() - 1
            && !controller->isBusy() && controller->canLoadMoreSearch())
            moreSearchButton->click();
    });

    auto *actions = new QHBoxLayout;
    catalogButton = new QPushButton(QStringLiteral("仅获取目录"), this);
    linksButton = new QPushButton(QStringLiteral("仅获取全部链接"), this);
    downloadButton = new QPushButton(QStringLiteral("下载并合并"), this);
    downloadButton->setAutoDefault(false);
    cancelButton = new QPushButton(QStringLiteral("取消任务"), this);
    copyButton = new QPushButton(QStringLiteral("复制选中链接"), this);
    actions->addWidget(downloadButton);
    actions->addWidget(catalogButton);
    actions->addWidget(linksButton);
    actions->addStretch();
    actions->addWidget(cancelButton);
    layout->addLayout(actions);
    bookLabel = new QLabel(QStringLiteral("尚未获取书籍目录。"), this);
    bookLabel->setTextFormat(Qt::PlainText);
    bookLabel->setWordWrap(true);
    layout->addWidget(bookLabel);

    table = new QTableWidget(0, 5, this);
    table->setObjectName(QStringLiteral("bookChaptersTable"));
    table->setHorizontalHeaderLabels({QStringLiteral("顺序"), QStringLiteral("章节"), QStringLiteral("章节 ID"),
                                      QStringLiteral("状态"), QStringLiteral("正文链接（含时效签名）")});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setWordWrap(false);
    table->setTextElideMode(Qt::ElideRight);
    table->verticalHeader()->hide();
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table->horizontalHeader()->setStretchLastSection(true);
    table->setColumnWidth(0, 55);
    table->setColumnWidth(1, 150);
    table->setColumnWidth(2, 210);
    table->setColumnWidth(3, 180);
    layout->addWidget(table, 1);
    auto *copyRow = new QHBoxLayout;
    copyRow->addWidget(new QLabel(QStringLiteral("签名链接请勿公开分享；下载时会重新查询。"), this));
    copyRow->addStretch();
    copyRow->addWidget(copyButton);
    layout->addLayout(copyRow);
    progress = new QProgressBar(this);
    progress->setRange(0, 1);
    progress->setValue(0);
    layout->addWidget(progress);
    fileProgress = new QLabel(this);
    fileProgress->setTextFormat(Qt::PlainText);
    layout->addWidget(fileProgress);
    statusLabel = new QLabel(QStringLiteral("获取设备会话、账号登录或导入一次抓包后，即可搜索与预览；有效会话会加密复用。"), this);
    statusLabel->setTextFormat(Qt::PlainText);
    statusLabel->setWordWrap(true);
    statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(statusLabel);

    connect(captureButton, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("选择原始 HTTP 请求抓包"),
                                                     capturePath->text(), QStringLiteral("请求抓包 (*.hcy)"));
        if (!path.isEmpty())
            capturePath->setText(path);
    });
    connect(importSessionButton, &QPushButton::clicked, this, [this] {
        loadAll = false;
        controller->importCapture(capturePath->text().trimmed());
    });
    connect(deviceSessionButton, &QPushButton::clicked, this, &BookDownloadWidget::refreshDeviceSession);
    connect(accountLoginButton, &QPushButton::clicked, this, &BookDownloadWidget::showAccountLogin);
    connect(captureSessionButton, &QPushButton::clicked, this, [this] { showMuMuCapture(); });
    connect(captureCategoryButton, &QPushButton::clicked, this, [this] { showMuMuCapture(true); });
    connect(renameCategoryButton, &QPushButton::clicked, this, &BookDownloadWidget::renameCurrentCategory);
    connect(deleteCategoryButton, &QPushButton::clicked, this, &BookDownloadWidget::removeCurrentCategory);
    connect(controller, &BookDownloadController::categoriesChanged, this, &BookDownloadWidget::updateCategories);
    connect(categoryCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index < 0)
            return;
        loadAll = false;
        resultFilter->clear();
        const auto error = controller->selectCategory(categoryCombo->itemData(index).toString());
        if (!error.isEmpty())
            statusLabel->setText(error);
        else
            statusLabel->setText(QStringLiteral("已选择分类“%1”；缓存可直接浏览，点击“浏览当前分类”才会按需联网。")
                                     .arg(controller->currentCategory().name));
    });
    connect(forgetSessionButton, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, QStringLiteral("清除会话"),
                QStringLiteral("清除本机保存的会话凭证并保留原设备身份？此操作不会注销服务器账号，也不会自动换号。"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes) {
            loadAll = false;
            networkConfirmed = false;
            controller->forgetSession();
        }
    });
    connect(controller, &BookDownloadController::sessionChanged, this, &BookDownloadWidget::updateSessionStatus);
    connect(directoryButton, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getExistingDirectory(this, QStringLiteral("选择 TXT 保存目录"),
                                                          outputDirectory->text());
        if (!path.isEmpty())
            outputDirectory->setText(path);
    });
    connect(capturePath, &QLineEdit::textChanged, this, &BookDownloadWidget::invalidateSession);
    connect(bookId, &QLineEdit::textChanged, this, [this] {
        controller->invalidateBook();
        updateActions();
    });
    connect(keywords, &QLineEdit::textChanged, this, [this] {
        controller->invalidateSearch();
        updateActions();
    });
    connect(outputDirectory, &QLineEdit::textChanged, this, &BookDownloadWidget::updateActions);
    connect(searchButton, &QPushButton::clicked, this, &BookDownloadWidget::searchGroupBooks);
    connect(globalSearchButton, &QPushButton::clicked, this, [this] {
        if (confirmNetwork()) {
            loadAll = false;
            resultFilter->clear();
            controller->searchBooks(keywords->text().trimmed(), capturePath->text().trimmed());
        }
    });
    connect(keywords, &QLineEdit::returnPressed, this, [this] {
        searchButton->click();
    });
    connect(moreSearchButton, &QPushButton::clicked, this, [this] {
        if (confirmNetwork())
            controller->loadMoreSearch();
    });
    connect(searchTable, &QTableWidget::itemSelectionChanged, this, &BookDownloadWidget::selectSearchBook);
    connect(searchTable, &QTableWidget::itemClicked, this, &BookDownloadWidget::selectSearchBook);
    connect(searchTable, &QTableWidget::itemDoubleClicked, this, &BookDownloadWidget::showBookDetails);
    connect(detailButton, &QPushButton::clicked, this, &BookDownloadWidget::showBookDetails);
    connect(controller, &BookDownloadController::searchResultsChanged, this, &BookDownloadWidget::showSearchResults);
    connect(catalogButton, &QPushButton::clicked, this, [this] {
        if (confirmNetwork())
            controller->fetchBook(bookId->text().trimmed(), capturePath->text().trimmed());
    });
    connect(linksButton, &QPushButton::clicked, controller, &BookDownloadController::fetchLinks);
    connect(downloadButton, &QPushButton::clicked, this, [this] {
        if (confirmNetwork())
            controller->downloadBook(bookId->text().trimmed(), capturePath->text().trimmed(),
                                     outputDirectory->text().trimmed());
    });
    connect(cancelButton, &QPushButton::clicked, this, [this] {
        loadAll = false;
        controller->cancel();
    });
    connect(controller, &BookDownloadController::bookChanged, this, &BookDownloadWidget::showBook);
    connect(controller, &BookDownloadController::statusChanged, statusLabel, &QLabel::setText);
    connect(controller, &BookDownloadController::busyChanged, this, [this](bool busy) {
        if (busy)
            pageSucceeded = false;
        settings->setEnabled(!busy);
        searchTable->setEnabled(!busy);
        if (!busy) {
            if (loadAll && pageSucceeded && controller->canLoadMoreSearch()) {
                QTimer::singleShot(0, this, [this] {
                    if (loadAll && !controller->isBusy())
                        controller->loadMoreSearch();
                });
            } else {
                loadAll = false;
            }
            fileProgress->clear();
            if (progress->maximum() == 0) {
                progress->setRange(0, 1);
                progress->setValue(0);
                progress->setFormat(QStringLiteral("搜索任务已结束 · 当前 %1 本").arg(controller->searchResults().size()));
            }
        }
        updateActions();
        emit busyChanged(busy);
    });
    connect(controller, &BookDownloadController::chapterChanged, this,
            [this](int row, const QUrl &content, const QString &status) {
        table->item(row, 3)->setText(status);
        table->item(row, 4)->setText(content.isEmpty() ? QString() : content.toString(QUrl::FullyEncoded));
        table->scrollToItem(table->item(row, 0));
        updateActions();
    });
    connect(controller, &BookDownloadController::progressChanged, this, [this](int completed, int total) {
        if (controller->isAuthenticating()) {
            progress->setRange(0, 0);
            progress->setFormat(QStringLiteral("正在获取会话…"));
        } else if (controller->isSearching()) {
            progress->setRange(0, 0);
            progress->setFormat(QStringLiteral("正在读取书籍列表…"));
        } else if (controller->isPreviewing()) {
            progress->setRange(0, qMax(1, total));
            progress->setValue(completed);
            progress->setFormat(QStringLiteral("预览：%1 / %2 章").arg(completed).arg(total));
        } else {
            progress->setRange(0, qMax(1, total));
            progress->setValue(completed);
            progress->setFormat(QStringLiteral("%1 / %2 条目录记录").arg(completed).arg(total));
        }
    });
    connect(controller, &BookDownloadController::downloadProgress, this, [this](qint64 received, qint64 total) {
        fileProgress->setText(QStringLiteral("当前章节：%1 KiB%2")
                                  .arg(received / 1024)
                                  .arg(total > 0 ? QStringLiteral(" / %1 KiB").arg(total / 1024) : QString()));
    });
    connect(table, &QTableWidget::itemSelectionChanged, this, &BookDownloadWidget::updateActions);
    connect(copyButton, &QPushButton::clicked, this, [this] {
        const auto row = table->currentRow();
        if (row >= 0 && !table->item(row, 4)->text().isEmpty())
            QApplication::clipboard()->setText(table->item(row, 4)->text());
    });
    updateCategories();
    showSearchResults();
}

bool BookDownloadWidget::hasSessionSource() const
{
    return controller->hasSession() || (!controller->hasDeviceIdentity()
        && QFileInfo(capturePath->text().trimmed()).isFile());
}

void BookDownloadWidget::updateSessionStatus()
{
    sessionLabel->setText(controller->sessionDescription());
    updateActions();
}

bool BookDownloadWidget::confirmDeviceIdentity()
{
    if (controller->hasDeviceIdentity())
        return true;
    return QMessageBox::warning(this, QStringLiteral("建立本机独立身份"),
        QStringLiteral("当前没有设备身份。是否明确建立一个固定的本机独立身份用于正常会话接口？\n"
                       "它不是手机上的原账号，可能建立独立游客账户；不会继承手机账号的收藏或权益。\n"
                       "若要沿用手机身份，请取消并先导入一次自己的请求抓包。失败后不会自动更换身份。"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
}

void BookDownloadWidget::refreshDeviceSession()
{
    const bool newIdentity = !controller->hasDeviceIdentity();
    if (!confirmDeviceIdentity())
        return;
    if (controller->isAccountSession()
        && QMessageBox::warning(this, QStringLiteral("确认设备会话刷新"),
            QStringLiteral("当前会话来自账号登录或抓包，设备接口返回的身份可能不同。\n"
                           "如需保持同一账号，请取消并使用账号登录。是否主动获取设备会话？"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
    if (!confirmNetwork())
        return;
    loadAll = false;
    controller->acquireDeviceSession(newIdentity);
}

void BookDownloadWidget::updateCategories()
{
    const QSignalBlocker blocker(categoryCombo);
    categoryCombo->clear();
    for (const auto &category : controller->categories())
        categoryCombo->addItem(QStringLiteral("%1 · %2").arg(category.name, category.tagId), category.key());
    categoryCombo->setCurrentIndex(categoryCombo->findData(controller->currentCategory().key()));
    updateActions();
}

void BookDownloadWidget::renameCurrentCategory()
{
    if (controller->isBusy() || categoryCombo->currentIndex() < 0)
        return;
    const auto category = controller->currentCategory();
    QInputDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("重命名小说分类"));
    dialog.setLabelText(QStringLiteral("请输入分类名称（最多 128 字符）："));
    dialog.setTextValue(category.name);
    if (auto *edit = dialog.findChild<QLineEdit *>())
        edit->setMaxLength(128);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const auto error = controller->renameCategory(category.key(), dialog.textValue());
    statusLabel->setText(error.isEmpty()
        ? QStringLiteral("分类名称已保存为“%1”；原书籍列表和分页缓存保留，未联网。")
              .arg(controller->currentCategory().name)
        : error);
}

void BookDownloadWidget::removeCurrentCategory()
{
    if (controller->isBusy() || categoryCombo->currentIndex() < 0)
        return;
    const auto category = controller->currentCategory();
    QMessageBox dialog(QMessageBox::Question, QStringLiteral("删除小说分类"),
                       QStringLiteral("确定删除分类“%1”（%2）？\n\n仅从分类列表移除，保留书籍列表缓存和已下载 TXT。\n以后重新添加相同分类时可以复用缓存。")
                           .arg(category.name, category.tagId),
                       QMessageBox::Yes | QMessageBox::No, this);
    dialog.setTextFormat(Qt::PlainText);
    dialog.setDefaultButton(QMessageBox::No);
    dialog.setEscapeButton(QMessageBox::No);
    if (dialog.exec() != QMessageBox::Yes)
        return;
    const auto error = controller->removeCategory(category.key());
    if (!error.isEmpty()) {
        statusLabel->setText(error);
        return;
    }
    loadAll = false;
    resultFilter->clear();
    statusLabel->setText(QStringLiteral("已移除分类“%1”，书籍列表缓存和已下载 TXT 保留。\n已选择分类“%2”，未自动联网。")
                             .arg(category.name, controller->currentCategory().name));
}

void BookDownloadWidget::showMuMuCapture(bool categoryOnly)
{
    if (controller->isBusy())
        return;
    loadAll = false;
    MuMuCaptureDialog dialog(this, categoryOnly);
    connect(&dialog, &MuMuCaptureDialog::busyChanged, this, [this](bool busy) {
        emit busyChanged(busy);
    });
    connect(&dialog, &MuMuCaptureDialog::requestReady, this, [this, &dialog, categoryOnly](const QByteArray &request) {
        if (categoryOnly) {
            NovelCategory category;
            const auto error = NovelApi::categoryFromRequest(request, category);
            if (!error.isEmpty()) {
                dialog.setResultMessage(error);
                return;
            }
            const auto &categories = controller->categories();
            const auto existing = std::find_if(categories.cbegin(), categories.cend(), [&category](const NovelCategory &item) {
                return item.key() == category.key();
            });
            if (existing != categories.cend()) {
                dialog.setResultMessage(QStringLiteral("该筛选条件已存在于分类“%1”，未重复添加，也未替换原会话。")
                                            .arg(existing->name));
                return;
            }
            QInputDialog naming(&dialog);
            naming.setWindowTitle(QStringLiteral("确认添加小说分类"));
            naming.setLabelText(QStringLiteral("已捕获分类条件：\n%1\n请填写分类名称（最多 128 字符）：")
                                    .arg(QString::fromUtf8(QJsonDocument(category.parameters()).toJson(QJsonDocument::Indented))));
            naming.setTextValue(QStringLiteral("分类 %1").arg(category.tagId));
            if (auto *edit = naming.findChild<QLineEdit *>())
                edit->setMaxLength(128);
            if (naming.exec() != QDialog::Accepted || naming.textValue().trimmed().isEmpty()) {
                dialog.setResultMessage(QStringLiteral("已取消添加分类，原分类和会话保留。"));
                return;
            }
            category.name = naming.textValue().trimmed();
            const auto added = controller->addCategory(category);
            if (!added.isEmpty()) {
                dialog.setResultMessage(added);
                return;
            }
            networkConfirmed = false;
            const auto imported = controller->importCapturedRequest(request);
            dialog.setResultMessage(imported.isEmpty()
                ? QStringLiteral("已保存并选中分类“%1”。%2\n关闭后点击“浏览当前分类”加载书籍，未自动联网。")
                      .arg(category.name, controller->sessionDescription())
                : QStringLiteral("分类“%1”已保存，但会话导入失败，原会话保留：%2").arg(category.name, imported));
            return;
        }
        networkConfirmed = false;
        const auto error = controller->importCapturedRequest(request);
        dialog.setImportResult(error, controller->sessionDescription());
    });
    dialog.exec();
    updateSessionStatus();
}

void BookDownloadWidget::showAccountLogin()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("正常账号登录"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *notice = new QLabel(QStringLiteral(
        "仅登录你自己的账号，不自动注册。原服务使用 HTTP，Body 加密不等同于 HTTPS；\n"
        "密码仅用于本次请求，不保存。若服务器要求额外验证，请在 App 中正常完成后导入。"), &dialog);
    notice->setWordWrap(true);
    layout->addWidget(notice);
    auto *form = new QFormLayout;
    QLineEdit account(&dialog);
    QLineEdit password(&dialog);
    account.setObjectName(QStringLiteral("accountLoginNameEdit"));
    password.setObjectName(QStringLiteral("accountLoginPasswordEdit"));
    account.setMaxLength(256);
    password.setMaxLength(256);
    password.setEchoMode(QLineEdit::Password);
    form->addRow(QStringLiteral("账号"), &account);
    form->addRow(QStringLiteral("密码"), &password);
    layout->addLayout(form);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("登录"));
    layout->addWidget(buttons);
    auto updateLogin = [&] {
        buttons->button(QDialogButtonBox::Ok)->setEnabled(!account.text().trimmed().isEmpty() && !password.text().isEmpty());
    };
    connect(&account, &QLineEdit::textChanged, &dialog, updateLogin);
    connect(&password, &QLineEdit::textChanged, &dialog, updateLogin);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    updateLogin();
    if (dialog.exec() != QDialog::Accepted) {
        password.clear();
        return;
    }
    const bool newIdentity = !controller->hasDeviceIdentity();
    if (!confirmDeviceIdentity() || !confirmNetwork()) {
        password.clear();
        return;
    }
    auto secret = password.text();
    password.clear();
    loadAll = false;
    controller->loginAccount(account.text().trimmed(), secret, newIdentity);
    secret.fill(QChar::Null);
    secret.clear();
}

void BookDownloadWidget::invalidateSession()
{
    networkConfirmed = false;
    loadAll = false;
    controller->invalidate(capturePath->text().trimmed());
    updateActions();
}

bool BookDownloadWidget::confirmNetwork()
{
    if (networkConfirmed)
        return true;
    networkConfirmed = QMessageBox::warning(
        this, QStringLiteral("确认发送会话凭证"),
        QStringLiteral("获取会话、账号登录、搜索和章节查询会将设备身份及必要凭证随加密 Body 发送到：\n"
                       "http://yj.kkj234ls.com\n\n"
                       "原接口使用 HTTP，不具备 HTTPS 的传输保护；Body 加密不能替代 TLS。\n"
                       "正文从 vgo.pkgplud.cn 的 HTTPS 地址下载，密钥不作为独立参数上传。\n"
                       "请仅使用你自己的有效会话和有权保存的内容。是否继续？"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
    return networkConfirmed;
}

void BookDownloadWidget::showBook(const NovelBook &book)
{
    table->setRowCount(static_cast<int>(book.chapters.size()));
    for (int row = 0; row < book.chapters.size(); ++row) {
        const auto &chapter = book.chapters[row];
        const QStringList values{QString::number(row + 1), chapter.name, chapter.id, chapter.status, QString()};
        for (int column = 0; column < values.size(); ++column)
            table->setItem(row, column, new QTableWidgetItem(values[column]));
    }
    bookLabel->setText(book.chapters.isEmpty()
                           ? QStringLiteral("尚未获取书籍目录。")
                           : QStringLiteral("《%1》 · %2 条目录记录 · ID：%3")
                                 .arg(book.name).arg(book.chapters.size()).arg(book.id));
    updateActions();
}

void BookDownloadWidget::showSearchResults()
{
    pageSucceeded = controller->searchPage() > 0;
    const QSignalBlocker blocker(searchTable);
    const QSignalBlocker scrollBlocker(searchTable->verticalScrollBar());
    searchTable->setSortingEnabled(false);
    const auto &books = controller->searchResults();
    const auto selectedId = bookId->text().trimmed();
    searchTable->clearContents();
    searchTable->setRowCount(static_cast<int>(books.size()));
    searchTable->clearSelection();
    for (int row = 0; row < books.size(); ++row) {
        const auto &book = books[row];
        const QStringList values{book.name, book.author.isEmpty() ? QStringLiteral("未提供") : book.author,
                                 book.chapterCount, book.status, book.id};
        for (int column = 0; column < values.size(); ++column) {
            QTableWidgetItem *item = column == 2
                ? new ChapterCountItem(values[column])
                : new QTableWidgetItem(values[column]);
            searchTable->setItem(row, column, item);
        }
        if (book.id.compare(selectedId, Qt::CaseInsensitive) == 0)
            searchTable->selectRow(row);
    }
    searchTable->setSortingEnabled(true);
    applyResultFilter();
    if (controller->searchPage() == 0)
        searchLabel->setText(QStringLiteral("搜索结果：单击书籍自动填入 ID，不会立即下载。"));
    else
        searchLabel->setText(QStringLiteral("%1 · %2 本 · 读取至第 %3 页 · 更新 %4\n%5")
                                 .arg(controller->isGroupList() ? QStringLiteral("分类“%1”缓存").arg(controller->currentCategory().name)
                                                                : QStringLiteral("全站搜索（未限定分类）"))
                                 .arg(books.size()).arg(controller->searchPage())
                                 .arg(controller->listUpdatedAt().toLocalTime().toString(QStringLiteral("MM-dd HH:mm")))
                                 .arg(controller->searchComplete() ? QStringLiteral("已加载全部返回结果，直接排序无需重新请求。")
                                      : controller->canLoadMoreSearch()
                                          ? QStringLiteral("未加载完整；章节排序将从下一页继续加载。")
                                          : QStringLiteral("已停止分页但未确认完整；当前只排序已加载书籍。")));
    updateActions();
}

void BookDownloadWidget::searchGroupBooks()
{
    const bool hasCache = controller->hasGroupCache();
    if (!hasCache && !hasSessionSource())
        return;
    if (!hasCache && !confirmNetwork())
        return;
    loadAll = false;
    controller->browseGroup(capturePath->text().trimmed());
    resultFilter->setText(keywords->text().trimmed());
    if (!controller->isBusy() && controller->canLoadMoreSearch()
        && hasSessionSource() && confirmNetwork()) {
        loadAll = true;
        controller->loadMoreSearch();
    } else if (controller->isBusy() && controller->isSearching()) {
        loadAll = true;
    }
    updateActions();
}

void BookDownloadWidget::applyResultFilter()
{
    const QSignalBlocker blocker(searchTable->verticalScrollBar());
    const auto text = resultFilter->text();
    for (int row = 0; row < searchTable->rowCount(); ++row)
        searchTable->setRowHidden(row, !searchTable->item(row, 0)->text().contains(text, Qt::CaseInsensitive)
            && !searchTable->item(row, 1)->text().contains(text, Qt::CaseInsensitive));
}

void BookDownloadWidget::selectSearchBook()
{
    if (controller->isBusy() || searchTable->selectedItems().isEmpty())
        return;
    const auto row = searchTable->currentRow();
    const auto &books = controller->searchResults();
    if (row < 0 || row >= books.size())
        return;
    const auto id = searchTable->item(row, 4)->text();
    const auto found = std::find_if(books.cbegin(), books.cend(), [&id](const NovelSearchBook &book) {
        return book.id == id;
    });
    if (found == books.cend())
        return;
    const auto &book = *found;
    bookId->setText(book.id);
    statusLabel->setText(QStringLiteral("已选择《%1》，ID 已填入。请选择保存目录，然后点击“下载并合并”。")
                             .arg(book.name));
    updateActions();
}

void BookDownloadWidget::showBookDetails()
{
    if (controller->isBusy() || searchTable->selectedItems().isEmpty())
        return;
    const auto row = searchTable->currentRow();
    if (row < 0 || !searchTable->item(row, 4))
        return;
    const auto id = searchTable->item(row, 4)->text();
    const auto &books = controller->searchResults();
    const auto found = std::find_if(books.cbegin(), books.cend(), [&id](const NovelSearchBook &book) {
        return book.id == id;
    });
    if (found == books.cend())
        return;
    loadAll = false;
    const NovelSearchBook selected = *found;
    BookDetailDialog dialog(selected, this);
    dialog.setPreviewAvailable(hasSessionSource());
    connect(controller, &BookDownloadController::previewCatalogReady, &dialog, &BookDetailDialog::setCatalog);
    connect(controller, &BookDownloadController::previewChapterReady, &dialog, &BookDetailDialog::setChapter);
    connect(controller, &BookDownloadController::previewFinished, &dialog, &BookDetailDialog::finishPreview);
    connect(controller, &BookDownloadController::statusChanged, &dialog, &BookDetailDialog::setMessage);
    connect(&dialog, &BookDetailDialog::previewRequested, this, [this, &dialog, id] {
        if (confirmNetwork())
            controller->previewBook(id, capturePath->text().trimmed());
        else
            dialog.finishPreview(QStringLiteral("未授权联网，未获取正文。"));
    });
    connect(&dialog, &BookDetailDialog::cancelRequested, controller, &BookDownloadController::cancel);
    connect(&dialog, &QDialog::finished, this, [this] {
        if (controller->isPreviewing())
            controller->cancel();
    });
    dialog.exec();
}

void BookDownloadWidget::updateActions()
{
    const bool busy = controller->isBusy();
    const bool hasBook = !controller->book().chapters.isEmpty();
    const bool hasSession = hasSessionSource();
    importSessionButton->setEnabled(!busy && QFileInfo(capturePath->text().trimmed()).isFile());
    deviceSessionButton->setEnabled(!busy);
    accountLoginButton->setEnabled(!busy);
    captureSessionButton->setEnabled(!busy);
    categoryCombo->setEnabled(!busy);
    captureCategoryButton->setEnabled(!busy);
    const bool hasCategory = categoryCombo->currentIndex() >= 0;
    renameCategoryButton->setEnabled(!busy && hasCategory);
    deleteCategoryButton->setEnabled(!busy && hasCategory && controller->categories().size() > 1);
    deleteCategoryButton->setToolTip(controller->categories().size() > 1
        ? QStringLiteral("仅移除分类，保留书籍列表缓存和已下载 TXT。")
        : QStringLiteral("至少保留一个小说分类；添加其他分类后可删除。"));
    forgetSessionButton->setEnabled(!busy && controller->hasDeviceIdentity());
    const bool validInput = NovelApi::isId(bookId->text().trimmed()) && hasSession;
    const bool validKeywords = NovelApi::isSearchKeywords(keywords->text().trimmed());
    searchButton->setEnabled(!busy && (hasSession || controller->hasGroupCache()) && validKeywords);
    globalSearchButton->setEnabled(!busy && hasSession && validKeywords);
    groupButton->setEnabled(!busy && (hasSession || controller->hasGroupCache()));
    refreshGroupButton->setEnabled(!busy && hasSession);
    allButton->setText(loadAll ? QStringLiteral("停止加载") : QStringLiteral("加载全部"));
    allButton->setEnabled(loadAll || (!busy && hasSession && controller->canLoadMoreSearch()));
    resultFilter->setEnabled(!busy);
    detailButton->setEnabled(!busy && !searchTable->selectedItems().isEmpty());
    moreSearchButton->setEnabled(!busy && hasSession && controller->canLoadMoreSearch());
    catalogButton->setEnabled(!busy && validInput);
    linksButton->setEnabled(!busy && hasBook);
    downloadButton->setEnabled(!busy && validInput && QFileInfo(outputDirectory->text().trimmed()).isDir());
    cancelButton->setEnabled(busy);
    const auto row = table->currentRow();
    copyButton->setEnabled(row >= 0 && table->item(row, 4) && !table->item(row, 4)->text().isEmpty());
}
