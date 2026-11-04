#include "bookdownloadcontroller.h"
#include "chapterdownload.h"
#include "novdecoder.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QStringDecoder>
#include <QTemporaryFile>

namespace {

QString safeName(QString name, qsizetype maximum)
{
    name.replace(QRegularExpression(QStringLiteral(R"([<>:"/\\|?*\x00-\x1F\x7F])")), QStringLiteral("_"));
    name = name.left(maximum);
    if (!name.isEmpty() && name.back().isHighSurrogate())
        name.chop(1);
    name.remove(QRegularExpression(QStringLiteral("[. ]+$")));
    static const QRegularExpression reserved(
        QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\\..*)?$"),
        QRegularExpression::CaseInsensitiveOption);
    if (name.isEmpty() || name == QStringLiteral(".") || name == QStringLiteral("..") || reserved.match(name).hasMatch())
        name = QStringLiteral("book");
    return name;
}

bool hasLinkedAncestor(const QString &path)
{
    QDir directory(QFileInfo(path).absoluteFilePath());
    for (;;) {
        const QFileInfo info(directory.absolutePath());
        if (info.isSymLink() || info.isJunction())
            return true;
        if (directory.isRoot())
            return false;
        if (!directory.cdUp())
            return true;
    }
}

}

BookDownloadController::BookDownloadController(QObject *parent)
    : QObject(parent), api(new NovelApi(this)), downloader(new ChapterDownload(this))
{
    cacheWarning = BookListCache::load(BookListCache::defaultPath(), groupList);
    next.setSingleShot(true);
    connect(&next, &QTimer::timeout, this, [this] {
        if (mode == Mode::Merge)
            mergeNextChapter();
        else if (mode == Mode::Preview)
            nextPreviewChapter();
        else
            nextChapter();
    });
    connect(api, &NovelApi::bookReady, this, [this](const NovelBook &book) {
        if (mode == Mode::PreviewCatalog) {
            previewBookData = book;
            previewIndex = 0;
            setMode(Mode::Preview);
            emit previewCatalogReady(book);
            emit progressChanged(0, qMin(3, static_cast<int>(book.chapters.size())));
            next.start(0);
            return;
        }
        currentBook = book;
        emit bookChanged(currentBook);
        emit progressChanged(0, static_cast<int>(book.chapters.size()));
        if (!pendingDirectory.isEmpty()) {
            const auto directory = pendingDirectory;
            pendingDirectory.clear();
            startDownload(directory);
            return;
        }
        finishTask(QStringLiteral("已获取《%1》，共 %2 条目录记录。")
                       .arg(book.name).arg(book.chapters.size()));
    });
    connect(api, &NovelApi::chapterReady, this, [this](const QString &id, const QUrl &content) {
        if (mode == Mode::Preview) {
            if (previewIndex >= previewBookData.chapters.size() || previewBookData.chapters[previewIndex].id != id) {
                stop(QStringLiteral("返回的预览章节与当前请求不一致。"));
                return;
            }
            downloader->startPreview(content, decodeKey);
            return;
        }
        if (mode != Mode::Links && mode != Mode::Download)
            return;
        if (currentBook.chapters[current].id != id) {
            stop(QStringLiteral("返回的章节与当前任务不一致，已停止。"));
            return;
        }
        currentBook.chapters[current].content = content;
        if (mode == Mode::Links) {
            updateChapter(QStringLiteral("链接已获取"));
            ++current;
            emit progressChanged(current, static_cast<int>(currentBook.chapters.size()));
            next.start(0);
            return;
        }
        const auto directoryError = checkDirectory();
        if (!directoryError.isEmpty()) {
            stop(directoryError);
            return;
        }
        const QFileInfo output(chapterPath(current));
        if (output.exists() || output.isSymLink()) {
            stop(QStringLiteral("下载期间出现同名文件，已停止以避免覆盖。"));
            return;
        }
        updateChapter(QStringLiteral("正在下载"));
        downloader->start(content, output.absoluteFilePath(), decodeKey, false, true);
    });
    connect(api, &NovelApi::searchReady, this,
            [this](const QVector<NovelSearchBook> &books, int page, bool hasMore) {
        if (mode != Mode::Search)
            return;
        auto &list = activeList();
        if (page != list.page + 1) {
            stop(QStringLiteral("列表返回的页码与当前任务不一致，已停止。"));
            return;
        }
        QSet<QString> ids;
        for (const auto &book : list.books)
            ids.insert(book.id);
        const auto newCount = std::count_if(books.cbegin(), books.cend(), [&ids](const NovelSearchBook &book) {
            return !ids.contains(book.id);
        });
        if (list.books.size() + newCount > 100000) {
            list.hasMore = false;
            list.complete = false;
            if (groupActive)
                BookListCache::save(BookListCache::defaultPath(), list);
            emit searchResultsChanged();
            stop(QStringLiteral("列表已达到 100000 本限制，未确认完整；已加载结果保留。"));
            return;
        }
        int added = 0;
        for (const auto &book : books) {
            if (ids.contains(book.id))
                continue;
            ids.insert(book.id);
            list.books.append(book);
            ++added;
        }
        list.page = page;
        list.complete = !hasMore;
        list.updatedAt = QDateTime::currentDateTimeUtc();
        list.hasMore = hasMore && page < 10000 && !(groupActive && !books.isEmpty() && added == 0);
        const auto saved = groupActive ? BookListCache::save(BookListCache::defaultPath(), list) : QString();
        emit searchResultsChanged();
        const auto ending = page == 10000 && hasMore
            ? QStringLiteral("已达 10000 页限制，不再继续加载。")
            : !list.hasMore && hasMore ? QStringLiteral("返回的小说全部重复，已停止分页；不代表已确认全量。")
            : hasMore ? QStringLiteral("可继续加载下一页。") : QStringLiteral("服务器已返回空页，没有更多结果。");
        finishTask(QStringLiteral("已读取%1第 %2 页，新增 %3 本，共 %4 本。%5%6")
                       .arg(groupActive ? QStringLiteral("分组") : QStringLiteral("搜索"))
                       .arg(page).arg(added).arg(list.books.size()).arg(ending)
                       .arg(saved.isEmpty() ? QString() : QStringLiteral("\n") + saved));
    });
    connect(api, &NovelApi::failed, this, &BookDownloadController::stop);
    connect(downloader, &ChapterDownload::progress, this, &BookDownloadController::downloadProgress);
    connect(downloader, &ChapterDownload::previewReady, this, [this](const QString &text) {
        if (mode == Mode::Preview)
            emit previewChapterReady(previewIndex, text, QStringLiteral("已加载，仅本弹窗内保留"));
    });
    connect(downloader, &ChapterDownload::finished, this,
            [this](bool success, const QString &message, qint64, qsizetype) {
        if (mode == Mode::Preview) {
            if (cancelling) {
                finishTask(QStringLiteral("预览已取消，已加载内容保留至关闭弹窗。"));
            } else if (!success) {
                emit previewChapterReady(previewIndex, {}, QStringLiteral("加载失败"));
                stop(message);
            } else {
                ++previewIndex;
                emit progressChanged(previewIndex, qMin(3, static_cast<int>(previewBookData.chapters.size())));
                next.start(0);
            }
            return;
        }
        if (cancelling) {
            updateChapter(QStringLiteral("已取消"));
            finishTask(QStringLiteral("已取消，已完成的 TXT 保留。"));
            return;
        }
        if (mode != Mode::Download)
            return;
        if (!success) {
            stop(message);
            return;
        }
        updateChapter(QStringLiteral("TXT 已保存"));
        ++saved;
        ++current;
        emit progressChanged(current, static_cast<int>(currentBook.chapters.size()));
        next.start(0);
    });
}

BookDownloadController::~BookDownloadController()
{
    api->disconnect(this);
    downloader->disconnect(this);
    api->cancel();
    downloader->cancel();
    decodeKey.fill('\0');
}

void BookDownloadController::setMode(Mode value)
{
    const bool wasBusy = isBusy();
    mode = value;
    if (wasBusy != isBusy())
        emit busyChanged(isBusy());
}

void BookDownloadController::invalidate(const QString &capturePath)
{
    if (isBusy())
        return;
    api->clearSession();
    listCapturePath = capturePath;
    invalidateBook();
    keywordList = {};
    searchKeywords.clear();
    groupActive = true;
    emit searchResultsChanged();
}

void BookDownloadController::invalidateBook()
{
    if (isBusy())
        return;
    currentBook = {};
    emit bookChanged(currentBook);
    emit progressChanged(0, 0);
}

void BookDownloadController::invalidateSearch()
{
    if (isBusy())
        return;
    keywordList = {};
    searchKeywords.clear();
    if (!groupActive)
        emit searchResultsChanged();
}

void BookDownloadController::browseGroup(const QString &capturePath, bool refresh)
{
    if (isBusy())
        return;
    groupActive = true;
    listCapturePath = capturePath;
    if (!refresh && groupList.page > 0) {
        emit searchResultsChanged();
        emit statusChanged(QStringLiteral("已恢复分组缓存：%1 本，读取至第 %2 页；排序不重复请求已加载分页。%3")
                               .arg(groupList.books.size()).arg(groupList.page)
                               .arg(groupList.complete ? QStringLiteral("已加载全部返回结果。")
                                                       : QStringLiteral("数据未确认完整。")));
        return;
    }
    const auto error = api->importSession(listCapturePath);
    if (!error.isEmpty()) {
        emit searchResultsChanged();
        emit statusChanged(error);
        return;
    }
    groupList = {};
    emit searchResultsChanged();
    setMode(Mode::Search);
    emit progressChanged(0, 0);
    emit statusChanged(QStringLiteral("正在读取分类 %1 的第一页…%2").arg(NovelApi::groupTagId(), cacheWarning));
    cacheWarning.clear();
    api->searchBooks({}, 1);
}

void BookDownloadController::searchBooks(const QString &keywords, const QString &capturePath)
{
    if (isBusy())
        return;
    if (!NovelApi::isSearchKeywords(keywords)) {
        emit statusChanged(QStringLiteral("请输入 1–256 字符的书名或关键词，不支持控制字符。"));
        return;
    }
    if (keywordList.page > 0 && searchKeywords == keywords.trimmed()) {
        groupActive = false;
        listCapturePath = capturePath;
        emit searchResultsChanged();
        emit statusChanged(QStringLiteral("已恢复该关键词的搜索结果，未重新请求已加载分页。"));
        return;
    }
    const auto error = api->importSession(capturePath);
    if (!error.isEmpty()) {
        api->clearSession();
        emit statusChanged(error);
        return;
    }
    groupActive = false;
    keywordList = {};
    searchKeywords = keywords.trimmed();
    listCapturePath = capturePath;
    emit searchResultsChanged();
    setMode(Mode::Search);
    emit progressChanged(0, 0);
    emit statusChanged(QStringLiteral("正在搜索书名，第 1 页…"));
    api->searchBooks(searchKeywords, 1);
}

void BookDownloadController::loadMoreSearch()
{
    if (isBusy() || !activeList().hasMore)
        return;
    const auto error = api->hasSession() ? QString() : api->importSession(listCapturePath);
    setMode(Mode::Search);
    emit progressChanged(0, 0);
    if (!error.isEmpty()) {
        stop(error);
        return;
    }
    const auto page = activeList().page + 1;
    emit statusChanged(QStringLiteral("正在加载%1第 %2 页…")
                           .arg(groupActive ? QStringLiteral("分组") : QStringLiteral("搜索")).arg(page));
    api->searchBooks(groupActive ? QString() : searchKeywords, page);
}

void BookDownloadController::fetchBook(const QString &id, const QString &capturePath)
{
    requestBook(id, capturePath, {});
}

void BookDownloadController::downloadBook(const QString &id, const QString &capturePath, const QString &directory)
{
    if (isBusy())
        return;
    const QFileInfo root(directory);
    if (directory.isEmpty() || !root.isDir() || hasLinkedAncestor(root.absoluteFilePath())) {
        emit statusChanged(QStringLiteral("请选择存在的普通保存目录，不支持符号链接或目录联接。"));
        return;
    }
    requestBook(id, capturePath, root.canonicalFilePath());
}

void BookDownloadController::requestBook(const QString &id, const QString &capturePath, const QString &directory)
{
    if (isBusy())
        return;
    pendingDirectory.clear();
    currentBook = {};
    emit bookChanged(currentBook);
    emit progressChanged(0, 0);
    const auto error = api->importSession(capturePath);
    if (!error.isEmpty()) {
        api->clearSession();
        emit statusChanged(error);
        return;
    }
    pendingDirectory = directory;
    setMode(Mode::Catalog);
    emit statusChanged(QStringLiteral("本机密钥已加载，正在获取书籍目录…"));
    api->fetchBook(id);
}

void BookDownloadController::previewBook(const QString &id, const QString &capturePath)
{
    if (isBusy())
        return;
    const auto error = api->importSession(capturePath);
    if (!error.isEmpty()) {
        emit previewFinished(error);
        return;
    }
    previewBookData = {};
    decodeKey = api->chapterKey();
    setMode(Mode::PreviewCatalog);
    emit statusChanged(QStringLiteral("正在获取预览目录…"));
    api->fetchBook(id);
}

void BookDownloadController::nextPreviewChapter()
{
    const auto count = qMin(3, static_cast<int>(previewBookData.chapters.size()));
    if (previewIndex >= count) {
        finishTask(QStringLiteral("前三章预览处理完成；无权阅读的章节未获取，正文未写入磁盘。"));
        return;
    }
    const auto &chapter = previewBookData.chapters[previewIndex];
    if (!chapter.canRead) {
        emit previewChapterReady(previewIndex, {}, QStringLiteral("当前会话无免费阅读权限，未获取正文"));
        ++previewIndex;
        emit progressChanged(previewIndex, count);
        next.start(0);
        return;
    }
    emit statusChanged(QStringLiteral("正在加载预览 %1 / %2：%3")
                           .arg(previewIndex + 1).arg(count).arg(chapter.name));
    api->fetchChapter(chapter.id);
}

void BookDownloadController::fetchLinks()
{
    if (isBusy() || currentBook.chapters.isEmpty())
        return;
    current = 0;
    setMode(Mode::Links);
    emit progressChanged(0, static_cast<int>(currentBook.chapters.size()));
    next.start(0);
}

QString BookDownloadController::prepareDirectory(const QString &directory)
{
    const QFileInfo root(directory);
    if (!root.isDir() || hasLinkedAncestor(root.absoluteFilePath()))
        return QStringLiteral("请选择存在的普通保存目录，不支持符号链接或目录联接。");
    const QDir parent(root.canonicalFilePath());
    const auto name = safeName(currentBook.name, 64) + QStringLiteral("_") + currentBook.id;
    const auto path = parent.filePath(name);
    const QFileInfo target(path);
    if (target.isSymLink() || target.isJunction() || (target.exists() && !target.isDir()))
        return QStringLiteral("书籍保存位置不是普通目录，已拒绝写入。");
    if (!target.exists() && !parent.mkdir(name))
        return QStringLiteral("无法创建书籍保存目录。");
    bookDirectory = QFileInfo(path).canonicalFilePath();
    return checkDirectory();
}

QString BookDownloadController::checkDirectory() const
{
    const QFileInfo info(bookDirectory);
    if (bookDirectory.isEmpty() || !info.isDir() || hasLinkedAncestor(bookDirectory)
        || info.canonicalFilePath().compare(bookDirectory, Qt::CaseInsensitive) != 0)
        return QStringLiteral("书籍保存目录已变化或不安全，已停止。");
    return {};
}

QString BookDownloadController::chapterPath(int index) const
{
    const auto name = QStringLiteral("%1_%2.txt")
                          .arg(index + 1, 4, 10, QLatin1Char('0'))
                          .arg(safeName(currentBook.chapters[index].name, 90));
    return QDir(bookDirectory).filePath(name);
}

void BookDownloadController::startDownload(const QString &directory)
{
    const auto &key = api->chapterKey();
    if (key.size() != 16) {
        stop(QStringLiteral("本机正文密钥未就绪，请重新导入请求抓包。"));
        return;
    }
    const auto error = prepareDirectory(directory);
    if (!error.isEmpty()) {
        stop(error);
        return;
    }
    decodeKey = key;
    current = 0;
    saved = 0;
    skipped = 0;
    cancelling = false;
    setMode(Mode::Download);
    emit progressChanged(0, static_cast<int>(currentBook.chapters.size()));
    next.start(0);
}

void BookDownloadController::nextChapter()
{
    if (mode != Mode::Links && mode != Mode::Download)
        return;
    if (current >= currentBook.chapters.size()) {
        if (mode == Mode::Links)
            finishTask(QStringLiteral("全部 %1 条章节链接已获取。下载时仍会逐章刷新签名。")
                           .arg(currentBook.chapters.size()));
        else
            startMerge();
        return;
    }
    auto &chapter = currentBook.chapters[current];
    if (!chapter.canRead) {
        stop(QStringLiteral("章节“%1”不是免费可阅读状态，已停止。").arg(chapter.name));
        return;
    }
    if (mode == Mode::Download) {
        const auto error = checkDirectory();
        if (!error.isEmpty()) {
            stop(error);
            return;
        }
        const QFileInfo output(chapterPath(current));
        if (output.isSymLink() || output.isJunction() || (output.exists() && !output.isFile())) {
            stop(QStringLiteral("章节输出位置不是普通文件，已停止。"));
            return;
        }
        if (output.exists()) {
            updateChapter(QStringLiteral("已存在，跳过（未校验内容）"));
            ++skipped;
            ++current;
            emit progressChanged(current, static_cast<int>(currentBook.chapters.size()));
            next.start(0);
            return;
        }
    }
    updateChapter(QStringLiteral("正在获取新链接"));
    emit statusChanged(QStringLiteral("正在处理 %1 / %2：%3")
                           .arg(current + 1).arg(currentBook.chapters.size()).arg(chapter.name));
    api->fetchChapter(chapter.id);
}

void BookDownloadController::startMerge()
{
    const auto error = checkDirectory();
    if (!error.isEmpty()) {
        stop(error);
        return;
    }
    mergedFile = std::make_unique<QTemporaryFile>(
        QDir(bookDirectory).filePath(QStringLiteral(".NovDecoder-merge-XXXXXX.tmp")));
    if (!mergedFile->open() || mergedFile->write("\xEF\xBB\xBF", 3) != 3) {
        stop(QStringLiteral("无法创建整本 TXT 临时文件；单章 TXT 已保留。"));
        return;
    }
    decodeKey.fill('\0');
    decodeKey.clear();
    current = 0;
    setMode(Mode::Merge);
    emit progressChanged(0, static_cast<int>(currentBook.chapters.size()));
    emit statusChanged(QStringLiteral("章节下载完成，正在按目录合并带标题的整本 TXT…"));
    next.start(0);
}

void BookDownloadController::mergeNextChapter()
{
    const auto error = checkDirectory();
    if (!error.isEmpty()) {
        stop(error);
        return;
    }
    if (current >= currentBook.chapters.size()) {
        completeMerge();
        return;
    }

    const auto &chapter = currentBook.chapters[current];
    const QFileInfo info(chapterPath(current));
    if (!info.isFile() || info.isSymLink() || info.isJunction()) {
        stop(QStringLiteral("章节“%1”的 TXT 缺失或不是普通文件，未生成新合集。").arg(chapter.name));
        return;
    }
    QFile file(info.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > NovDecoder::MaxFileSize) {
        stop(QStringLiteral("无法读取章节“%1”，或 TXT 为空、超过 32 MiB，未生成新合集。").arg(chapter.name));
        return;
    }
    const auto bytes = file.read(NovDecoder::MaxFileSize + 1);
    if (file.error() != QFileDevice::NoError || bytes.size() > NovDecoder::MaxFileSize) {
        stop(QStringLiteral("读取章节“%1”失败，未生成新合集。").arg(chapter.name));
        return;
    }
    QStringDecoder decoder(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
    QString text = decoder(bytes);
    if (decoder.hasError() || text.contains(QChar::Null) || text.trimmed().isEmpty()) {
        stop(QStringLiteral("章节“%1”不是有效的非空 UTF-8 TXT，未生成新合集。").arg(chapter.name));
        return;
    }
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    qsizetype begin = 0;
    qsizetype end = text.size();
    while (begin < end && text[begin] == QLatin1Char('\n'))
        ++begin;
    while (end > begin && text[end - 1] == QLatin1Char('\n'))
        --end;
    qsizetype first = begin;
    while (first < end && text[first].isSpace())
        ++first;
    auto lineEnd = text.indexOf(QLatin1Char('\n'), first);
    if (lineEnd < 0 || lineEnd > end)
        lineEnd = end;
    if (text.sliced(first, lineEnd - first).trimmed() == chapter.name) {
        begin = lineEnd;
        while (begin < end && text[begin] == QLatin1Char('\n'))
            ++begin;
    }
    if (text.sliced(begin, end - begin).trimmed().isEmpty()) {
        stop(QStringLiteral("章节“%1”只有标题、缺少正文，未生成新合集。").arg(chapter.name));
        return;
    }
    auto section = chapter.name + QStringLiteral("\n\n") + text.sliced(begin, end - begin)
        + (current + 1 < currentBook.chapters.size() ? QStringLiteral("\n\n\n") : QStringLiteral("\n"));
    section.replace(QStringLiteral("\n"), QStringLiteral("\r\n"));
    const auto encoded = section.toUtf8();
    if (mergedFile->write(encoded) != encoded.size()) {
        stop(QStringLiteral("整本 TXT 写入失败；单章 TXT 已保留。"));
        return;
    }
    updateChapter(QStringLiteral("已合并（UTF-8 已校验）"));
    ++current;
    emit progressChanged(current, static_cast<int>(currentBook.chapters.size()));
    emit statusChanged(QStringLiteral("正在合并 %1 / %2：%3")
                           .arg(current).arg(currentBook.chapters.size()).arg(chapter.name));
    next.start(0);
}

void BookDownloadController::completeMerge()
{
    if (!mergedFile->flush()) {
        stop(QStringLiteral("整本 TXT 保存失败；单章 TXT 已保留。"));
        return;
    }
    mergedFile->close();
    const auto stem = safeName(currentBook.name, 64) + QStringLiteral("_合并版");
    for (int number = 1; number <= 10000; ++number) {
        const auto suffix = number == 1 ? QString() : QStringLiteral("_%1").arg(number);
        const auto path = QDir(bookDirectory).filePath(stem + suffix + QStringLiteral(".txt"));
        const QFileInfo output(path);
        if (output.exists() || output.isSymLink() || output.isJunction())
            continue;
        if (!mergedFile->rename(path)) {
            const QFileInfo collision(path);
            if (collision.exists() || collision.isSymLink() || collision.isJunction())
                continue;
            stop(QStringLiteral("无法发布整本 TXT，未覆盖任何已有文件；单章 TXT 已保留。"));
            return;
        }
        mergedFile->setAutoRemove(false);
        mergedFile.reset();
        finishTask(QStringLiteral("下载并合并完成：%1 条目录记录，新下载 %2 章，复用 %3 个已有 TXT。\n"
                                  "合并版：%4\nUTF-8 BOM · 每章含标题 · 所有单章文件保留。")
                       .arg(currentBook.chapters.size()).arg(saved).arg(skipped)
                       .arg(QDir::toNativeSeparators(path)));
        return;
    }
    stop(QStringLiteral("已有合并版文件过多，请整理保存目录后重试；单章 TXT 已保留。"));
}

void BookDownloadController::updateChapter(const QString &status)
{
    if (current >= currentBook.chapters.size())
        return;
    auto &chapter = currentBook.chapters[current];
    chapter.status = status;
    emit chapterChanged(current, chapter.content, status);
}

void BookDownloadController::stop(const QString &message)
{
    next.stop();
    api->cancel();
    if (mode == Mode::Links || mode == Mode::Download || mode == Mode::Merge)
        updateChapter(QStringLiteral("已停止"));
    finishTask(message + (isPreviewing()
        ? QStringLiteral("\n预览已停止，不会自动重试，已加载内容保留至关闭弹窗。")
        : mode == Mode::Search ? QStringLiteral("\n已加载的列表保留，不会自动重试。")
        : QStringLiteral("\n已完成的单章和链接保留，不会生成不完整的新合集，不会自动重试。")));
}

void BookDownloadController::finishTask(const QString &message)
{
    const bool preview = isPreviewing();
    next.stop();
    pendingDirectory.clear();
    mergedFile.reset();
    decodeKey.fill('\0');
    decodeKey.clear();
    cancelling = false;
    if (preview)
        previewBookData = {};
    setMode(Mode::Idle);
    emit statusChanged(message);
    if (preview)
        emit previewFinished(message);
}

void BookDownloadController::cancel()
{
    if (!isBusy())
        return;
    next.stop();
    api->cancel();
    if (downloader->isBusy()) {
        cancelling = true;
        downloader->cancel();
    } else {
        if (mode == Mode::Links || mode == Mode::Download || mode == Mode::Merge)
            updateChapter(QStringLiteral("已取消"));
        finishTask(isPreviewing() ? QStringLiteral("预览已取消，正文未写入磁盘。")
            : mode == Mode::Search ? QStringLiteral("列表加载已取消，已加载结果保留。")
            : QStringLiteral("已取消，已完成的单章和链接保留，未生成新的合并版。"));
    }
}
