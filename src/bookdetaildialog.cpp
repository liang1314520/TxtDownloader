#include "bookdetaildialog.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

BookDetailDialog::BookDetailDialog(const NovelSearchBook &book, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("书籍详情"));
    resize(760, 700);
    setMinimumSize(550, 500);
    auto *layout = new QVBoxLayout(this);
    auto *title = new QLabel(book.name, this);
    title->setTextFormat(Qt::PlainText);
    title->setWordWrap(true);
    auto font = title->font();
    font.setPointSize(font.pointSize() + 3);
    font.setBold(true);
    title->setFont(font);
    layout->addWidget(title);
    auto *metadata = new QLabel(QStringLiteral("作者：%1    章节数：%2    状态：%3")
                                   .arg(book.author.isEmpty() ? QStringLiteral("未提供") : book.author,
                                        book.chapterCount.isEmpty() ? QStringLiteral("未提供") : book.chapterCount,
                                        book.status), this);
    metadata->setTextFormat(Qt::PlainText);
    metadata->setWordWrap(true);
    layout->addWidget(metadata);
    layout->addWidget(new QLabel(QStringLiteral("书籍简介"), this));
    descriptionView = new QPlainTextEdit(this);
    descriptionView->setObjectName(QStringLiteral("bookDescriptionView"));
    descriptionView->setReadOnly(true);
    descriptionView->setPlainText(book.description.trimmed().isEmpty() ? QStringLiteral("暂无简介") : book.description);
    descriptionView->setMaximumHeight(180);
    layout->addWidget(descriptionView);
    auto *row = new QHBoxLayout;
    loadButton = new QPushButton(QStringLiteral("加载前三章"), this);
    loadButton->setObjectName(QStringLiteral("loadChapterPreviewButton"));
    cancelButton = new QPushButton(QStringLiteral("停止预览加载"), this);
    cancelButton->setEnabled(false);
    chapterChoice = new QComboBox(this);
    chapterChoice->setObjectName(QStringLiteral("previewChapterCombo"));
    row->addWidget(loadButton);
    row->addWidget(cancelButton);
    row->addWidget(chapterChoice, 1);
    layout->addLayout(row);
    previewView = new QPlainTextEdit(this);
    previewView->setObjectName(QStringLiteral("chapterPreviewView"));
    previewView->setReadOnly(true);
    previewView->setPlaceholderText(QStringLiteral("按需加载目录中的前三章；仅处理当前会话免费且有权阅读的内容。"));
    layout->addWidget(previewView, 1);
    messageLabel = new QLabel(QStringLiteral("正文仅在本弹窗内保留，不自动生成 TXT；关闭后释放。"), this);
    messageLabel->setTextFormat(Qt::PlainText);
    messageLabel->setWordWrap(true);
    layout->addWidget(messageLabel);
    auto *closeButton = new QPushButton(QStringLiteral("关闭"), this);
    layout->addWidget(closeButton, 0, Qt::AlignRight);
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
        setMessage(QStringLiteral("简介可离线查看；请选择有效请求抓包后再打开弹窗加载正文。"));
}
