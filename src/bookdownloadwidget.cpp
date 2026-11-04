#include "bookdownloadwidget.h"
#include "bookdownloadcontroller.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
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
#include <QStandardPaths>
#include <QTableWidget>
#include <QVBoxLayout>

BookDownloadWidget::BookDownloadWidget(QWidget *parent)
    : QWidget(parent), controller(new BookDownloadController(this))
{
    auto *layout = new QVBoxLayout(this);
    auto *notice = new QLabel(QStringLiteral(
        "按书名搜索并选中结果自动填入 ID，或直接填写书籍 ID；下载后自动合并带标题的整本 TXT。\n"
        "自动使用本机 App 4.0.9 密钥；会话仅在内存中使用，仅处理当前会话免费可阅读的章节。"), this);
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
    keywords->setPlaceholderText(QStringLiteral("输入书名或关键词，搜索后选择对应书籍"));
    searchButton = new QPushButton(QStringLiteral("搜索书名"), settings);
    searchButton->setObjectName(QStringLiteral("searchBooksButton"));
    searchButton->setAutoDefault(false);
    capturePath = new QLineEdit(settings);
    capturePath->setObjectName(QStringLiteral("sessionCaptureEdit"));
    capturePath->setPlaceholderText(QStringLiteral("选择小说搜索、书籍详情或章节详情的 http_req_*.hcy"));
    auto *captureButton = new QPushButton(QStringLiteral("选择抓包…"), settings);
    outputDirectory = new QLineEdit(settings);
    outputDirectory->setObjectName(QStringLiteral("bookOutputDirectoryEdit"));
    outputDirectory->setText(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
    auto *directoryButton = new QPushButton(QStringLiteral("保存目录…"), settings);
    grid->addWidget(new QLabel(QStringLiteral("书名搜索"), settings), 0, 0);
    grid->addWidget(keywords, 0, 1);
    grid->addWidget(searchButton, 0, 2);
    grid->addWidget(new QLabel(QStringLiteral("书籍 ID"), settings), 1, 0);
    grid->addWidget(bookId, 1, 1, 1, 2);
    grid->addWidget(new QLabel(QStringLiteral("会话请求抓包"), settings), 2, 0);
    grid->addWidget(capturePath, 2, 1);
    grid->addWidget(captureButton, 2, 2);
    grid->addWidget(new QLabel(QStringLiteral("TXT 保存目录"), settings), 3, 0);
    grid->addWidget(outputDirectory, 3, 1);
    grid->addWidget(directoryButton, 3, 2);
    grid->setColumnStretch(1, 1);
    layout->addWidget(settings);

    auto *searchRow = new QHBoxLayout;
    searchLabel = new QLabel(QStringLiteral("搜索结果：单击书籍自动填入 ID，不会立即下载。"), this);
    searchLabel->setTextFormat(Qt::PlainText);
    searchLabel->setWordWrap(true);
    moreSearchButton = new QPushButton(QStringLiteral("加载更多结果"), this);
    moreSearchButton->setObjectName(QStringLiteral("loadMoreBooksButton"));
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
    statusLabel = new QLabel(QStringLiteral("选择请求抓包后可按书名搜索；选中书籍或手动填入 ID，再点击“下载并合并”。"), this);
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
    connect(searchButton, &QPushButton::clicked, this, [this] {
        if (confirmNetwork())
            controller->searchBooks(keywords->text().trimmed(), capturePath->text().trimmed());
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
    connect(cancelButton, &QPushButton::clicked, controller, &BookDownloadController::cancel);
    connect(controller, &BookDownloadController::bookChanged, this, &BookDownloadWidget::showBook);
    connect(controller, &BookDownloadController::statusChanged, statusLabel, &QLabel::setText);
    connect(controller, &BookDownloadController::busyChanged, this, [this](bool busy) {
        settings->setEnabled(!busy);
        searchTable->setEnabled(!busy);
        if (!busy) {
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
        if (controller->isSearching()) {
            progress->setRange(0, 0);
            progress->setFormat(QStringLiteral("正在查询书名…"));
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
    updateActions();
}

void BookDownloadWidget::invalidateSession()
{
    networkConfirmed = false;
    controller->invalidate();
    updateActions();
}

bool BookDownloadWidget::confirmNetwork()
{
    if (networkConfirmed)
        return true;
    networkConfirmed = QMessageBox::warning(
        this, QStringLiteral("确认发送会话凭证"),
        QStringLiteral("搜索书籍、获取目录与章节链接需要将抓包中的会话凭证随加密 Body 发送到：\n"
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
    const QSignalBlocker blocker(searchTable);
    const auto &books = controller->searchResults();
    const auto selectedId = bookId->text().trimmed();
    searchTable->clearContents();
    searchTable->setRowCount(static_cast<int>(books.size()));
    searchTable->clearSelection();
    for (int row = 0; row < books.size(); ++row) {
        const auto &book = books[row];
        const QStringList values{book.name, book.author.isEmpty() ? QStringLiteral("未提供") : book.author,
                                 book.chapterCount, book.status, book.id};
        for (int column = 0; column < values.size(); ++column)
            searchTable->setItem(row, column, new QTableWidgetItem(values[column]));
        if (book.id.compare(selectedId, Qt::CaseInsensitive) == 0)
            searchTable->selectRow(row);
    }
    if (controller->searchPage() == 0)
        searchLabel->setText(QStringLiteral("搜索结果：单击书籍自动填入 ID，不会立即下载。"));
    else
        searchLabel->setText(QStringLiteral("已读取 %1 页，共 %2 本。单击书籍自动填入 ID。%3")
                                 .arg(controller->searchPage()).arg(books.size())
                                 .arg(controller->canLoadMoreSearch() ? QStringLiteral("可手动加载更多。")
                                                                      : QStringLiteral("已停止分页。")));
    updateActions();
}

void BookDownloadWidget::selectSearchBook()
{
    if (controller->isBusy() || searchTable->selectedItems().isEmpty())
        return;
    const auto row = searchTable->currentRow();
    const auto &books = controller->searchResults();
    if (row < 0 || row >= books.size())
        return;
    const auto &book = books[row];
    bookId->setText(book.id);
    statusLabel->setText(QStringLiteral("已选择《%1》，ID 已填入。请选择保存目录，然后点击“下载并合并”。")
                             .arg(book.name));
    updateActions();
}

void BookDownloadWidget::updateActions()
{
    const bool busy = controller->isBusy();
    const bool hasBook = !controller->book().chapters.isEmpty();
    const bool hasCapture = QFileInfo(capturePath->text().trimmed()).isFile();
    const bool validInput = NovelApi::isId(bookId->text().trimmed()) && hasCapture;
    searchButton->setEnabled(!busy && hasCapture && NovelApi::isSearchKeywords(keywords->text().trimmed()));
    moreSearchButton->setEnabled(!busy && controller->canLoadMoreSearch());
    catalogButton->setEnabled(!busy && validInput);
    linksButton->setEnabled(!busy && hasBook);
    downloadButton->setEnabled(!busy && validInput && QFileInfo(outputDirectory->text().trimmed()).isDir());
    cancelButton->setEnabled(busy);
    const auto row = table->currentRow();
    copyButton->setEnabled(row >= 0 && table->item(row, 4) && !table->item(row, 4)->text().isEmpty());
}
