#include "bookdetaildialog.h"
#include "workbenchtheme.h"

#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTextDocument>
#include <QVBoxLayout>

BookDetailDialog::BookDetailDialog(const NovelSearchBook &book, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("书籍详情与正文预览"));
    setStyleSheet(WorkbenchTheme::styleSheet());
    resize(820, 780);
    setMinimumSize(620, 560);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 20);
    layout->setSpacing(14);
    layout->addWidget(WorkbenchTheme::label(QStringLiteral("A FEW PAGES FIRST / 书籍详情"), "eyebrow", this));
    layout->addWidget(WorkbenchTheme::label(book.name, "pageTitle", this));
    layout->addWidget(WorkbenchTheme::label(QStringLiteral("作者：%1    章节数：%2    状态：%3")
        .arg(book.author.isEmpty() ? QStringLiteral("未提供") : book.author,
             book.chapterCount.isEmpty() ? QStringLiteral("未提供") : book.chapterCount,
             book.status), "muted", this));
    auto *descriptionCard = WorkbenchTheme::panel(this);
    auto *descriptionLayout = new QVBoxLayout(descriptionCard);
    descriptionLayout->setContentsMargins(16, 14, 16, 14);
    descriptionLayout->addWidget(WorkbenchTheme::label(QStringLiteral("书籍简介"), "sectionTitle", descriptionCard));
    descriptionView = new QPlainTextEdit(descriptionCard);
    descriptionView->setObjectName(QStringLiteral("bookDescriptionView"));
    descriptionView->setReadOnly(true);
    descriptionView->setPlainText(book.description.trimmed().isEmpty() ? QStringLiteral("暂无简介") : book.description);
    descriptionView->document()->setDocumentMargin(12);
    descriptionView->setMaximumHeight(130);
    descriptionLayout->addWidget(descriptionView);
    layout->addWidget(descriptionCard);
    auto *readingCard = WorkbenchTheme::panel(this);
    auto *readingLayout = new QVBoxLayout(readingCard);
    readingLayout->setContentsMargins(16, 16, 16, 16);
    readingLayout->setSpacing(12);
    readingLayout->addWidget(WorkbenchTheme::label(QStringLiteral("正文预览 / 前三章"), "sectionTitle", readingCard));
    auto *row = new QHBoxLayout;
    loadButton = new QPushButton(QStringLiteral("加载前三章"), readingCard);
    loadButton->setObjectName(QStringLiteral("loadChapterPreviewButton"));
    loadButton->setProperty("primary", true);
    cancelButton = new QPushButton(QStringLiteral("停止预览加载"), readingCard);
    cancelButton->setEnabled(false);
    chapterChoice = new QComboBox(readingCard);
    chapterChoice->setObjectName(QStringLiteral("previewChapterCombo"));
    chapterChoice->setMinimumContentsLength(8);
    chapterChoice->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    row->addWidget(loadButton);
    row->addWidget(cancelButton);
    row->addWidget(chapterChoice, 1);
    readingLayout->addLayout(row);
    previewView = new QPlainTextEdit(readingCard);
    previewView->setObjectName(QStringLiteral("chapterPreviewView"));
    previewView->setReadOnly(true);
    previewView->document()->setDocumentMargin(20);
    previewView->setPlaceholderText(QStringLiteral("点击“加载前三章”后才联网。仅处理当前会话免费且有权阅读的内容。"));
    readingLayout->addWidget(previewView, 1);
    layout->addWidget(readingCard, 1);
    auto *footer = new QHBoxLayout;
    messageLabel = WorkbenchTheme::label(QStringLiteral("正文仅在本弹窗内保留，不自动生成 TXT；关闭后释放。"), "status", this);
    footer->addWidget(messageLabel, 1);
    auto *closeButton = new QPushButton(QStringLiteral("关闭"), this);
    footer->addWidget(closeButton, 0, Qt::AlignTop);
    layout->addLayout(footer);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(chapterChoice, &QComboBox::currentIndexChanged, this, &BookDetailDialog::showChapter);
    connect(cancelButton, &QPushButton::clicked, this, [this] {
        cancelButton->setEnabled(false);
        emit cancelRequested();
    });
    connect(loadButton, &QPushButton::clicked, this, [this] {
        loadButton->setEnabled(false);
        cancelButton->setEnabled(true);
        setMessage(QStringLiteral("正在读取目录与前三章，请稍候…"));
        emit previewRequested();
    });
}

void BookDetailDialog::setCatalog(const NovelBook &book)
{
    if (!book.description.trimmed().isEmpty())
        descriptionView->setPlainText(book.description);
    const QSignalBlocker blocker(chapterChoice);
    chapterChoice->clear();
    texts.clear();
    statuses.clear();
    const auto count = qMin(3, static_cast<int>(book.chapters.size()));
    for (int index = 0; index < count; ++index) {
        chapterChoice->addItem(book.chapters[index].name);
        texts.append(QString());
        statuses.append(QStringLiteral("等待加载"));
    }
    showChapter();
}

void BookDetailDialog::setChapter(int index, const QString &text, const QString &status)
{
    if (index < 0 || index >= texts.size())
        return;
    texts[index] = text;
    statuses[index] = status;
    if (chapterChoice->currentIndex() == index)
        showChapter();
}

void BookDetailDialog::showChapter()
{
    const auto index = chapterChoice->currentIndex();
    if (index < 0 || index >= texts.size())
        return;
    previewView->setPlainText(texts[index].isEmpty() ? statuses[index] : texts[index]);
}

void BookDetailDialog::setMessage(const QString &message)
{
    messageLabel->setText(message);
}

void BookDetailDialog::finishPreview(const QString &message)
{
    cancelButton->setEnabled(false);
    for (auto &status : statuses) {
        if (status == QStringLiteral("等待加载"))
            status = QStringLiteral("未加载（预览已结束或停止）");
    }
    showChapter();
    setMessage(message);
}

void BookDetailDialog::setPreviewAvailable(bool available)
{
    loadButton->setEnabled(available);
    if (!available)
        setMessage(QStringLiteral("简介可离线查看；请先获取有效会话或导入请求抓包，再打开弹窗加载正文。"));
}
