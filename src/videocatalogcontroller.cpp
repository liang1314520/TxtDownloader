#include "videocatalogcontroller.h"
#include "videoclient.h"

#include <QSet>

#include <algorithm>

VideoCatalogController::VideoCatalogController(const VideoStoragePaths &storage, QObject *parent)
    : QObject(parent), paths(storage)
{
    QStringList errors;
    for (const auto &error : {VideoStore::loadCategories(paths, sourceCategories),
                              VideoStore::loadFavorites(paths, savedFavorites),
                              VideoStore::loadOrder(paths, order)}) {
        if (!error.isEmpty())
            errors.append(error);
    }
    order = VideoStore::mergedOrder(sourceCategories, order);
    const auto cacheError = VideoStore::loadSnapshot(paths, categoryState.query, categoryState);
    if (!cacheError.isEmpty())
        errors.append(cacheError);
    initialError = errors.join(QLatin1Char('\n'));
    client = new VideoClient(paths, this);
    connect(client, &VideoClient::finished, this, &VideoCatalogController::receive);
    connect(client, &VideoClient::sessionChanged, this, &VideoCatalogController::connectionChanged);
}
VideoCatalogController::~VideoCatalogController() { cancel(); }
bool VideoCatalogController::isFavorite(const QString &id) const
{
    return std::any_of(savedFavorites.cbegin(), savedFavorites.cend(), [&id](const VideoApi::Summary &item) { return item.id == id; });
}
bool VideoCatalogController::hasSession() const
{
    VideoSession session;
    return VideoStore::loadSession(paths, session).isEmpty() && !session.token.isEmpty();
}
QString VideoCatalogController::selectCachedCategory(const QString &parentId, const QString &childId)
{
    if (busy)
        return QStringLiteral("视频请求进行中，请先取消再切换缓存。 ");
    VideoApi::Query query;
    auto error = VideoApi::categoryQuery(sourceCategories, parentId, childId, query);
    if (!error.isEmpty())
        return error;
    VideoSnapshot snapshot;
    error = VideoStore::loadSnapshot(paths, query, snapshot);
    if (!error.isEmpty())
        return error;
    categoryState = std::move(snapshot);
    emit categoryResultsChanged();
    return {};
}
QString VideoCatalogController::setFavorite(const VideoApi::Summary &item, bool favorite)
{
    QVector<VideoApi::Summary> committed;
    const auto error = VideoStore::updateFavorite(paths, item, favorite, committed);
    if (!error.isEmpty())
        return error;
    savedFavorites = std::move(committed);
    emit favoritesChanged();
    return {};
}
QString VideoCatalogController::saveOrder(const VideoCategoryOrder &newOrder)
{
    if (busy)
        return QStringLiteral("视频请求进行中，暂不能修改分类排序。 ");
    const auto error = VideoStore::saveOrder(paths, sourceCategories, newOrder);
    if (!error.isEmpty())
        return error;
    order = newOrder;
    emit categoriesChanged();
    return {};
}
QString VideoCatalogController::clearSession()
{
    if (busy)
        return QStringLiteral("视频请求进行中，请先取消再清除会话。 ");
    VideoSession committed;
    const auto error = VideoStore::clearSessionToken(paths, committed);
    if (!error.isEmpty())
        return error;
    consentGranted = false;
    client->setConsentGranted(false);
    emit connectionChanged();
    return {};
}
void VideoCatalogController::showSavedDetails(const VideoApi::Summary &item)
{
    if (!VideoApi::validateSummary(item).isEmpty() || currentDetails.id == item.id)
        return;
    currentDetails = {};
    currentDetails.id = item.id;
    currentDetails.title = item.title;
    currentDetails.viewsText = QString::number(item.views);
    emit detailsChanged();
}
void VideoCatalogController::setBusy(bool value)
{
    if (busy == value)
        return;
    busy = value;
    emit busyChanged(value);
}
void VideoCatalogController::start(Pending request)
{
    if (!consentGranted) {
        consentRequest = request;
        awaitingConsent = true;
        emit consentRequired();
        return;
    }
    dispatch(request);
}
void VideoCatalogController::dispatch(Pending request)
{
    cancel();
    request.generation = generation;
    pending = request;
    setBusy(true);
    VideoClient::Operation operation = VideoClient::Operation::Categories;
    switch (request.operation) {
    case Operation::Categories: break;
    case Operation::List: operation = VideoClient::Operation::List; break;
    case Operation::Search: operation = VideoClient::Operation::Search; break;
    case Operation::Details: operation = VideoClient::Operation::Details; break;
    case Operation::Session: operation = VideoClient::Operation::Session; break;
    }
    pending.requestId = client->request(operation, request.query, request.page, request.detailId);
}
void VideoCatalogController::grantConsent(bool granted)
{
    consentGranted = granted;
    client->setConsentGranted(granted);
    const auto request = consentRequest;
    const bool resume = awaitingConsent;
    awaitingConsent = false;
    if (granted && resume)
        dispatch(request);
}
void VideoCatalogController::retrySession()
{
    Pending request;
    request.operation = Operation::Session;
    start(request);
}
void VideoCatalogController::refreshCategories()
{
    Pending request;
    request.operation = Operation::Categories;
    start(request);
}
void VideoCatalogController::loadCategory(const QString &parentId, const QString &childId)
{
    Pending request;
    request.operation = Operation::List;
    const auto error = VideoApi::categoryQuery(sourceCategories, parentId, childId, request.query);
    if (!error.isEmpty()) {
        emit errorOccurred(error);
        return;
    }
    start(request);
}
void VideoCatalogController::search(const QString &keyword, VideoApi::SearchPart part)
{
    Pending request;
    request.operation = Operation::Search;
    request.query.kind = VideoApi::QueryKind::Search;
    request.query.keyword = keyword.trimmed();
    request.query.part = part;
    const auto error = VideoApi::validateQuery(request.query, 1);
    if (!error.isEmpty()) {
        emit errorOccurred(error);
        return;
    }
    start(request);
}
void VideoCatalogController::loadMore(VideoApi::QueryKind kind)
{
    if (busy)
        return;
    const auto &snapshot = kind == VideoApi::QueryKind::Category ? categoryState : searchState;
    if (snapshot.page < 1 || !snapshot.hasMore)
        return;
    Pending request;
    request.operation = kind == VideoApi::QueryKind::Category ? Operation::List : Operation::Search;
    request.query = snapshot.query;
    request.page = snapshot.page + 1;
    start(request);
}
void VideoCatalogController::refreshDetails(const QString &id)
{
    if (!VideoApi::validateId(id).isEmpty()) {
        emit errorOccurred(QStringLiteral("请先选择有效的视频条目。 "));
        return;
    }
    Pending request;
    request.operation = Operation::Details;
    request.detailId = id;
    start(request);
}
void VideoCatalogController::cancel()
{
    ++generation;
    awaitingConsent = false;
    pending.requestId = 0;
    if (client)
        client->cancelAll();
    setBusy(false);
}
QString VideoCatalogController::categoryText(const VideoApi::Query &query) const
{
    for (const auto &parent : sourceCategories) {
        if (parent.id != query.parentId)
            continue;
        if (query.childId.isEmpty())
            return parent.name;
        for (const auto &child : parent.children) {
            if (child.id == query.childId)
                return child.name;
        }
    }
    return QStringLiteral("全部分类");
}
void VideoCatalogController::receive(quint64 requestId, const QByteArray &body, const QString &networkError)
{
    if (!busy || requestId != pending.requestId || pending.generation != generation)
        return;
    const auto request = pending;
    QString error = networkError;
    if (error.isEmpty() && request.operation == Operation::Categories) {
        QVector<VideoApi::Category> loaded;
        error = VideoApi::parseCategories(body, loaded);
        if (error.isEmpty())
            error = VideoStore::saveCategories(paths, loaded);
        if (error.isEmpty()) {
            sourceCategories = std::move(loaded);
            order = VideoStore::mergedOrder(sourceCategories, order);
            VideoApi::Query current;
            const auto &selected = categoryState.query;
            if (!VideoApi::categoryQuery(sourceCategories, selected.parentId, selected.childId, current).isEmpty()
                || current.key() != selected.key()) {
                VideoSnapshot fallback;
                const auto cacheError = VideoStore::loadSnapshot(paths, {}, fallback);
                categoryState = std::move(fallback);
                if (!cacheError.isEmpty())
                    error = cacheError;
                emit categoryResultsChanged();
            }
            emit categoriesChanged();
        }
    } else if (error.isEmpty() && request.operation == Operation::Details) {
        VideoApi::Details loaded;
        error = VideoApi::parseDetails(body, request.detailId, loaded);
        if (error.isEmpty() && currentDetails.id == request.detailId) {
            currentDetails = std::move(loaded);
            emit detailsChanged();
        }
    } else if (error.isEmpty() && request.operation == Operation::Session) {
        emit connectionChanged();
    } else if (error.isEmpty()) {
        VideoApi::Page page;
        const bool search = request.operation == Operation::Search;
        error = search ? VideoApi::parseSearch(body, page) : VideoApi::parseList(body, page);
        auto &state = search ? searchState : categoryState;
        if (error.isEmpty() && request.page > 1
            && (state.query.key() != request.query.key() || state.page + 1 != request.page))
            error = QStringLiteral("视频分页状态已改变，未采用过时结果。 ");
        if (error.isEmpty()) {
            VideoSnapshot updated = request.page == 1 ? VideoSnapshot() : state;
            updated.query = request.query;
            updated.page = request.page;
            updated.updatedAt = QDateTime::currentDateTimeUtc();
            updated.complete = page.items.isEmpty();
            updated.hasMore = !updated.complete;
            QSet<QString> ids;
            for (const auto &item : updated.items)
                ids.insert(item.id);
            const auto previous = updated.items.size();
            for (auto item : page.items) {
                if (!ids.contains(item.id) && updated.items.size() < VideoApi::MaxItems) {
                    if (!search)
                        item.categoryText = categoryText(request.query);
                    updated.items.append(item);
                    ids.insert(item.id);
                }
            }
            if (!page.items.isEmpty() && (updated.items.size() == previous || updated.items.size() == VideoApi::MaxItems
                                          || request.page == 10000)) {
                updated.hasMore = false;
                updated.complete = false;
                error = QStringLiteral("遇到重复页或本机上限，已停止加载；未确认完整。 ");
            }
            const auto cacheError = VideoStore::saveSnapshot(paths, updated);
            state = std::move(updated);
            if (search)
                emit searchResultsChanged();
            else
                emit categoryResultsChanged();
            if (!cacheError.isEmpty())
                error += cacheError;
        }
    }
    pending.requestId = 0;
    setBusy(false);
    if (!error.isEmpty())
        emit errorOccurred(error);
}
