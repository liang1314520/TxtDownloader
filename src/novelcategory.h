#pragma once

#include <QJsonObject>
#include <QString>

struct NovelCategory {
    QString name;
    QString tagId;
    QString adCode;
    QString position;

    QJsonObject parameters() const;
    QString key() const;
    bool isValid() const;
    static NovelCategory defaultCategory();
    static QString fromRequestData(const QJsonObject &data, NovelCategory &category);
};
