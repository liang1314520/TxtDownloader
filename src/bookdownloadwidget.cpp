#include "bookdownloadwidget.h"
#include "bookdownloadcontroller.h"
#include "bookdetaildialog.h"
#include "mumucapturedialog.h"
#include "workbenchtheme.h"

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
#include <QFrame>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QSignalBlocker>
#include <QSet>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QStackedWidget>
#include <QTextDocument>
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
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    pages = new QStackedWidget(this);
    pages->setObjectName(QStringLiteral("bookWorkspaceStack"));
    buildLibraryPage();
    buildDownloadPage();
    buildSessionPage();
    buildFavoritesPage();
    layout->addWidget(pages, 1);

    auto *taskBar = new QFrame(this);
    taskBar->setObjectName(QStringLiteral("workbenchTaskBar"));
    auto *taskLayout = new QVBoxLayout(taskBar);
    taskLayout->setContentsMargins(24, 12, 24, 12);
    taskLayout->setSpacing(8);
    auto *taskRow = new QHBoxLayout;
    statusLabel = WorkbenchTheme::label(QStringLiteral("就绪。缓存可离线浏览；连接配置在“会话与连接”中管理。"), "status", taskBar);
    statusLabel->setObjectName(QStringLiteral("bookTaskStatusLabel"));
    statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    cancelButton = new QPushButton(QStringLiteral("取消当前任务"), taskBar);
    cancelButton->setObjectName(QStringLiteral("cancelBookTaskButton"));
    taskRow->addWidget(statusLabel, 1);
    taskRow->addWidget(cancelButton, 0, Qt::AlignTop);
    taskLayout->addLayout(taskRow);
    progress = new QProgressBar(taskBar);
    progress->setRange(0, 1);
    progress->setValue(0);
    progress->setFormat(QStringLiteral("尚无进行中的任务"));
    taskLayout->addWidget(progress);
    fileProgress = WorkbenchTheme::label({}, "muted", taskBar);
    taskLayout->addWidget(fileProgress);
    layout->addWidget(taskBar);

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

    connect(captureFileButton, &QPushButton::clicked, this, [this] {
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
    connect(reorderCategoriesButton, &QPushButton::clicked, this, &BookDownloadWidget::reorderCurrentCategories);
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
    connect(searchTable, &QTableWidget::itemClicked, this, [this](QTableWidgetItem *item) {
        if (item->column() != 5)
            selectSearchBook();
    });
    connect(searchTable, &QTableWidget::itemDoubleClicked, this, [this](QTableWidgetItem *item) {
        if (item->column() != 5)
            showBookDetails();
    });
    connect(detailButton, &QPushButton::clicked, this, &BookDownloadWidget::showBookDetails);
    connect(controller, &BookDownloadController::searchResultsChanged, this, &BookDownloadWidget::showSearchResults);
    connect(searchTable, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (item->column() != 5)
            return;
        const auto id = item->data(Qt::UserRole).toString();
        const auto &books = controller->searchResults();
        const auto found = std::find_if(books.cbegin(), books.cend(), [&id](const NovelSearchBook &book) {
            return book.id == id;
        });
        if (found != books.cend()) {
            const NovelSearchBook selected = *found;
            changeFavorite(selected, item->checkState() == Qt::Checked);
        }
    });
    connect(controller, &BookDownloadController::favoritesChanged, this, [this] {
        updateFavoriteMarkers();
        showFavorites();
    });
    connect(favoritesFilter, &QLineEdit::textChanged, this, &BookDownloadWidget::applyFavoritesFilter);
    connect(favoritesTable, &QTableWidget::itemSelectionChanged, this, &BookDownloadWidget::updateActions);
    connect(favoriteDetailButton, &QPushButton::clicked, this, [this] {
        if (const auto *book = selectedFavoriteBook()) {
            const NovelSearchBook selected = *book;
            showBookDetailsFor(selected);
        }
    });
    connect(favoritesTable, &QTableWidget::itemDoubleClicked, favoriteDetailButton, &QPushButton::click);
    connect(removeFavoriteButton, &QPushButton::clicked, this, [this] {
        if (const auto *book = selectedFavoriteBook()) {
            const NovelSearchBook selected = *book;
            changeFavorite(selected, false);
        }
    });
    connect(favoriteDownloadButton, &QPushButton::clicked, this, [this] {
        const auto *book = selectedFavoriteBook();
        if (controller->isBusy() || !book)
            return;
        const auto id = book->id;
        bookId->setText(id);
        emit downloadPageRequested();
    });
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
        downloadSettings->setEnabled(!busy);
        sessionSettings->setEnabled(!busy);
        keywords->setEnabled(!busy);
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
            completedMetric->setText(total > 0 ? QStringLiteral("%1 / %2").arg(completed).arg(total) : QStringLiteral("—"));
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
    showFavorites();
}

void BookDownloadWidget::setPage(Page page)
{
    pages->setCurrentIndex(static_cast<int>(page));
}

void BookDownloadWidget::buildLibraryPage()
{
    auto *page = new QWidget(pages);
    page->setProperty("pageSurface", true);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 24, 24, 20);
    layout->setSpacing(16);
    layout->addWidget(WorkbenchTheme::label(QStringLiteral("YOUR NEXT STORY / 小说书库"), "eyebrow", page));
    auto *header = new QHBoxLayout;
    auto *heading = new QVBoxLayout;
    heading->addWidget(WorkbenchTheme::label(QStringLiteral("发现下一段好故事"), "pageTitle", page));
    heading->addWidget(WorkbenchTheme::label(QStringLiteral("浏览分类、查找书籍，先读几页，再决定是否保存。"), "muted", page));
    header->addLayout(heading, 1);
    captureCategoryButton = new QPushButton(QStringLiteral("采集添加分类…"), page);
    captureCategoryButton->setObjectName(QStringLiteral("captureCategoryButton"));
    header->addWidget(captureCategoryButton, 0, Qt::AlignTop);
    layout->addLayout(header);

    auto *splitter = new QSplitter(Qt::Horizontal, page);
    splitter->setObjectName(QStringLiteral("libraryDetailSplitter"));
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(16);
    auto *main = new QWidget(splitter);
    auto *mainLayout = new QVBoxLayout(main);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(12);
    auto *filters = WorkbenchTheme::panel(main);
    auto *filterLayout = new QVBoxLayout(filters);
    filterLayout->setContentsMargins(16, 14, 16, 14);
    filterLayout->setSpacing(10);
    auto *categoryRow = new QHBoxLayout;
    categoryCombo = new QComboBox(filters);
    categoryCombo->setObjectName(QStringLiteral("bookCategoryCombo"));
    categoryCombo->setMinimumWidth(120);
    categoryCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    categoryCombo->setMinimumContentsLength(10);
    renameCategoryButton = new QPushButton(QStringLiteral("重命名…"), filters);
    renameCategoryButton->setObjectName(QStringLiteral("renameCategoryButton"));
    deleteCategoryButton = new QPushButton(QStringLiteral("删除…"), filters);
    deleteCategoryButton->setObjectName(QStringLiteral("deleteCategoryButton"));
    deleteCategoryButton->setProperty("danger", true);
    reorderCategoriesButton = new QPushButton(QStringLiteral("排序…"), filters);
    reorderCategoriesButton->setObjectName(QStringLiteral("reorderCategoriesButton"));
    categoryRow->addWidget(categoryCombo, 1);
    categoryRow->addWidget(reorderCategoriesButton);
    categoryRow->addWidget(renameCategoryButton);
    categoryRow->addWidget(deleteCategoryButton);
    filterLayout->addLayout(categoryRow);
    auto *searchRow = new QHBoxLayout;
    keywords = new QLineEdit(filters);
    keywords->setObjectName(QStringLiteral("bookSearchKeywordsEdit"));
    keywords->setMaxLength(256);
    keywords->setPlaceholderText(QStringLiteral("书名 / 作者，默认在分类内查找"));
    keywords->setClearButtonEnabled(true);
    searchButton = new QPushButton(QStringLiteral("分类内查找"), filters);
    searchButton->setObjectName(QStringLiteral("searchBooksButton"));
    searchButton->setAutoDefault(false);
    searchButton->setProperty("primary", true);
    globalSearchButton = new QPushButton(QStringLiteral("全站搜索"), filters);
    globalSearchButton->setObjectName(QStringLiteral("globalBookSearchButton"));
    globalSearchButton->setToolTip(QStringLiteral("服务器关键词搜索，不限定分类；结果不代表属于当前分类。"));
    globalSearchButton->setAutoDefault(false);
    searchRow->addWidget(keywords, 1);
    searchRow->addWidget(searchButton);
    searchRow->addWidget(globalSearchButton);
    filterLayout->addLayout(searchRow);
    mainLayout->addWidget(filters);

    auto *listPanel = WorkbenchTheme::panel(main);
    auto *listLayout = new QVBoxLayout(listPanel);
    listLayout->setContentsMargins(12, 12, 12, 12);
    listLayout->setSpacing(10);
    auto *listActions = new QHBoxLayout;
    groupButton = new QPushButton(QStringLiteral("浏览分类"), listPanel);
    groupButton->setObjectName(QStringLiteral("browseGroupButton"));
    refreshGroupButton = new QPushButton(QStringLiteral("刷新"), listPanel);
    refreshGroupButton->setObjectName(QStringLiteral("refreshGroupButton"));
    allButton = new QPushButton(QStringLiteral("加载全部"), listPanel);
    moreSearchButton = new QPushButton(QStringLiteral("加载更多"), listPanel);
    moreSearchButton->setObjectName(QStringLiteral("loadMoreBooksButton"));
    listActions->addWidget(groupButton);
    listActions->addWidget(refreshGroupButton);
    listActions->addStretch();
    listActions->addWidget(allButton);
    listActions->addWidget(moreSearchButton);
    listLayout->addLayout(listActions);
    searchTable = new QTableWidget(0, 6, listPanel);
    searchTable->setObjectName(QStringLiteral("bookSearchResultsTable"));
    searchTable->setHorizontalHeaderLabels({QStringLiteral("书名"), QStringLiteral("作者"),
        QStringLiteral("章节数"), QStringLiteral("状态"), QStringLiteral("书籍 ID"), QStringLiteral("收藏")});
    searchTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    searchTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    searchTable->setSelectionMode(QAbstractItemView::SingleSelection);
    searchTable->setWordWrap(false);
    searchTable->setTextElideMode(Qt::ElideRight);
    searchTable->setAlternatingRowColors(true);
    searchTable->setShowGrid(false);
    searchTable->verticalHeader()->hide();
    searchTable->verticalHeader()->setDefaultSectionSize(52);
    searchTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    searchTable->horizontalHeader()->setStretchLastSection(true);
    searchTable->setColumnWidth(0, 210);
    searchTable->setColumnWidth(1, 105);
    searchTable->setColumnWidth(2, 80);
    searchTable->setColumnWidth(3, 90);
    searchTable->setColumnWidth(4, 180);
    searchTable->setColumnWidth(5, 60);
    searchTable->horizontalHeader()->moveSection(5, 0);
    searchTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Fixed);
    searchTable->horizontalHeaderItem(5)->setToolTip(QStringLiteral("勾选加入本机收藏，取消勾选移出收藏；不联网。"));
    searchTable->setMinimumHeight(180);
    listLayout->addWidget(searchTable, 1);
    resultFilter = new QLineEdit(listPanel);
    resultFilter->setObjectName(QStringLiteral("bookResultFilterEdit"));
    resultFilter->setPlaceholderText(QStringLiteral("仅筛选已加载的列表：书名 / 作者"));
    resultFilter->setClearButtonEnabled(true);
    listLayout->addWidget(resultFilter);
    searchLabel = WorkbenchTheme::label({}, "muted", listPanel);
    searchLabel->setObjectName(QStringLiteral("bookSearchSummaryLabel"));
    listLayout->addWidget(searchLabel);
    mainLayout->addWidget(listPanel, 1);
    splitter->addWidget(main);

    auto *detail = WorkbenchTheme::panel(splitter);
    detail->setMinimumWidth(240);
    auto *detailLayout = new QVBoxLayout(detail);
    detailLayout->setContentsMargins(18, 18, 18, 18);
    detailLayout->setSpacing(12);
    auto *art = WorkbenchTheme::label({}, "bookArt", detail);
    art->setAlignment(Qt::AlignCenter);
    art->setPixmap(WorkbenchTheme::icon(WorkbenchTheme::Symbol::Book).pixmap(64, 64));
    detailLayout->addWidget(art);
    detailLayout->addWidget(WorkbenchTheme::label(QStringLiteral("SELECTED BOOK / 选中书籍"), "eyebrow", detail));
    selectedTitle = WorkbenchTheme::label(QStringLiteral("请选择书籍"), "sectionTitle", detail);
    selectedTitle->setObjectName(QStringLiteral("selectedBookTitleLabel"));
    selectedMetadata = WorkbenchTheme::label({}, "muted", detail);
    selectedId = WorkbenchTheme::label({}, "muted", detail);
    selectedId->setObjectName(QStringLiteral("selectedBookIdLabel"));
    selectedId->setTextInteractionFlags(Qt::TextSelectableByMouse);
    detailLayout->addWidget(selectedTitle);
    detailLayout->addWidget(selectedMetadata);
    detailLayout->addWidget(selectedId);
    selectedDescription = new QPlainTextEdit(detail);
    selectedDescription->setObjectName(QStringLiteral("bookSelectedDescriptionView"));
    selectedDescription->setReadOnly(true);
    selectedDescription->setPlaceholderText(QStringLiteral("单击列表中的书籍查看简介；选书不会联网。"));
    selectedDescription->setMinimumHeight(110);
    selectedDescription->document()->setDocumentMargin(4);
    detailLayout->addWidget(selectedDescription, 1);
    detailButton = new QPushButton(QStringLiteral("简介与正文预览"), detail);
    detailButton->setObjectName(QStringLiteral("bookDetailsButton"));
    detailButton->setProperty("primary", true);
    openDownloadButton = new QPushButton(QStringLiteral("前往整书下载"), detail);
    openDownloadButton->setObjectName(QStringLiteral("openBookDownloadButton"));
    detailLayout->addWidget(detailButton);
    detailLayout->addWidget(openDownloadButton);
    detailLayout->addWidget(WorkbenchTheme::label(QStringLiteral("预览仅在内存中；只处理当前会话免费且有权阅读的章节。"), "muted", detail));
    connect(openDownloadButton, &QPushButton::clicked, this, [this] {
        if (controller->isBusy() || !selectedSearchBook())
            return;
        selectSearchBook();
        emit downloadPageRequested();
    });
    splitter->addWidget(detail);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({680, 280});
    layout->addWidget(splitter, 1);
    auto *scroll = new QScrollArea(pages);
    scroll->setWidgetResizable(true);
    scroll->setWidget(page);
    pages->addWidget(scroll);
}

void BookDownloadWidget::buildDownloadPage()
{
    auto *page = new QWidget(pages);
    page->setProperty("pageSurface", true);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 24, 24, 20);
    layout->setSpacing(16);
    layout->addWidget(WorkbenchTheme::label(QStringLiteral("KEEP THE STORY / 整书下载"), "eyebrow", page));
    layout->addWidget(WorkbenchTheme::label(QStringLiteral("保存一个完整的故事"), "pageTitle", page));
    layout->addWidget(WorkbenchTheme::label(QStringLiteral("查询目录、逐章下载、合并 TXT。所有操作由你主动开始。"), "muted", page));
    auto *metrics = new QHBoxLayout;
    const QStringList metricTitles{QStringLiteral("目录记录"), QStringLiteral("任务进度"), QStringLiteral("合并输出")};
    for (int index = 0; index < metricTitles.size(); ++index) {
        auto *card = WorkbenchTheme::panel(page);
        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(18, 14, 18, 14);
        cardLayout->addWidget(WorkbenchTheme::label(metricTitles[index], "muted", card));
        auto *value = WorkbenchTheme::label(index == 2 ? QStringLiteral("UTF-8 TXT") : QStringLiteral("—"), "metric", card);
        cardLayout->addWidget(value);
        if (index == 0)
            catalogMetric = value;
        else if (index == 1)
            completedMetric = value;
        metrics->addWidget(card, 1);
    }
    layout->addLayout(metrics);
    auto *splitter = new QSplitter(Qt::Horizontal, page);
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(16);
    auto *catalog = WorkbenchTheme::panel(splitter);
    auto *catalogLayout = new QVBoxLayout(catalog);
    catalogLayout->setContentsMargins(16, 16, 16, 16);
    catalogLayout->setSpacing(12);
    catalogLayout->addWidget(WorkbenchTheme::label(QStringLiteral("章节目录"), "sectionTitle", catalog));
    bookLabel = WorkbenchTheme::label(QStringLiteral("尚未获取书籍目录。请从书库选书，或在右侧输入书籍 ID。"), "muted", catalog);
    catalogLayout->addWidget(bookLabel);
    table = new QTableWidget(0, 5, catalog);
    table->setObjectName(QStringLiteral("bookChaptersTable"));
    table->setHorizontalHeaderLabels({QStringLiteral("顺序"), QStringLiteral("章节"), QStringLiteral("章节 ID"),
        QStringLiteral("状态"), QStringLiteral("正文链接（含时效签名）")});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setWordWrap(false);
    table->setTextElideMode(Qt::ElideRight);
    table->setAlternatingRowColors(true);
    table->setShowGrid(false);
    table->verticalHeader()->hide();
    table->verticalHeader()->setDefaultSectionSize(44);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table->horizontalHeader()->setStretchLastSection(true);
    table->setColumnWidth(0, 60);
    table->setColumnWidth(1, 180);
    table->setColumnWidth(2, 210);
    table->setColumnWidth(3, 140);
    table->setMinimumHeight(180);
    catalogLayout->addWidget(table, 1);
    catalogLayout->addWidget(WorkbenchTheme::label(QStringLiteral("签名链接请勿公开分享；下载时会重新查询。"), "muted", catalog));
    copyButton = new QPushButton(QStringLiteral("复制选中链接"), catalog);
    catalogLayout->addWidget(copyButton, 0, Qt::AlignRight);
    splitter->addWidget(catalog);

    auto *optionsCard = WorkbenchTheme::panel(splitter);
    optionsCard->setMinimumWidth(240);
    auto *optionsLayout = new QVBoxLayout(optionsCard);
    optionsLayout->setContentsMargins(18, 18, 18, 18);
    optionsLayout->setSpacing(12);
    optionsLayout->addWidget(WorkbenchTheme::label(QStringLiteral("下载设置"), "sectionTitle", optionsCard));
    downloadSettings = new QWidget(optionsCard);
    auto *fields = new QVBoxLayout(downloadSettings);
    fields->setContentsMargins(0, 0, 0, 0);
    fields->setSpacing(10);
    fields->addWidget(WorkbenchTheme::label(QStringLiteral("书籍 ID"), "muted", downloadSettings));
    bookId = new QLineEdit(downloadSettings);
    bookId->setObjectName(QStringLiteral("bookIdEdit"));
    bookId->setMaxLength(32);
    bookId->setPlaceholderText(QStringLiteral("32 位书籍 ID，不是章节 ID"));
    bookId->setMinimumWidth(100);
    fields->addWidget(bookId);
    fields->addWidget(WorkbenchTheme::label(QStringLiteral("TXT 保存目录"), "muted", downloadSettings));
    outputDirectory = new QLineEdit(downloadSettings);
    outputDirectory->setObjectName(QStringLiteral("bookOutputDirectoryEdit"));
    outputDirectory->setText(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
    fields->addWidget(outputDirectory);
    directoryButton = new QPushButton(QStringLiteral("选择保存目录…"), downloadSettings);
    fields->addWidget(directoryButton);
    optionsLayout->addWidget(downloadSettings);
    optionsLayout->addWidget(WorkbenchTheme::label(QStringLiteral("仅保存有权阅读的免费章节。取消时保留已完成章节，完整下载后才发布合并 TXT。"), "muted", optionsCard));
    downloadButton = new QPushButton(QStringLiteral("下载并合并"), optionsCard);
    downloadButton->setProperty("primary", true);
    downloadButton->setAutoDefault(false);
    catalogButton = new QPushButton(QStringLiteral("仅获取目录"), optionsCard);
    linksButton = new QPushButton(QStringLiteral("仅获取全部链接"), optionsCard);
    optionsLayout->addWidget(downloadButton);
    optionsLayout->addWidget(catalogButton);
    optionsLayout->addWidget(linksButton);
    optionsLayout->addStretch();
    splitter->addWidget(optionsCard);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({680, 280});
    layout->addWidget(splitter, 1);
    auto *scroll = new QScrollArea(pages);
    scroll->setWidgetResizable(true);
    scroll->setWidget(page);
    pages->addWidget(scroll);
}

void BookDownloadWidget::buildSessionPage()
{
    auto *page = new QWidget(pages);
    page->setProperty("pageSurface", true);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 24, 24, 20);
    layout->setSpacing(16);
    layout->addWidget(WorkbenchTheme::label(QStringLiteral("CONNECTED, NOT COMPLICATED / 会话与连接"), "eyebrow", page));
    layout->addWidget(WorkbenchTheme::label(QStringLiteral("连接准备好，阅读更从容"), "pageTitle", page));
    layout->addWidget(WorkbenchTheme::label(QStringLiteral("会话、账号与本机采集集中管理。启动恢复已有配置，不自动联网。"), "muted", page));
    auto *state = WorkbenchTheme::panel(page);
    auto *stateLayout = new QVBoxLayout(state);
    stateLayout->setContentsMargins(20, 18, 20, 18);
    stateLayout->addWidget(WorkbenchTheme::label(QStringLiteral("本机会话"), "sectionTitle", state));
    sessionLabel = WorkbenchTheme::label(controller->sessionDescription(), "status", state);
    sessionLabel->setObjectName(QStringLiteral("sessionStateLabel"));
    sessionLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    stateLayout->addWidget(sessionLabel);
    layout->addWidget(state);

    sessionSettings = new QWidget(page);
    auto *cards = new QGridLayout(sessionSettings);
    cards->setContentsMargins(0, 0, 0, 0);
    cards->setSpacing(16);
    auto *normal = WorkbenchTheme::panel(sessionSettings);
    auto *normalLayout = new QVBoxLayout(normal);
    normalLayout->setContentsMargins(20, 20, 20, 20);
    normalLayout->setSpacing(12);
    normalLayout->addWidget(WorkbenchTheme::label(QStringLiteral("正常会话与登录"), "sectionTitle", normal));
    normalLayout->addWidget(WorkbenchTheme::label(QStringLiteral("使用固定设备身份获取会话，或登录你自己的账号。密码仅用于本次请求，不保存。"), "muted", normal));
    deviceSessionButton = new QPushButton(QStringLiteral("获取／刷新设备会话"), normal);
    deviceSessionButton->setObjectName(QStringLiteral("deviceSessionButton"));
    deviceSessionButton->setProperty("primary", true);
    accountLoginButton = new QPushButton(QStringLiteral("账号登录…"), normal);
    accountLoginButton->setObjectName(QStringLiteral("accountLoginButton"));
    forgetSessionButton = new QPushButton(QStringLiteral("清除会话"), normal);
    forgetSessionButton->setObjectName(QStringLiteral("forgetSessionButton"));
    forgetSessionButton->setProperty("danger", true);
    normalLayout->addWidget(deviceSessionButton);
    normalLayout->addWidget(accountLoginButton);
    normalLayout->addStretch();
    normalLayout->addWidget(forgetSessionButton, 0, Qt::AlignLeft);
    cards->addWidget(normal, 0, 0);

    auto *mumu = WorkbenchTheme::panel(sessionSettings);
    auto *mumuLayout = new QVBoxLayout(mumu);
    mumuLayout->setContentsMargins(20, 20, 20, 20);
    mumuLayout->setSpacing(12);
    mumuLayout->addWidget(WorkbenchTheme::label(QStringLiteral("MuMu 本机采集"), "sectionTitle", mumu));
    mumuLayout->addWidget(WorkbenchTheme::label(QStringLiteral("检测本机工具与实例。你确认并授权后才开始记录；结束时关闭记录并恢复原设置。"), "muted", mumu));
    captureSessionButton = new QPushButton(QStringLiteral("打开 MuMu 采集工作台…"), mumu);
    captureSessionButton->setObjectName(QStringLiteral("captureSessionButton"));
    mumuLayout->addWidget(captureSessionButton);
    mumuLayout->addWidget(WorkbenchTheme::label(QStringLiteral("采集涉及所选实例的整个网卡，可能包含其他应用数据。每次开始前都需要明确授权。"), "notice", mumu));
    mumuLayout->addStretch();
    cards->addWidget(mumu, 0, 1);
    auto *manual = WorkbenchTheme::panel(sessionSettings);
    auto *manualLayout = new QVBoxLayout(manual);
    manualLayout->setContentsMargins(20, 16, 20, 16);
    manualLayout->setSpacing(10);
    manualLayout->addWidget(WorkbenchTheme::label(QStringLiteral("导入自己的请求文件"), "sectionTitle", manual));
    capturePath = new QLineEdit(manual);
    capturePath->setObjectName(QStringLiteral("sessionCaptureEdit"));
    capturePath->setPlaceholderText(QStringLiteral("选择自己的 http_req_*.hcy，导入后可受保护复用"));
    auto *captureRow = new QHBoxLayout;
    captureFileButton = new QPushButton(QStringLiteral("选择请求文件…"), manual);
    importSessionButton = new QPushButton(QStringLiteral("导入并记住"), manual);
    importSessionButton->setObjectName(QStringLiteral("importSessionButton"));
    captureRow->addWidget(capturePath, 1);
    captureRow->addWidget(captureFileButton);
    captureRow->addWidget(importSessionButton);
    manualLayout->addLayout(captureRow);
    cards->addWidget(manual, 1, 0, 1, 2);
    cards->setColumnStretch(0, 1);
    cards->setColumnStretch(1, 1);
    layout->addWidget(sessionSettings);
    layout->addWidget(WorkbenchTheme::label(QStringLiteral("连接与隐私边界\n原服务使用 HTTP，Body 加密不等同于 HTTPS。会话凭证受当前 Windows 用户 DPAPI 保护；不保存密码，不绕过账号或阅读权限。"), "notice", page));
    layout->addStretch();
    auto *scroll = new QScrollArea(pages);
    scroll->setWidgetResizable(true);
    scroll->setWidget(page);
    pages->addWidget(scroll);
}

void BookDownloadWidget::buildFavoritesPage()
{
    auto *page = new QWidget(pages);
    page->setProperty("pageSurface", true);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 24, 24, 20);
    layout->setSpacing(16);
    layout->addWidget(WorkbenchTheme::label(QStringLiteral("SAVED FOR LATER / 我的收藏"), "eyebrow", page));
    layout->addWidget(WorkbenchTheme::label(QStringLiteral("把喜欢的故事，留在这里"), "pageTitle", page));
    layout->addWidget(WorkbenchTheme::label(QStringLiteral("本机统一收藏，最近收藏在前。离线可看简介，切换或清除会话不会移除收藏。"), "muted", page));
    auto *splitter = new QSplitter(Qt::Horizontal, page);
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(16);
    auto *list = WorkbenchTheme::panel(splitter);
    auto *listLayout = new QVBoxLayout(list);
    listLayout->setContentsMargins(16, 16, 16, 16);
    listLayout->setSpacing(12);
    favoritesFilter = new QLineEdit(list);
    favoritesFilter->setObjectName(QStringLiteral("favoritesFilterEdit"));
    favoritesFilter->setPlaceholderText(QStringLiteral("在收藏中查找书名 / 作者，不联网"));
    favoritesFilter->setClearButtonEnabled(true);
    listLayout->addWidget(favoritesFilter);
    favoritesTable = new QTableWidget(0, 5, list);
    favoritesTable->setObjectName(QStringLiteral("bookFavoritesTable"));
    favoritesTable->setHorizontalHeaderLabels({QStringLiteral("书名"), QStringLiteral("作者"),
        QStringLiteral("章节数"), QStringLiteral("状态"), QStringLiteral("书籍 ID")});
    favoritesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    favoritesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    favoritesTable->setSelectionMode(QAbstractItemView::SingleSelection);
    favoritesTable->setWordWrap(false);
    favoritesTable->setAlternatingRowColors(true);
    favoritesTable->setShowGrid(false);
    favoritesTable->verticalHeader()->hide();
    favoritesTable->verticalHeader()->setDefaultSectionSize(52);
    favoritesTable->horizontalHeader()->setStretchLastSection(true);
    favoritesTable->setColumnWidth(0, 210);
    favoritesTable->setColumnWidth(1, 105);
    favoritesTable->setColumnWidth(2, 80);
    favoritesTable->setColumnWidth(3, 90);
    favoritesTable->setColumnWidth(4, 180);
    favoritesTable->setMinimumHeight(180);
    listLayout->addWidget(favoritesTable, 1);
    favoritesSummary = WorkbenchTheme::label({}, "muted", list);
    favoritesSummary->setObjectName(QStringLiteral("favoritesSummaryLabel"));
    listLayout->addWidget(favoritesSummary);
    splitter->addWidget(list);

    auto *detail = WorkbenchTheme::panel(splitter);
    detail->setMinimumWidth(240);
    auto *detailLayout = new QVBoxLayout(detail);
    detailLayout->setContentsMargins(18, 18, 18, 18);
    detailLayout->setSpacing(12);
    detailLayout->addWidget(WorkbenchTheme::label(QStringLiteral("YOUR BOOKMARK / 收藏详情"), "eyebrow", detail));
    favoriteTitle = WorkbenchTheme::label(QStringLiteral("请选择收藏书籍"), "sectionTitle", detail);
    favoriteTitle->setObjectName(QStringLiteral("favoriteSelectedTitleLabel"));
    favoriteMetadata = WorkbenchTheme::label({}, "muted", detail);
    favoriteId = WorkbenchTheme::label({}, "muted", detail);
    favoriteId->setTextInteractionFlags(Qt::TextSelectableByMouse);
    detailLayout->addWidget(favoriteTitle);
    detailLayout->addWidget(favoriteMetadata);
    detailLayout->addWidget(favoriteId);
    favoriteDescription = new QPlainTextEdit(detail);
    favoriteDescription->setObjectName(QStringLiteral("favoriteSelectedDescriptionView"));
    favoriteDescription->setReadOnly(true);
    favoriteDescription->setMinimumHeight(110);
    favoriteDescription->setPlaceholderText(QStringLiteral("在书库勾选“收藏”，就能在这里离线查看。"));
    favoriteDescription->document()->setDocumentMargin(8);
    detailLayout->addWidget(favoriteDescription, 1);
    favoriteDetailButton = new QPushButton(QStringLiteral("简介与正文预览"), detail);
    favoriteDetailButton->setObjectName(QStringLiteral("favoriteDetailsButton"));
    favoriteDetailButton->setProperty("primary", true);
    favoriteDownloadButton = new QPushButton(QStringLiteral("前往整书下载"), detail);
    favoriteDownloadButton->setObjectName(QStringLiteral("openFavoriteDownloadButton"));
    removeFavoriteButton = new QPushButton(QStringLiteral("取消收藏"), detail);
    removeFavoriteButton->setObjectName(QStringLiteral("removeFavoriteButton"));
    removeFavoriteButton->setProperty("danger", true);
    detailLayout->addWidget(favoriteDetailButton);
    detailLayout->addWidget(favoriteDownloadButton);
    detailLayout->addWidget(removeFavoriteButton);
    detailLayout->addWidget(WorkbenchTheme::label(QStringLiteral("只保存书籍资料，不保存正文或凭证，也不与源 App 同步。预览和下载仍需当前会话有权阅读。"), "muted", detail));
    splitter->addWidget(detail);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({680, 280});
    layout->addWidget(splitter, 1);
    auto *scroll = new QScrollArea(pages);
    scroll->setWidgetResizable(true);
    scroll->setWidget(page);
    pages->addWidget(scroll);
}

const NovelSearchBook *BookDownloadWidget::selectedSearchBook() const
{
    const auto row = searchTable->currentRow();
    if (row < 0 || searchTable->selectedItems().isEmpty() || searchTable->isRowHidden(row)
        || !searchTable->item(row, 4))
        return nullptr;
    const auto id = searchTable->item(row, 4)->text();
    const auto &books = controller->searchResults();
    const auto found = std::find_if(books.cbegin(), books.cend(), [&id](const NovelSearchBook &book) {
        return book.id == id;
    });
    return found == books.cend() ? nullptr : &*found;
}

void BookDownloadWidget::updateSelectedBook()
{
    const auto *book = selectedSearchBook();
    selectedTitle->setText(book ? book->name : QStringLiteral("请选择书籍"));
    selectedMetadata->setText(book ? QStringLiteral("%1 · %2 章 · %3")
        .arg(book->author.isEmpty() ? QStringLiteral("作者未提供") : book->author,
             book->chapterCount.isEmpty() ? QStringLiteral("—") : book->chapterCount, book->status) : QString());
    selectedId->setText(book ? QStringLiteral("ID：%1").arg(book->id) : QString());
    const auto description = book ? (book->description.trimmed().isEmpty() ? QStringLiteral("暂无简介") : book->description) : QString();
    if (selectedDescription->toPlainText() != description)
        selectedDescription->setPlainText(description);
}

const NovelSearchBook *BookDownloadWidget::selectedFavoriteBook() const
{
    const auto row = favoritesTable->currentRow();
    if (row < 0 || favoritesTable->selectedItems().isEmpty() || favoritesTable->isRowHidden(row)
        || !favoritesTable->item(row, 4))
        return nullptr;
    const auto id = favoritesTable->item(row, 4)->text();
    const auto &books = controller->favorites();
    const auto found = std::find_if(books.cbegin(), books.cend(), [&id](const NovelSearchBook &book) {
        return book.id == id;
    });
    return found == books.cend() ? nullptr : &*found;
}

void BookDownloadWidget::updateSelectedFavorite()
{
    const auto *book = selectedFavoriteBook();
    favoriteTitle->setText(book ? book->name : QStringLiteral("请选择收藏书籍"));
    favoriteMetadata->setText(book ? QStringLiteral("%1 · %2 章 · %3")
        .arg(book->author.isEmpty() ? QStringLiteral("作者未提供") : book->author,
             book->chapterCount.isEmpty() ? QStringLiteral("—") : book->chapterCount, book->status) : QString());
    favoriteId->setText(book ? QStringLiteral("ID：%1").arg(book->id) : QString());
    const auto description = book ? (book->description.trimmed().isEmpty() ? QStringLiteral("暂无简介") : book->description) : QString();
    if (favoriteDescription->toPlainText() != description)
        favoriteDescription->setPlainText(description);
}

void BookDownloadWidget::showFavorites()
{
    const auto *selected = selectedFavoriteBook();
    const auto selectedId = selected ? selected->id : QString();
    const QSignalBlocker blocker(favoritesTable);
    const auto &books = controller->favorites();
    favoritesTable->clearContents();
    favoritesTable->setRowCount(static_cast<int>(books.size()));
    favoritesTable->clearSelection();
    for (int row = 0; row < books.size(); ++row) {
        const auto &book = books[row];
        const QStringList values{book.name, book.author.isEmpty() ? QStringLiteral("未提供") : book.author,
                                 book.chapterCount, book.status, book.id};
        for (int column = 0; column < values.size(); ++column)
            favoritesTable->setItem(row, column, new QTableWidgetItem(values[column]));
        if (book.id == selectedId)
            favoritesTable->selectRow(row);
    }
    applyFavoritesFilter();
}

void BookDownloadWidget::applyFavoritesFilter()
{
    int visible = 0;
    const auto text = favoritesFilter->text();
    for (int row = 0; row < favoritesTable->rowCount(); ++row) {
        const bool matches = favoritesTable->item(row, 0)->text().contains(text, Qt::CaseInsensitive)
            || favoritesTable->item(row, 1)->text().contains(text, Qt::CaseInsensitive);
        favoritesTable->setRowHidden(row, !matches);
        visible += matches;
    }
    const auto error = controller->favoritesError();
    favoritesSummary->setText(!error.isEmpty() ? error
        : favoritesTable->rowCount() == 0
            ? QStringLiteral("还没有收藏。在小说书库勾选每本书左侧的“收藏”，即可加入这里。")
            : visible == 0 ? QStringLiteral("没有匹配的收藏，请换一个书名或作者。")
            : QStringLiteral("本机收藏 %1 本 · 当前显示 %2 本 · 最近收藏在前").arg(favoritesTable->rowCount()).arg(visible));
    updateActions();
}

void BookDownloadWidget::updateFavoriteMarkers()
{
    QSet<QString> ids;
    for (const auto &book : controller->favorites())
        ids.insert(book.id);
    const QSignalBlocker blocker(searchTable);
    const QSignalBlocker scrollBlocker(searchTable->verticalScrollBar());
    searchTable->setSortingEnabled(false);
    for (int row = 0; row < searchTable->rowCount(); ++row) {
        auto *item = searchTable->item(row, 5);
        const bool favorite = ids.contains(item->data(Qt::UserRole).toString().toLower());
        item->setCheckState(favorite ? Qt::Checked : Qt::Unchecked);
        item->setToolTip(favorite ? QStringLiteral("已收藏，取消勾选移出本机收藏。")
                                  : QStringLiteral("勾选加入本机收藏，不联网。"));
    }
    searchTable->setSortingEnabled(true);
}

void BookDownloadWidget::changeFavorite(const NovelSearchBook &book, bool favorite)
{
    const auto error = controller->setFavorite(book, favorite);
    if (!error.isEmpty())
        updateFavoriteMarkers();
    statusLabel->setText(!error.isEmpty() ? error
        : favorite ? QStringLiteral("已加入本机收藏；可在左侧“我的收藏”离线查看，未联网。")
                   : QStringLiteral("已取消收藏，书库缓存和已下载 TXT 保持不变。"));
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

void BookDownloadWidget::reorderCurrentCategories()
{
    if (controller->isBusy() || controller->categories().size() < 2)
        return;
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("调整分类顺序"));
    dialog.resize(480, 520);
    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(WorkbenchTheme::label(QStringLiteral("拖动分类调整顺序，或选择后使用上移 / 下移。保存后生效；取消不会修改分类。"), "muted", &dialog));
    auto *list = new QListWidget(&dialog);
    list->setObjectName(QStringLiteral("categoryOrderList"));
    list->setDragDropMode(QAbstractItemView::InternalMove);
    list->setDefaultDropAction(Qt::MoveAction);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    list->setAlternatingRowColors(true);
    for (const auto &category : controller->categories()) {
        auto *item = new QListWidgetItem(QStringLiteral("%1 · %2").arg(category.name, category.tagId), list);
        item->setData(Qt::UserRole, category.key());
        item->setFlags(item->flags() & ~Qt::ItemIsDropEnabled);
    }
    list->setCurrentRow(0);
    layout->addWidget(list, 1);
    auto *moves = new QHBoxLayout;
    auto *up = new QPushButton(QStringLiteral("上移"), &dialog);
    up->setObjectName(QStringLiteral("moveCategoryUpButton"));
    auto *down = new QPushButton(QStringLiteral("下移"), &dialog);
    down->setObjectName(QStringLiteral("moveCategoryDownButton"));
    up->setAutoDefault(false);
    down->setAutoDefault(false);
    moves->addWidget(up);
    moves->addWidget(down);
    moves->addStretch();
    layout->addLayout(moves);
    const auto updateMoves = [list, up, down] {
        const auto row = list->currentRow();
        up->setEnabled(row > 0);
        down->setEnabled(row >= 0 && row + 1 < list->count());
    };
    connect(list, &QListWidget::currentRowChanged, &dialog, updateMoves);
    connect(list->model(), &QAbstractItemModel::rowsMoved, &dialog, updateMoves);
    connect(up, &QPushButton::clicked, &dialog, [list, updateMoves] {
        const auto row = list->currentRow();
        if (row > 0) {
            list->insertItem(row - 1, list->takeItem(row));
            list->setCurrentRow(row - 1);
            updateMoves();
        }
    });
    connect(down, &QPushButton::clicked, &dialog, [list, updateMoves] {
        const auto row = list->currentRow();
        if (row >= 0 && row + 1 < list->count()) {
            list->insertItem(row + 1, list->takeItem(row));
            list->setCurrentRow(row + 1);
            updateMoves();
        }
    });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("保存顺序"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    updateMoves();
    if (dialog.exec() != QDialog::Accepted)
        return;
    QStringList keys;
    for (int row = 0; row < list->count(); ++row)
        keys.append(list->item(row)->data(Qt::UserRole).toString());
    const auto error = controller->reorderCategories(keys);
    statusLabel->setText(error.isEmpty()
        ? QStringLiteral("分类顺序已保存；当前分类、列表和分页保持不变，未联网。") : error);
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
    catalogMetric->setText(book.chapters.isEmpty() ? QStringLiteral("—") : QString::number(book.chapters.size()));
    completedMetric->setText(QStringLiteral("—"));
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
        auto *favorite = new QTableWidgetItem;
        favorite->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        favorite->setData(Qt::UserRole, book.id);
        searchTable->setItem(row, 5, favorite);
        if (book.id.compare(selectedId, Qt::CaseInsensitive) == 0)
            searchTable->selectRow(row);
    }
    updateFavoriteMarkers();
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
    updateActions();
}

void BookDownloadWidget::selectSearchBook()
{
    const auto *book = selectedSearchBook();
    if (controller->isBusy() || !book) {
        updateActions();
        return;
    }
    bookId->setText(book->id);
    statusLabel->setText(QStringLiteral("已选择《%1》，未联网。可预览简介，或前往“整书下载”选择保存目录。")
                             .arg(book->name));
    updateActions();
}

void BookDownloadWidget::showBookDetails()
{
    if (const auto *book = selectedSearchBook()) {
        const NovelSearchBook selected = *book;
        showBookDetailsFor(selected);
    }
}

void BookDownloadWidget::showBookDetailsFor(const NovelSearchBook &book)
{
    if (controller->isBusy())
        return;
    loadAll = false;
    const auto id = book.id;
    BookDetailDialog dialog(book, this);
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
    reorderCategoriesButton->setEnabled(!busy && controller->categories().size() > 1);
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
    updateSelectedBook();
    const bool hasSelection = selectedSearchBook() != nullptr;
    detailButton->setEnabled(!busy && hasSelection);
    openDownloadButton->setEnabled(!busy && hasSelection);
    updateSelectedFavorite();
    const bool hasFavoriteSelection = selectedFavoriteBook() != nullptr;
    removeFavoriteButton->setEnabled(!busy && hasFavoriteSelection);
    favoriteDetailButton->setEnabled(!busy && hasFavoriteSelection);
    favoriteDownloadButton->setEnabled(!busy && hasFavoriteSelection);
    moreSearchButton->setEnabled(!busy && hasSession && controller->canLoadMoreSearch());
    catalogButton->setEnabled(!busy && validInput);
    linksButton->setEnabled(!busy && hasBook);
    downloadButton->setEnabled(!busy && validInput && QFileInfo(outputDirectory->text().trimmed()).isDir());
    cancelButton->setEnabled(busy);
    const auto row = table->currentRow();
    copyButton->setEnabled(row >= 0 && table->item(row, 4) && !table->item(row, 4)->text().isEmpty());
}
