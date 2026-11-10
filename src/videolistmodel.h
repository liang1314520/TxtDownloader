#pragma once

#include "videoapitypes.h"

#include <QAbstractListModel>
#include <QSet>
#include <QStyledItemDelegate>

class VideoCoverCache;
class VideoListModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role { IdRole = Qt::UserRole + 1, TitleRole, DurationRole, ViewsRole,
                ThumbnailRole, FavoriteRole, CategoryRole };
    explicit VideoListModel(VideoCoverCache *covers, QObject *parent = nullptr);
    void setItems(const QVector<VideoApi::Summary> &items);
    void setFavorites(const QSet<QString> &ids);
    void thumbnailChanged(const QString &key);
    VideoApi::Summary item(const QModelIndex &index) const;
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
private:
    VideoCoverCache *covers;
    QVector<VideoApi::Summary> items;
    QSet<QString> favorites;
};
class VideoCardDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit VideoCardDelegate(QObject *parent = nullptr);
    void paint(QPainter *, const QStyleOptionViewItem &, const QModelIndex &) const override;
    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override;
    bool editorEvent(QEvent *, QAbstractItemModel *, const QStyleOptionViewItem &, const QModelIndex &) override;
signals:
    void favoriteRequested(const QModelIndex &index);
};
