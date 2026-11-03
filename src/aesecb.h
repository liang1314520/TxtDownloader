#pragma once

#include <QByteArray>
#include <QString>

namespace AesEcb {

struct Result {
    QByteArray bytes;
    QString error;

    bool ok() const { return error.isEmpty(); }
};

Result encrypt(const QByteArray &plaintext, QByteArray key);
Result decrypt(const QByteArray &encrypted, QByteArray key);

}
