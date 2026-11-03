#pragma once

#include <QByteArray>
#include <QString>

namespace LocalKeyProfile {

QString load(const QString &version, QByteArray &apiKey, QByteArray &chapterKey);

}
