#include "trace/TraceImporter.h"

#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>
#include <QtGlobal>
#include <QtMath>

#include <algorithm>
#include <cstring>

using namespace CANManager;

namespace {

bool parseChannelToken(const QString& token, uint8_t& channelOut)
{
    QString digits;
    digits.reserve(token.size());
    for (const QChar ch : token) {
        if (ch.isDigit())
            digits.append(ch);
    }

    bool ok = false;
    const int channel = digits.toInt(&ok);
    if (!ok || channel <= 0 || channel > 255)
        return false;

    channelOut = static_cast<uint8_t>(channel);
    return true;
}

bool parseCanIdToken(QString token, uint32_t& idOut, bool& isExtendedOut)
{
    token = token.trimmed();
    if (token.isEmpty())
        return false;

    isExtendedOut = token.endsWith('x', Qt::CaseInsensitive);
    if (isExtendedOut)
        token.chop(1);

    if (token.endsWith('h', Qt::CaseInsensitive))
        token.chop(1);

    if (token.startsWith("0x", Qt::CaseInsensitive))
        token = token.mid(2);

    bool ok = false;
    const quint64 id = token.toULongLong(&ok, 16);
    if (!ok || id > 0x1FFFFFFFu)
        return false;

    // Some logs omit the explicit 'x' suffix for 29-bit IDs.
    if (!isExtendedOut && id > 0x7FFu)
        isExtendedOut = true;

    idOut = static_cast<uint32_t>(id);
    return true;
}

bool parseByteToken(QString token, quint8& outByte)
{
    token = token.trimmed();
    if (token.startsWith("0x", Qt::CaseInsensitive))
        token = token.mid(2);

    bool ok = false;
    const uint value = token.toUInt(&ok, 16);
    if (!ok || value > 0xFFu)
        return false;

    outByte = static_cast<quint8>(value);
    return true;
}

bool parseDlcToken(const QString& token, bool isFd, quint8& outDlc)
{
    bool ok = false;
    int value = token.toInt(&ok, 10);
    if (!ok) {
        value = token.toInt(&ok, 16);
        if (!ok)
            return false;
    }
    if (value < 0)
        return false;

    if (isFd) {
        if (value <= 15)
            outDlc = static_cast<quint8>(value);
        else
            outDlc = CANManager::lengthToDlc(value);
    } else {
        outDlc = static_cast<quint8>(qBound(0, value, 8));
    }
    return true;
}

bool isAscMetadataLine(const QString& trimmedLine)
{
    if (trimmedLine.startsWith("//"))
        return true;

    const QString lower = trimmedLine.toLower();
    return lower.startsWith("date ")
        || lower.startsWith("base ")
        || lower.startsWith("no internal events")
        || lower == "begin triggerblock"
        || lower == "end triggerblock"
        || lower == "begin trigger block"
        || lower == "end trigger block";
}

} // namespace

QString TraceImporter::load(const QString& filePath,
                            QVector<CANMessage>& outMessages)
{
    outMessages.clear();

    const QFileInfo fi(filePath);
    const QString ext = fi.suffix().toLower();
    if (ext == "asc")
        return loadAsc(filePath, outMessages);
    if (ext == "blf")
        return loadBlf(filePath, outMessages);

    return QString("Unsupported trace format: %1").arg(fi.suffix());
}

QString TraceImporter::loadAsc(const QString& filePath,
                               QVector<CANMessage>& outMessages)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString("Cannot open for reading: %1").arg(filePath);

    static const QRegularExpression wsRe(QStringLiteral("\\s+"));

    QTextStream in(&file);
    int parsedFrames = 0;

    while (!in.atEnd()) {
        const QString rawLine = in.readLine();

        const QString line = rawLine.trimmed();
        if (line.isEmpty() || isAscMetadataLine(line))
            continue;

        const QStringList tokens = line.split(wsRe, Qt::SkipEmptyParts);
        if (tokens.size() < 5)
            continue;

        bool tsOk = false;
        const double tsSeconds = tokens[0].toDouble(&tsOk);
        if (!tsOk || tsSeconds < 0.0)
            continue;

        uint8_t channel = 1;
        parseChannelToken(tokens[1], channel); // tolerate exotic channel labels

        uint32_t id = 0;
        bool isExtended = false;
        if (!parseCanIdToken(tokens[2], id, isExtended))
            continue;

        const QString dirToken = tokens[3].toLower();
        if (dirToken != "rx" && dirToken != "tx")
            continue;

        const QString typeToken = tokens[4].toLower();
        int cursor = 5;

        CANMessage msg;
        msg.id = id;
        msg.channel = channel;
        msg.isExtended = isExtended;
        msg.isTxConfirm = (dirToken == "tx");
        msg.timestamp = static_cast<quint64>(qRound64(tsSeconds * 1.0e9));

        if (typeToken == "errorframe" || typeToken == "error") {
            msg.isError = true;
            outMessages.append(msg);
            ++parsedFrames;
            continue;
        }

        if (typeToken == "r") {
            quint8 dlc = 0;
            if (cursor >= tokens.size() || !parseDlcToken(tokens[cursor], false, dlc))
                continue;

            msg.isRemote = true;
            msg.dlc = dlc;
            outMessages.append(msg);
            ++parsedFrames;
            continue;
        }

        if (typeToken == "canfd" || typeToken == "fd") {
            quint8 dlc = 0;
            if (cursor >= tokens.size() || !parseDlcToken(tokens[cursor], true, dlc))
                continue;

            msg.isFD = true;
            msg.dlc = dlc;
            ++cursor;

            int byteCount = 0;
            while (cursor < tokens.size()) {
                quint8 byteValue = 0;
                if (!parseByteToken(tokens[cursor], byteValue))
                    break;

                if (byteCount < 64)
                    msg.data[byteCount] = byteValue;
                ++byteCount;
                ++cursor;
            }

            if (byteCount > 0 && byteCount != CANManager::dlcToLength(msg.dlc))
                msg.dlc = CANManager::lengthToDlc(qMin(byteCount, 64));

            while (cursor < tokens.size()) {
                const QString flag = tokens[cursor].toUpper();
                if (flag == "BRS")
                    msg.isBRS = true;
                ++cursor;
            }

            outMessages.append(msg);
            ++parsedFrames;
            continue;
        }

        if (typeToken != "d")
            continue;

        quint8 dlc = 0;
        if (cursor >= tokens.size() || !parseDlcToken(tokens[cursor], false, dlc))
            continue;

        msg.dlc = dlc;
        ++cursor;

        int byteCount = 0;
        const int expected = qMin<int>(msg.dlc, 8);
        while (cursor < tokens.size() && byteCount < expected) {
            quint8 byteValue = 0;
            if (!parseByteToken(tokens[cursor], byteValue))
                break;

            msg.data[byteCount] = byteValue;
            ++byteCount;
            ++cursor;
        }

        if (byteCount != expected)
            msg.dlc = static_cast<quint8>(byteCount);

        outMessages.append(msg);
        ++parsedFrames;
    }

    if (parsedFrames == 0)
        return QString("No CAN frames found in ASC file: %1")
            .arg(QFileInfo(filePath).fileName());

    return {};
}

QString TraceImporter::loadBlf(const QString& filePath,
                               QVector<CANMessage>& outMessages)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return QString("Cannot open for reading: %1").arg(filePath);

    QDataStream ds(&file);
    ds.setByteOrder(QDataStream::LittleEndian);

    char fileSig[4] = {};
    if (ds.readRawData(fileSig, 4) != 4 || std::memcmp(fileSig, "BLF\0", 4) != 0)
        return QString("Invalid BLF header in %1").arg(QFileInfo(filePath).fileName());

    quint32 statsSize = 0;
    quint32 apiVersion = 0;
    ds >> statsSize >> apiVersion;
    Q_UNUSED(apiVersion);

    if (ds.status() != QDataStream::Ok)
        return QString("Failed to read BLF header: %1")
            .arg(QFileInfo(filePath).fileName());

    if (statsSize < 24 || statsSize > static_cast<quint32>(file.size()))
        return QString("Invalid BLF statistics block size (%1)").arg(statsSize);

    // ── Pre-allocate from header objectCount (offset 12 in the stats block) ──
    //  The stats block stores objectCount at offset 12 (quint32).  We already
    //  read past offset 12 (statsSize + apiVersion consumed bytes 4..11),
    //  so the stream is at offset 12 right now.  Read it and reserve.
    quint32 objectCount = 0;
    ds >> objectCount;
    if (objectCount > 0 && objectCount < 10000000u)
        outMessages.reserve(static_cast<int>(objectCount));

    if (!file.seek(statsSize))
        return QString("Failed to seek BLF data section in %1")
            .arg(QFileInfo(filePath).fileName());

    int parsedFrames = 0;

    while (file.pos() + 24 <= file.size()) {
        const qint64 objectStart = file.pos();

        char objectSig[4] = {};
        if (ds.readRawData(objectSig, 4) != 4)
            break;
        if (std::memcmp(objectSig, "LOBJ", 4) != 0) {
            return QString("Unexpected BLF object signature at offset %1")
                .arg(objectStart);
        }

        quint16 headerSize = 0;
        quint16 headerVersion = 0;
        quint32 objectSize = 0;
        quint32 objectType = 0;
        quint64 ts10ns = 0;
        ds >> headerSize >> headerVersion >> objectSize >> objectType >> ts10ns;
        Q_UNUSED(headerVersion);

        if (ds.status() != QDataStream::Ok) {
            return QString("Corrupted BLF object header at offset %1")
                .arg(objectStart);
        }

        if (headerSize < 24 || objectSize < headerSize) {
            return QString("Invalid BLF object size at offset %1")
                .arg(objectStart);
        }

        const qint64 objectEnd = objectStart + objectSize;
        if (objectEnd > file.size()) {
            return QString("Truncated BLF object at offset %1")
                .arg(objectStart);
        }

        if (!file.seek(objectStart + headerSize)) {
            return QString("Failed to seek BLF payload at offset %1")
                .arg(objectStart);
        }

        const quint32 payloadSize = objectSize - headerSize;
        if (objectType == 1 && payloadSize >= 16) {
            quint32 id = 0;
            quint16 channel = 1;
            quint8 dlc = 0;
            quint8 flags = 0;
            quint8 data[8] = {};

            ds >> id >> channel >> dlc >> flags;
            for (quint8& b : data)
                ds >> b;

            if (ds.status() != QDataStream::Ok) {
                return QString("Corrupted CAN object at offset %1")
                    .arg(objectStart);
            }

            CANMessage msg;
            msg.id = id & 0x1FFFFFFFu;
            msg.channel = static_cast<uint8_t>(qBound(1, static_cast<int>(channel), 255));
            msg.dlc = static_cast<uint8_t>(qMin<int>(dlc, 8));
            msg.isExtended = (flags & 0x04u) != 0u;
            msg.isTxConfirm = (flags & 0x10u) != 0u;
            msg.timestamp = ts10ns * 10ull;
            std::copy(std::begin(data), std::end(data), std::begin(msg.data));
            outMessages.append(msg);
            ++parsedFrames;
        } else if (objectType == 86 && payloadSize >= 76) {
            quint32 id = 0;
            quint16 channel = 1;
            quint8 dlc = 0;
            quint8 flags = 0;
            quint32 reserved = 0;
            quint8 data[64] = {};

            ds >> id >> channel >> dlc >> flags >> reserved;
            Q_UNUSED(reserved);
            for (quint8& b : data)
                ds >> b;

            if (ds.status() != QDataStream::Ok) {
                return QString("Corrupted CAN FD object at offset %1")
                    .arg(objectStart);
            }

            CANMessage msg;
            msg.id = id & 0x1FFFFFFFu;
            msg.channel = static_cast<uint8_t>(qBound(1, static_cast<int>(channel), 255));
            msg.isFD = true;
            msg.dlc = static_cast<uint8_t>(qMin<int>(dlc, 15));
            msg.isBRS = (flags & 0x01u) != 0u;
            msg.isExtended = (flags & 0x04u) != 0u;
            msg.isTxConfirm = (flags & 0x10u) != 0u;
            msg.timestamp = ts10ns * 10ull;
            std::copy(std::begin(data), std::end(data), std::begin(msg.data));
            outMessages.append(msg);
            ++parsedFrames;
        }

        if (!file.seek(objectEnd)) {
            return QString("Failed to seek next BLF object at offset %1")
                .arg(objectStart);
        }
    }

    if (parsedFrames == 0)
        return QString("No CAN frames found in BLF file: %1")
            .arg(QFileInfo(filePath).fileName());

    return {};
}
