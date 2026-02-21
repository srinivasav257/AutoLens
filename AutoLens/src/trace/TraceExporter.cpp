/**
 * @file TraceExporter.cpp
 * @brief ASC and BLF trace export implementations.
 *
 * ═══════════════════════════════════════════════════════════════════════════
 *  LEARNING NOTES
 * ═══════════════════════════════════════════════════════════════════════════
 *
 *  ASC uses QTextStream — the standard Qt way to write line-by-line text.
 *  BLF uses QDataStream with setByteOrder(LittleEndian) — the standard Qt
 *  way to write typed binary data with explicit byte ordering.
 *
 *  QDataStream << quint32 writes 4 bytes in the chosen byte order.
 *  QFile::seek() lets us jump back to fill in fields we only know at the end
 *  (e.g., objectCount and last-timestamp in the BLF file header).
 *
 *  Timestamp conversion:
 *    CANMessage::timestamp  → nanoseconds from measurement start
 *    ASC timestamp_s        = ns / 1 000 000 000.0   (seconds, 6 dp)
 *    BLF ts10ns             = ns / 10                 (10-nanosecond ticks)
 */

#include "trace/TraceExporter.h"

#include <QDataStream>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

// ─────────────────────────────────────────────────────────────────────────────
//  saveAsAsc
// ─────────────────────────────────────────────────────────────────────────────

QString TraceExporter::saveAsAsc(const QString& filePath,
                                  const QVector<TraceEntry>& frames)
{
    // ── Open file ─────────────────────────────────────────────────────────────
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return QString("Cannot open for writing: %1").arg(filePath);

    QTextStream out(&file);

    // ── ASC File Header ───────────────────────────────────────────────────────
    //
    //  CANalyzer / CANoe validate these three lines before reading frames.
    //  "base hex" means IDs and data bytes are written in hexadecimal.
    //  "timestamps absolute" means the timestamp column is seconds from
    //  the start of the measurement (not relative to the previous frame).
    //
    const QDateTime now = QDateTime::currentDateTime();
    // date format CANoe expects: "Wed Feb 21 10:30:00.000 am 2026"
    out << "date " << now.toString("ddd MMM dd hh:mm:ss.zzz ap yyyy") << "\n";
    out << "base hex  timestamps absolute\n";
    out << "no internal events logged\n";
    out << "// version 9.0.0\n";
    out << "// Application: AutoLens  v1.0.0\n";
    out << "Begin Triggerblock\n";

    // ── Frame loop ────────────────────────────────────────────────────────────
    for (const TraceEntry& e : frames)
    {
        const auto& msg = e.msg;

        // Timestamp: nanoseconds → seconds with 6 decimal places.
        // WHY 6 dp: CANoe resolution is 1 µs → 0.000001 s (6 dp sufficient).
        const double ts_s = static_cast<double>(msg.timestamp) / 1.0e9;

        // CAN ID in ASC format:
        //   11-bit standard: 3 uppercase hex digits, no suffix  e.g. "0C4"
        //   29-bit extended: 8 uppercase hex digits + 'x'       e.g. "18DB33F1x"
        //
        // WHY pad to 3 / 8 digits: CANalyzer column-aligns on width; some
        // parsers are strict about the digit count.
        QString idStr;
        if (msg.isExtended)
            idStr = QString::number(msg.id, 16).toUpper().rightJustified(8, '0') + "x";
        else
            idStr = QString::number(msg.id, 16).toUpper().rightJustified(3, '0');

        // Direction: "Rx" for received, "Tx" for frames we transmitted.
        const QString dir = msg.isTxConfirm ? "Tx" : "Rx";

        // ── Error frame ───────────────────────────────────────────────────────
        if (msg.isError) {
            // ASC error frame: no data bytes, just the ErrorFrame keyword.
            out << QString("   %1 %2  %3  %4   ErrorFrame\n")
                       .arg(ts_s, 12, 'f', 6)
                       .arg(msg.channel)
                       .arg(idStr)
                       .arg(dir, -4);
            continue;
        }

        // ── Remote frame (RTR) ────────────────────────────────────────────────
        if (msg.isRemote) {
            // Remote frames carry a DLC but NO data bytes.
            // ASC uses 'r' for remote instead of 'd' (data).
            out << QString("   %1 %2  %3  %4   r %5\n")
                       .arg(ts_s, 12, 'f', 6)
                       .arg(msg.channel)
                       .arg(idStr)
                       .arg(dir, -4)
                       .arg(msg.dlc);
            continue;
        }

        // ── Build hex data string (shared by classic CAN and CAN FD) ─────────
        const int len = msg.dataLength();   // respects CAN FD DLC table
        QString dataHex;
        dataHex.reserve(len * 3);
        for (int i = 0; i < len; ++i) {
            if (i > 0) dataHex += ' ';
            dataHex += QString::number(msg.data[i], 16).toUpper().rightJustified(2, '0');
        }

        // ── CAN FD data frame ─────────────────────────────────────────────────
        if (msg.isFD) {
            // ASC CAN FD format:
            //   timestamp channel id dir CANFD dlc data... [BRS] [ESI]
            //
            // WHY "CANFD" keyword instead of "d": it tells the parser this is
            // a flexible-data-rate frame with an extended payload.
            //
            // BRS = Bit-Rate Switch: data phase ran at a higher bitrate.
            // ESI = Error-State Indicator: transmitting node was in error-passive.
            QString fdFlags;
            if (msg.isBRS) fdFlags += "  BRS";
            // ESI is not tracked in our CANMessage struct (Vector HW sets it
            // in the flags word); add this line if you extend CANMessage later:
            //   if (msg.isESI) fdFlags += " ESI";

            out << QString("   %1 %2  %3  %4   CANFD %5 %6%7\n")
                       .arg(ts_s, 12, 'f', 6)
                       .arg(msg.channel)
                       .arg(idStr)
                       .arg(dir, -4)
                       .arg(msg.dlc)
                       .arg(dataHex)
                       .arg(fdFlags);
            continue;
        }

        // ── Classic CAN data frame ────────────────────────────────────────────
        //   timestamp channel id dir d dlc byte0 byte1 ...
        out << QString("   %1 %2  %3  %4   d %5 %6\n")
                   .arg(ts_s, 12, 'f', 6)
                   .arg(msg.channel)
                   .arg(idStr)
                   .arg(dir, -4)
                   .arg(msg.dlc)
                   .arg(dataHex);
    }

    out << "End TriggerBlock\n";
    file.close();
    return {};  // empty string = success
}

// ─────────────────────────────────────────────────────────────────────────────
//  BLF private helper — write the 24-byte LOBJ object header
// ─────────────────────────────────────────────────────────────────────────────

void TraceExporter::writeBlfObjectHeader(QDataStream& ds,
                                          quint32 objectType,
                                          quint32 payloadBytes,
                                          quint64 ts10ns)
{
    // Signature: 4 ASCII bytes "LOBJ"
    // WHY write byte-by-byte: QDataStream << char writes the char directly;
    // using writeRawData avoids any codec conversion.
    ds.writeRawData("LOBJ", 4);                                     // [0..3]

    // headerSize: 24 = total bytes in THIS header structure.
    // WHY 24: 4(sig)+2(hdrSz)+2(hdrVer)+4(objSz)+4(objType)+8(ts) = 24.
    ds << static_cast<quint16>(BLF_OBJ_HEADER_SIZE);               // [4..5]

    // headerVersion: 1 = classic object header (as opposed to extended v2).
    ds << static_cast<quint16>(1);                                  // [6..7]

    // objectSize: full record = header + payload.
    ds << static_cast<quint32>(BLF_OBJ_HEADER_SIZE + payloadBytes); // [8..11]

    // objectType: 1=CAN_MESSAGE, 86=CAN_FD_MESSAGE
    ds << objectType;                                               // [12..15]

    // timestamp: 10-nanosecond ticks from measurement start.
    // WHY 10 ns resolution: Vector hardware timestamps have 10 ns precision;
    // storing in 10-ns ticks keeps the uint64 in a comfortable range for
    // very long measurement sessions (max ~5849 years at 10-ns granularity).
    ds << ts10ns;                                                   // [16..23]
}

// ─────────────────────────────────────────────────────────────────────────────
//  saveAsBLF
// ─────────────────────────────────────────────────────────────────────────────

QString TraceExporter::saveAsBLF(const QString& filePath,
                                  const QVector<TraceEntry>& frames)
{
    // ── Open file ─────────────────────────────────────────────────────────────
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly))
        return QString("Cannot open for writing: %1").arg(filePath);

    // QDataStream in LittleEndian mode mirrors the native x86 byte order that
    // the BLF format mandates.  Every << operator writes in that order.
    QDataStream ds(&file);
    ds.setByteOrder(QDataStream::LittleEndian);

    // ── Write File Statistics Block (144 bytes) ────────────────────────────────
    //
    //  The statistics block is ALWAYS at offset 0.  It contains:
    //    • a magic signature so tools can verify this is a BLF file
    //    • the object count (filled in later after we know how many frames)
    //    • the measurement start and end times
    //
    //  We write placeholder zeros for fields we can only fill after the loop,
    //  then seek back to patch them up before closing.
    //
    //  Offset breakdown:
    //    [0]   signature[4]         "BLF\0"
    //    [4]   statsSize            144
    //    [8]   apiVersion           0x0403 (BLF API v4.3)
    //    [12]  objectCount          ← filled at end (back-patch)
    //    [16]  objectsRead          ← filled at end (back-patch)
    //    [20]  unspecified          0
    //    [24]  measureStartTs       0 (10-ns ticks; 0 = start of trace)
    //    [32]  lastObjectTs         ← filled at end (back-patch)
    //    [40]  startTime (SYSTEMTIME 16 bytes: year,month,dow,day,hour,min,sec,ms)
    //    [56]  endTime   (SYSTEMTIME 16 bytes) ← filled at end (back-patch)
    //    [72]  reserved[72]         zeros to reach 144 bytes total
    //

    const QDateTime startDt = QDateTime::currentDateTime();

    // Lambda: write a Windows SYSTEMTIME (8 × uint16).
    // WHY this layout: Vector tools expect SYSTEMTIME, the Win32 structure.
    auto writeSystemTime = [&](const QDateTime& dt) {
        ds << static_cast<quint16>(dt.date().year());
        ds << static_cast<quint16>(dt.date().month());
        ds << static_cast<quint16>(dt.date().dayOfWeek() % 7); // Qt: Mon=1, Win: Sun=0
        ds << static_cast<quint16>(dt.date().day());
        ds << static_cast<quint16>(dt.time().hour());
        ds << static_cast<quint16>(dt.time().minute());
        ds << static_cast<quint16>(dt.time().second());
        ds << static_cast<quint16>(dt.time().msec());
    };

    ds.writeRawData("BLF", 3); ds.writeRawData("\0", 1);  // [0..3]  "BLF\0"
    ds << static_cast<quint32>(BLF_STATS_SIZE);            // [4..7]  statsSize = 144
    ds << static_cast<quint32>(BLF_API_VERSION);           // [8..11] apiVersion

    // Placeholders — we come back to these after writing all frames.
    const qint64 offsetObjectCount = file.pos();  // = 12
    ds << static_cast<quint32>(0);  // [12..15] objectCount   (back-patch)
    ds << static_cast<quint32>(0);  // [16..19] objectsRead   (back-patch)
    ds << static_cast<quint32>(0);  // [20..23] unspecified

    ds << static_cast<quint64>(0);  // [24..31] measureStartTs (0 = start)

    const qint64 offsetLastObjTs = file.pos(); // = 32
    ds << static_cast<quint64>(0);  // [32..39] lastObjectTs  (back-patch)

    writeSystemTime(startDt);       // [40..55] startTime

    const qint64 offsetEndTime = file.pos(); // = 56
    writeSystemTime(startDt);       // [56..71] endTime       (back-patch, re-written at end)

    // Pad the statistics block to exactly 144 bytes.
    // WHY: tools verify statsSize == actual bytes consumed before the first LOBJ.
    static constexpr int STATS_PADDING = BLF_STATS_SIZE - 72; // = 72 bytes
    for (int i = 0; i < STATS_PADDING; ++i)
        ds << static_cast<quint8>(0);   // [72..143] reserved

    // ── Write LOBJ records ─────────────────────────────────────────────────────
    quint32 objectCount = 0;
    quint64 lastTs10ns  = 0;

    for (const TraceEntry& e : frames)
    {
        const auto& msg = e.msg;

        // Skip error and remote frames — CAN_MESSAGE type expects data bytes.
        // (Vector BLF has dedicated error-object types we don't implement here.)
        if (msg.isError || msg.isRemote)
            continue;

        // Convert nanoseconds → 10-nanosecond ticks.
        // WHY divide by 10: BLF standard uses 10-ns resolution throughout.
        const quint64 ts10ns = msg.timestamp / 10;
        lastTs10ns = ts10ns;

        if (msg.isFD)
        {
            // ── CAN FD frame (objectType = 86) ────────────────────────────────
            //
            //  CAN_FD_MESSAGE payload layout (76 bytes):
            //    [0]   uint32  id           — arbitration ID
            //    [4]   uint16  channel      — 1-based hardware channel
            //    [6]   uint8   dlc          — DLC code (0..15)
            //    [7]   uint8   flags        — bit0=BRS, bit1=ESI, bit4=Tx, bit2=ExtId
            //    [8]   uint32  reserved     — 0
            //    [12]  uint8   data[64]     — payload (zero-padded)
            //
            //  WHY keep "reserved" field: aligns data[] to a 4-byte offset which
            //  makes CAN FD payload reads faster on modern CPUs.

            writeBlfObjectHeader(ds, BLF_OBJ_CAN_FD_MESSAGE,
                                 BLF_CANFD_MSG_PAYLOAD, ts10ns);

            quint8 flags = 0;
            if (msg.isBRS)       flags |= 0x01;  // Bit-Rate Switch
            if (msg.isExtended)  flags |= 0x04;  // Extended (29-bit) ID
            if (msg.isTxConfirm) flags |= 0x10;  // Direction: Tx

            ds << static_cast<quint32>(msg.id);           // [24..27] id
            ds << static_cast<quint16>(msg.channel);      // [28..29] channel
            ds << static_cast<quint8>(msg.dlc);           // [30]     dlc
            ds << flags;                                  // [31]     flags
            ds << static_cast<quint32>(0);                // [32..35] reserved

            // data[64] — write actual bytes then zero-pad to 64.
            const int dataLen = msg.dataLength();
            for (int i = 0; i < dataLen; ++i)
                ds << msg.data[i];
            for (int i = dataLen; i < 64; ++i)
                ds << static_cast<quint8>(0);             // [36..99] data[64]
        }
        else
        {
            // ── Classic CAN frame (objectType = 1) ────────────────────────────
            //
            //  CAN_MESSAGE payload layout (16 bytes):
            //    [0]   uint32  id           — arbitration ID
            //    [4]   uint16  channel      — 1-based hardware channel
            //    [6]   uint8   dlc          — 0..8
            //    [7]   uint8   flags        — bit4=Tx, bit2=ExtId
            //    [8]   uint8   data[8]      — payload (zero-padded to 8 bytes)
            //
            //  objectSize = headerSize(24) + payload(16) = 40 bytes.

            writeBlfObjectHeader(ds, BLF_OBJ_CAN_MESSAGE,
                                 BLF_CAN_MSG_PAYLOAD, ts10ns);

            quint8 flags = 0;
            if (msg.isExtended)  flags |= 0x04;  // Extended (29-bit) ID
            if (msg.isTxConfirm) flags |= 0x10;  // Direction: Tx

            ds << static_cast<quint32>(msg.id);           // [24..27] id
            ds << static_cast<quint16>(msg.channel);      // [28..29] channel
            ds << static_cast<quint8>(msg.dlc);           // [30]     dlc
            ds << flags;                                  // [31]     flags

            // data[8] — write actual bytes then zero-pad to 8.
            const int dataLen = qMin(static_cast<int>(msg.dlc), 8);
            for (int i = 0; i < dataLen; ++i)
                ds << msg.data[i];
            for (int i = dataLen; i < 8; ++i)
                ds << static_cast<quint8>(0);             // [32..39] data[8]
        }

        ++objectCount;
    }

    // ── Back-patch the file statistics block ──────────────────────────────────
    //
    //  WHY back-patch: we didn't know objectCount or the last timestamp while
    //  writing frames.  QFile::seek() lets us jump back to offset 12 to fill
    //  in those fields now that we have the final values.
    //
    //  This is a common binary-file writing pattern in C++ (used in WAV files,
    //  ZIP local headers, RIFF chunks, etc.).

    const QDateTime endDt = QDateTime::currentDateTime();

    // Patch objectCount and objectsRead at offset 12.
    file.seek(offsetObjectCount);
    ds << objectCount;  // objectCount  [12..15]
    ds << objectCount;  // objectsRead  [16..19] — same value; "objects read" = total

    // Patch lastObjectTs at offset 32.
    file.seek(offsetLastObjTs);
    ds << lastTs10ns;   // lastObjectTs [32..39]

    // Patch endTime (SYSTEMTIME) at offset 56.
    file.seek(offsetEndTime);
    writeSystemTime(endDt);

    file.close();
    return {};  // empty string = success
}
