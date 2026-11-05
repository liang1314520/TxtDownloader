#include "pcaprequestreader.h"

#include <QtEndian>

#include <limits>

namespace {
constexpr qsizetype MaxRequest = 1024 * 1024;
constexpr qsizetype MaxHeaders = 16 * 1024;
constexpr qsizetype MaxPacket = 256 * 1024;
constexpr qsizetype MaxConnections = 32;
constexpr qsizetype MaxPendingSegments = 128;
constexpr qsizetype MaxBuffered = 4 * 1024 * 1024;
}

void PcapRequestReader::reset()
{
    buffer.clear();
    streams.clear();
    hasHeader = false;
    snapshotLength = 0;
    error.clear();
}

quint32 PcapRequestReader::word32(const char *data) const
{
    return littleEndian ? qFromLittleEndian<quint32>(data) : qFromBigEndian<quint32>(data);
}

quint16 PcapRequestReader::word16(const char *data) const
{
    return littleEndian ? qFromLittleEndian<quint16>(data) : qFromBigEndian<quint16>(data);
}

QList<QByteArray> PcapRequestReader::append(const QByteArray &bytes)
{
    QList<QByteArray> requests;
    if (!error.isEmpty())
        return requests;
    if (buffer.size() + bytes.size() > MaxBuffered) {
        error = QStringLiteral("采集解析缓冲超过 4 MiB，已停止。");
        return requests;
    }
    buffer.append(bytes);
    if (!hasHeader) {
        if (buffer.size() < 24)
            return requests;
        const auto magic = buffer.first(4).toHex();
        littleEndian = magic == "d4c3b2a1" || magic == "4d3cb2a1";
        if (!littleEndian && magic != "a1b2c3d4" && magic != "a1b23c4d") {
            error = QStringLiteral("不支持此网络记录格式；需要 classic PCAP。");
            return requests;
        }
        snapshotLength = word32(buffer.constData() + 16);
        if (word16(buffer.constData() + 4) != 2 || word16(buffer.constData() + 6) != 4
            || word32(buffer.constData() + 20) != 1 || snapshotLength < 54 || snapshotLength > MaxPacket) {
            error = QStringLiteral("网络记录头不受支持；需要 Ethernet PCAP 2.4。");
            return requests;
        }
        buffer.remove(0, 24);
        hasHeader = true;
    }
    for (auto it = streams.begin(); it != streams.end();) {
        parseHttp(it.value(), requests);
        if (it->closed && (it->blocked || (it->data.isEmpty() && it->pending.isEmpty())))
            it = streams.erase(it);
        else
            ++it;
        if (requests.size() >= 8)
            break;
    }
    while (buffer.size() >= 16 && requests.size() < 8) {
        const auto included = word32(buffer.constData() + 8);
        const auto original = word32(buffer.constData() + 12);
        if (included > snapshotLength || included > MaxPacket || included > original) {
            error = QStringLiteral("网络记录中的包长度无效，已停止。");
            break;
        }
        if (buffer.size() < 16 + static_cast<qsizetype>(included))
            break;
        const auto frame = buffer.sliced(16, included);
        buffer.remove(0, 16 + included);
        if (included == original)
            packet(frame, requests);
        if (!error.isEmpty())
            break;
    }
    qsizetype memory = buffer.size();
    for (const auto &stream : streams) {
        memory += stream.data.size();
        for (const auto &segment : stream.pending)
            memory += segment.size();
    }
    if (memory > MaxBuffered)
        error = QStringLiteral("TCP 重组超过 4 MiB 限制，已停止。");
    return requests;
}

void PcapRequestReader::packet(const QByteArray &frame, QList<QByteArray> &requests)
{
    if (frame.size() < 14)
        return;
    qsizetype ipOffset = 14;
    auto type = qFromBigEndian<quint16>(frame.constData() + 12);
    if (type == 0x8100 || type == 0x88a8) {
        if (frame.size() < 18)
            return;
        type = qFromBigEndian<quint16>(frame.constData() + 16);
        ipOffset = 18;
    }
    if (type != 0x0800 || frame.size() < ipOffset + 20)
        return;
    const auto *ip = reinterpret_cast<const unsigned char *>(frame.constData() + ipOffset);
    const auto ipHeader = static_cast<qsizetype>((ip[0] & 15) * 4);
    const auto total = qFromBigEndian<quint16>(ip + 2);
    if (ip[0] >> 4 != 4 || ipHeader < 20 || total < ipHeader + 20
        || frame.size() < ipOffset + total || ip[9] != 6
        || (qFromBigEndian<quint16>(ip + 6) & 0x3fff) != 0)
        return;
    const auto *tcp = ip + ipHeader;
    const auto tcpHeader = static_cast<qsizetype>((tcp[12] >> 4) * 4);
    if (qFromBigEndian<quint16>(tcp + 2) != 80 || tcpHeader < 20 || ipHeader + tcpHeader > total)
        return;
    QByteArray key(reinterpret_cast<const char *>(ip + 12), 8);
    key.append(reinterpret_cast<const char *>(tcp), 4);
    if (!streams.contains(key) && streams.size() >= MaxConnections) {
        error = QStringLiteral("采集期间 HTTP 连接超过 32 条，请关闭模拟器内其他应用后重试。");
        return;
    }
    auto &stream = streams[key];
    const auto flags = tcp[13];
    auto sequence = qFromBigEndian<quint32>(tcp + 4);
    if (flags & 0x02) {
        stream = {};
        ++sequence;
        stream.nextSequence = sequence;
        stream.initialized = true;
    }
    auto payload = QByteArray(reinterpret_cast<const char *>(tcp + tcpHeader), total - ipHeader - tcpHeader);
    if (!stream.initialized) {
        stream.initialized = true;
        stream.nextSequence = sequence;
    }
    if (flags & 0x05)
        stream.closed = true;
    if (stream.blocked || payload.isEmpty()) {
        if (stream.closed && (stream.blocked || (stream.data.isEmpty() && stream.pending.isEmpty())))
            streams.remove(key);
        return;
    }
    if (sequence < stream.nextSequence) {
        const quint32 consumed = stream.nextSequence - sequence;
        if (consumed >= static_cast<quint32>(payload.size()))
            return;
        payload.remove(0, consumed);
        sequence = stream.nextSequence;
    }
    if (sequence > stream.nextSequence) {
        if (sequence - stream.nextSequence > MaxRequest || stream.pending.size() >= MaxPendingSegments) {
            stream.blocked = true;
            stream.pending.clear();
            stream.data.clear();
            return;
        }
        if (!stream.pending.contains(sequence) || stream.pending[sequence].size() < payload.size())
            stream.pending[sequence] = payload;
        return;
    }
    for (;;) {
        if (stream.data.size() + payload.size() > MaxRequest
            || static_cast<quint64>(stream.nextSequence) + payload.size() > std::numeric_limits<quint32>::max()) {
            stream.blocked = true;
            stream.data.clear();
            stream.pending.clear();
            return;
        }
        stream.data.append(payload);
        stream.nextSequence += static_cast<quint32>(payload.size());
        parseHttp(stream, requests);
        if (stream.blocked || stream.pending.isEmpty() || stream.pending.firstKey() > stream.nextSequence)
            break;
        const auto pendingSequence = stream.pending.firstKey();
        payload = stream.pending.take(pendingSequence);
        const auto consumed = stream.nextSequence - pendingSequence;
        if (consumed >= static_cast<quint32>(payload.size())) {
            payload.clear();
            continue;
        }
        payload.remove(0, consumed);
    }
    if (stream.closed && (stream.blocked || (stream.data.isEmpty() && stream.pending.isEmpty())))
        streams.remove(key);
}

void PcapRequestReader::parseHttp(Stream &stream, QList<QByteArray> &requests)
{
    while (!stream.data.isEmpty() && !stream.blocked && requests.size() < 8) {
        const auto separator = stream.data.indexOf("\r\n\r\n");
        if (separator < 0) {
            if (stream.data.size() > MaxHeaders)
                stream.blocked = true;
            return;
        }
        if (separator > MaxHeaders) {
            stream.blocked = true;
            break;
        }
        const auto lines = stream.data.first(separator).split('\n');
        const auto line = lines.first().trimmed();
        const auto parts = line.split(' ');
        if (parts.size() != 3 || (parts[2] != "HTTP/1.1" && parts[2] != "HTTP/1.0")) {
            stream.blocked = true;
            break;
        }
        QMap<QByteArray, QByteArray> headers;
        for (qsizetype index = 1; index < lines.size(); ++index) {
            const auto header = lines[index].trimmed();
            const auto colon = header.indexOf(':');
            const auto name = header.first(qMax<qsizetype>(0, colon)).toLower();
            if (colon <= 0 || headers.contains(name)) {
                stream.blocked = true;
                break;
            }
            headers.insert(name, header.sliced(colon + 1).trimmed());
        }
        if (stream.blocked || headers.contains("transfer-encoding")) {
            stream.blocked = true;
            break;
        }
        bool valid = false;
        const auto length = headers.value("content-length").toLongLong(&valid);
        const bool noBody = !headers.contains("content-length") && (parts[0] == "GET" || parts[0] == "HEAD");
        if ((!valid && !noBody) || length < 0 || length > MaxRequest - separator - 4) {
            stream.blocked = true;
            break;
        }
        const auto size = separator + 4 + length;
        if (stream.data.size() < size)
            return;
        auto path = parts[1];
        const QByteArray prefix("http://yj.kkj234ls.com");
        if (path.startsWith(prefix + '/'))
            path.remove(0, prefix.size());
        const bool target = parts[0] == "POST" && parts[2] == "HTTP/1.1" && length > 0
            && headers.value("host").toLower() == "yj.kkj234ls.com"
            && headers.value("content-type").toLower() == "application/octet-stream"
            && (path == "/v1/xs/ss" || path == "/v1/xs/xq" || path == "/v1/xs/zjxq");
        if (target) {
            auto request = stream.data.first(size);
            if (parts[1] != path) {
                const auto end = request.indexOf("\r\n");
                request.replace(0, end, "POST " + path + " HTTP/1.1");
            }
            requests.append(request);
        }
        stream.data.remove(0, size);
    }
    if (stream.blocked) {
        stream.data.clear();
        stream.pending.clear();
    }
}
