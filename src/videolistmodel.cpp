#include "videolistmodel.h"
#include "videocover.h"

#include <QMouseEvent>
#include <QPainter>
#include <QStyle>

namespace {
QRect favoriteRect(const QRect &card) { return QRect(card.right() - 96, card.top() + 80, 80, 28); }
QString durationText(qint64 seconds)
{
    return QStringLiteral("%1:%2:%3").arg(seconds / 3600).arg((seconds / 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(seconds % 60, 2, 10, QLatin1Char('0'));
}
}
VideoListModel::VideoListModel(VideoCoverCache *cache, QObject *parent)
    : QAbstractListModel(parent), covers(cache)
{
    connect(covers, &VideoCoverCache::thumbnailReady, this, &VideoListModel::thumbnailChanged);
}
void VideoListModel::setItems(const QVector<VideoApi::Summary> &updated)
{
    beginResetModel();
    items = updated;
    endResetModel();
}
void VideoListModel::setFavorites(const QSet<QString> &ids)
{
    const auto changed = (favorites - ids) | (ids - favorites);
    favorites = ids;
    for (qsizetype row = 0; row < items.size(); ++row) {
        if (changed.contains(items[row].id))
            emit dataChanged(index(static_cast<int>(row)), index(static_cast<int>(row)), {FavoriteRole});
    }
}
void VideoListModel::thumbnailChanged(const QString &key)
{
    for (qsizetype row = 0; row < items.size(); ++row) {
        if (items[row].thumbnailKey == key)
            emit dataChanged(index(static_cast<int>(row)), index(static_cast<int>(row)), {ThumbnailRole});
    }
}
VideoApi::Summary VideoListModel::item(const QModelIndex &index) const
{
    return index.isValid() && index.model() == this && index.row() >= 0 && index.row() < items.size()
        ? items[index.row()] : VideoApi::Summary();
}
int VideoListModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : static_cast<int>(items.size()); }
QVariant VideoListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= items.size())
        return {};
    const auto &item = items[index.row()];
    switch (role) {
    case Qt::DisplayRole:
    case TitleRole: return item.title;
    case IdRole: return item.id;
    case DurationRole: return item.durationSeconds;
    case ViewsRole: return item.views;
    case CategoryRole: return item.categoryText;
    case FavoriteRole: return favorites.contains(item.id);
    case ThumbnailRole: return covers->cached(item.thumbnailKey);
    default: return {};
    }
}
VideoCardDelegate::VideoCardDelegate(QObject *parent) : QStyledItemDelegate(parent) {}
void VideoCardDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    const auto card = option.rect.adjusted(2, 3, -2, -3);
    painter->setPen(QColor(QStringLiteral("#dde5ef")));
    painter->setBrush(option.state.testFlag(QStyle::State_Selected) ? QColor(QStringLiteral("#eaf2ff")) : Qt::white);
    painter->drawRoundedRect(card, 10, 10);
    const QRect cover(card.left() + 12, card.top() + 12, 144, 96);
    const auto image = index.data(VideoListModel::ThumbnailRole).value<QImage>();
    painter->fillRect(cover, QColor(QStringLiteral("#edf1f6")));
    if (image.isNull()) {
        painter->setPen(QColor(QStringLiteral("#7c8b9e")));
        painter->drawText(cover, Qt::AlignCenter, QStringLiteral("本机暂无封面"));
    } else {
        const auto size = image.size().scaled(cover.size(), Qt::KeepAspectRatio);
        painter->drawImage(QRect(cover.center() - QPoint(size.width() / 2, size.height() / 2), size), image);
    }
    const int left = cover.right() + 16;
    const int width = qMax(0, card.right() - left - 14);
    auto font = option.font;
    font.setBold(true);
    painter->setFont(font);
    painter->setPen(QColor(QStringLiteral("#25374c")));
    painter->drawText(QRect(left, card.top() + 14, width, 26), Qt::AlignLeft | Qt::AlignVCenter,
                      QFontMetrics(font).elidedText(index.data(VideoListModel::TitleRole).toString(), Qt::ElideRight, width));
    font.setBold(false);
    painter->setFont(font);
    painter->setPen(QColor(QStringLiteral("#63768c")));
    const auto metadata = QStringLiteral("时长 %1 · 浏览 %2").arg(durationText(index.data(VideoListModel::DurationRole).toLongLong()))
        .arg(index.data(VideoListModel::ViewsRole).toLongLong());
    painter->drawText(QRect(left, card.top() + 44, width, 24), Qt::AlignVCenter, metadata);
    painter->drawText(QRect(left, card.top() + 80, qMax(0, width - 90), 28), Qt::AlignVCenter,
        option.fontMetrics.elidedText(index.data(VideoListModel::CategoryRole).toString(), Qt::ElideRight, qMax(0, width - 90)));
    const auto favorite = favoriteRect(card);
    painter->setPen(QColor(QStringLiteral("#386fc3")));
    painter->drawRoundedRect(favorite, 5, 5);
    painter->drawText(favorite, Qt::AlignCenter, index.data(VideoListModel::FavoriteRole).toBool()
        ? QStringLiteral("已收藏") : QStringLiteral("收藏"));
    painter->restore();
}
QSize VideoCardDelegate::sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const { return QSize(390, 126); }
bool VideoCardDelegate::editorEvent(QEvent *event, QAbstractItemModel *, const QStyleOptionViewItem &option,
                                     const QModelIndex &index)
{
    if (event->type() != QEvent::MouseButtonRelease)
        return false;
    const auto *mouse = static_cast<QMouseEvent *>(event);
    if (mouse->button() != Qt::LeftButton || !favoriteRect(option.rect.adjusted(2, 3, -2, -3)).contains(mouse->position().toPoint()))
        return false;
    emit favoriteRequested(index);
    return true;
}
