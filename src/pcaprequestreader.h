#pragma once

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QString>

class PcapRequestReader {
public:
    QList<QByteArray> append(const QByteArray &bytes);
    QString errorString() const { return error; }
    void reset();

private:
    struct Stream {
        quint32 nextSequence = 0;
        bool initialized = false;
        bool blocked = false;
        bool closed = false;
        QByteArray data;
        QMap<quint32, QByteArray> pending;
    };
    void packet(const QByteArray &frame, QList<QByteArray> &requests);
    void parseHttp(Stream &stream, QList<QByteArray> &requests);
    quint32 word32(const char *data) const;
    quint16 word16(const char *data) const;

    QByteArray buffer;
    QMap<QByteArray, Stream> streams;
    bool hasHeader = false;
    bool littleEndian = true;
    quint32 snapshotLength = 0;
    QString error;
};
