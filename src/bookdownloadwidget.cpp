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
#include <QStandardPaths>
#include <QTableWidget>
#include <QVBoxLayout>

BookDownloadWidget::BookDownloadWidget(QWidget *parent)
    : QWidget(parent), controller(new BookDownloadController(this))
{
    auto *layout = new QVBoxLayout(this);
    auto *notice = new QLabel(QStringLiteral(
        "填写书籍 ID 并选择原始请求抓包，一键下载并合并带章节标题的整本 TXT，单章文件保留。\n"
        "自动使用本机 App 4.0.9 密钥；会话仅在内存中使用，仅处理当前会话免费可阅读的章节。"), this);
    notice->setWordWrap(true);
    layout->addWidget(notice);
    settings = new QWidget(this);
    auto *grid = new QGridLayout(settings);
    grid->setContentsMargins(0, 0, 0, 0);
    bookId = new QLineEdit(settings);
    bookId->setObjectName(QStringLiteral("bookIdEdit"));
    bookId->setMaxLength(32);
    bookId->setPlaceholderText(QStringLiteral("32 位书籍 ID，不是章节 ID；首版不支持按书名搜索"));
    capturePath = new QLineEdit(settings);
    capturePath->setObjectName(QStringLiteral("sessionCaptureEdit"));
    capturePath->setPlaceholderText(QStringLiteral("选择书籍详情或章节详情的 http_req_*.hcy"));
    auto *captureButton = new QPushButton(QStringLiteral("选择抓包…"), settings);
    outputDirectory = new QLineEdit(settings);
    outputDirectory->setObjectName(QStringLiteral("bookOutputDirectoryEdit"));
    outputDirectory->setText(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
    auto *directoryButton = new QPushButton(QStringLiteral("保存目录…"), settings);
    grid->addWidget(new QLabel(QStringLiteral("书籍 ID"), settings), 0, 0);
    grid->addWidget(bookId, 0, 1, 1, 2);
    grid->addWidget(new QLabel(QStringLiteral("会话请求抓包"), settings), 1, 0);
    grid->addWidget(capturePath, 1, 1);
    grid->addWidget(captureButton, 1, 2);
    grid->addWidget(new QLabel(QStringLiteral("TXT 保存目录"), settings), 2, 0);
    grid->addWidget(outputDirectory, 2, 1);
    grid->addWidget(directoryButton, 2, 2);
    grid->setColumnStretch(1, 1);
    layout->addWidget(settings);

    auto *actions = new QHBoxLayout;
    catalogButton = new QPushButton(QStringLiteral("仅获取目录"), this);
    linksButton = new QPushButton(QStringLiteral("仅获取全部链接"), this);
    downloadButton = new QPushButton(QStringLiteral("下载并合并"), this);
    downloadButton->setDefault(true);
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
    statusLabel = new QLabel(QStringLiteral("填写书籍 ID、选择请求抓包和保存目录，然后点击“下载并合并”；无需手动输入密钥。"), this);
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
    for (auto *edit : {bookId, capturePath})
        connect(edit, &QLineEdit::textChanged, this, &BookDownloadWidget::invalidateSession);
    connect(outputDirectory, &QLineEdit::textChanged, this, &BookDownloadWidget::updateActions);
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
        if (!busy)
            fileProgress->clear();
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
        progress->setRange(0, qMax(1, total));
        progress->setValue(completed);
        progress->setFormat(QStringLiteral("%1 / %2 条目录记录").arg(completed).arg(total));
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
        QStringLiteral("获取目录与章节链接需要将抓包中的会话凭证随加密 Body 发送到：\n"
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

void BookDownloadWidget::updateActions()
{
    const bool busy = controller->isBusy();
    const bool hasBook = !controller->book().chapters.isEmpty();
    const bool validInput = NovelApi::isId(bookId->text().trimmed())
        && QFileInfo(capturePath->text().trimmed()).isFile();
    catalogButton->setEnabled(!busy && validInput);
    linksButton->setEnabled(!busy && hasBook);
    downloadButton->setEnabled(!busy && validInput && QFileInfo(outputDirectory->text().trimmed()).isDir());
    cancelButton->setEnabled(busy);
    const auto row = table->currentRow();
    copyButton->setEnabled(row >= 0 && table->item(row, 4) && !table->item(row, 4)->text().isEmpty());
}
