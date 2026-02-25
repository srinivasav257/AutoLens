#pragma once
/**
 * @file TraceExporter.h
 * @brief CAN trace export helpers — ASC (text) and BLF (binary) formats.
 *
 * ═══════════════════════════════════════════════════════════════════════════
 *  SUPPORTED FORMATS
 * ═══════════════════════════════════════════════════════════════════════════
 *
 *  ASC  (Vector ASCII Log)
 *  ─────────────────────────────────────────────────────────────────────────
 *  Human-readable text format.  Opens directly in a text editor or in
 *  CANalyzer / CANoe for replay.  Each frame is one line:
 *
 *    date Mon Feb 21 10:30:00.000 am 2026
 *    base hex  timestamps absolute
 *    no internal events logged
 *    Begin Triggerblock
 *       0.001234 1  0C4  Rx   d 8 AA BB CC DD 00 01 02 03
 *       0.002345 2  18DB33F1x Tx   d 4 FF FE FD FC
 *       0.003456 1  064  Rx   CANFD 8 11 22 33 44 55 66 77  BRS
 *    End TriggerBlock
 *
 *  Fields: timestamp_s  channel  id  direction  type  dlc  [data...]  [flags]
 *    timestamp  — seconds from measurement start, 6 decimal places
 *    id         — hex, no suffix = 11-bit standard; 'x' suffix = 29-bit extended
 *    type       — d (data), r (remote frame), CANFD (CAN-FD data)
 *    BRS / ESI  — CAN-FD bit-rate-switch and error-state-indicator flags
 *
 *  BLF  (Vector Binary Log File)
 *  ─────────────────────────────────────────────────────────────────────────
 *  Compact binary format.  Typically 3–5× smaller than ASC for the same data.
 *  Preferred by automated test systems and large-trace analysis tools.
 *
 *  File layout:
 *
 *    ┌─────────────────────────────┐
 *    │  File Statistics Block      │  144 bytes — metadata, object count,
 *    │  (BlfFileStatistics)        │  measurement start/end timestamps
 *    ├─────────────────────────────┤
 *    │  LOBJ record #0             │  24-byte header + frame-specific payload
 *    │  LOBJ record #1             │
 *    │  ...                        │
 *    └─────────────────────────────┘
 *
 *  Every LOBJ record starts with "LOBJ" (4 bytes), then fields that tell the
 *  reader the object size, type and timestamp.  The payload depends on type:
 *    type  1 = CAN_MESSAGE       classic CAN (DLC 0..8)
 *    type 86 = CAN_FD_MESSAGE    CAN FD (DLC 0..64)
 *
 *  All multi-byte integers are little-endian (x86 native).
 *  Timestamps are stored in 10-nanosecond units (so 1 ms = 100 000 ticks).
 *
 * ═══════════════════════════════════════════════════════════════════════════
 *  USAGE
 * ═══════════════════════════════════════════════════════════════════════════
 *
 *    #include "trace/TraceExporter.h"
 *
 *    // ASC
 *    QString err = TraceExporter::saveAsAsc("/path/to/trace.asc",
 *                                          traceModel.frames());
 *    if (!err.isEmpty())  qWarning() << err;
 *
 *    // BLF
 *    QString err = TraceExporter::saveAsBLF("/path/to/trace.blf",
 *                                          traceModel.frames());
 */

#include <QString>
#include <QVector>
#include <deque>
#include "trace/TraceModel.h"   // for TraceEntry + CANMessage

// ─────────────────────────────────────────────────────────────────────────────
//  TraceExporter — stateless export helpers (all methods are static)
// ─────────────────────────────────────────────────────────────────────────────

class TraceExporter
{
public:
    /**
     * @brief Save trace in Vector ASC (ASCII Log) format.
     * @param filePath  Destination file path (must be writable).
     * @param frames    Frames from TraceModel::frames().
     * @return  Empty string on success; human-readable error message on failure.
     */
    static QString saveAsAsc(const QString& filePath,
                             const std::deque<TraceEntry>& frames);

    /**
     * @brief Save trace in Vector BLF (Binary Log File) format.
     * @param filePath  Destination file path (must be writable).
     * @param frames    Frames from TraceModel::frames().
     * @return  Empty string on success; human-readable error message on failure.
     */
    static QString saveAsBLF(const QString& filePath,
                             const std::deque<TraceEntry>& frames);

    /**
     * @brief Save trace as comma-separated values (CSV).
     * @param filePath  Destination file path (must be writable).
     * @param frames    Frames from TraceModel::frames().
     * @return  Empty string on success; human-readable error message on failure.
     */
    static QString saveAsCsv(const QString& filePath,
                             const std::deque<TraceEntry>& frames);

private:
    // ── BLF format constants ──────────────────────────────────────────────────

    /// Object type code for a classic CAN frame (≤ 8 data bytes).
    static constexpr quint32 BLF_OBJ_CAN_MESSAGE    = 1;

    /// Object type code for a CAN FD frame (≤ 64 data bytes).
    static constexpr quint32 BLF_OBJ_CAN_FD_MESSAGE = 86;

    /// Size of the file-statistics block at the start of a BLF file.
    static constexpr quint32 BLF_STATS_SIZE = 144;

    /// Size of the LOBJ object header (common to every log object).
    static constexpr quint32 BLF_OBJ_HEADER_SIZE = 24;

    /// Size of a CAN_MESSAGE payload (id + channel + dlc + flags + 8 bytes data).
    static constexpr quint32 BLF_CAN_MSG_PAYLOAD  = 16;

    /// Size of a CAN_FD_MESSAGE payload (id + channel + dlc + flags + pad + 64 bytes data).
    static constexpr quint32 BLF_CANFD_MSG_PAYLOAD = 76;

    /// BLF API version written into the file header (0x0403 = v4.3).
    static constexpr quint32 BLF_API_VERSION = 0x0403;

    // ── BLF private helpers ───────────────────────────────────────────────────

    /**
     * @brief Write the 24-byte LOBJ object header.
     *
     * WHY a helper function: every LOBJ record — whether CAN, CAN FD, or
     * a future type — starts with the same 24-byte header.  One helper
     * eliminates copy-paste errors in the field ordering / sizes.
     *
     * Header layout (all little-endian):
     *   [0]   char[4]   "LOBJ"                — magic signature
     *   [4]   uint16    headerSize = 24        — total size of THIS header
     *   [6]   uint16    headerVersion = 1      — header format version
     *   [8]   uint32    objectSize             — header + payload in bytes
     *   [12]  uint32    objectType             — BLF_OBJ_CAN_MESSAGE etc.
     *   [16]  uint64    timestamp              — 10-ns ticks from meas. start
     *
     * @param ds            Output data stream (must be LittleEndian).
     * @param objectType    BLF_OBJ_CAN_MESSAGE or BLF_OBJ_CAN_FD_MESSAGE.
     * @param payloadBytes  Bytes of payload that follow this header.
     * @param ts10ns        Timestamp in 10-nanosecond units.
     */
    static void writeBlfObjectHeader(QDataStream& ds,
                                     quint32 objectType,
                                     quint32 payloadBytes,
                                     quint64 ts10ns);
};
