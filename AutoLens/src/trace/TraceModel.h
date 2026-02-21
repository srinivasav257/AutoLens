#pragma once
/**
 * @file TraceModel.h
 * @brief Professional hierarchical CAN/CAN-FD trace model (QAbstractItemModel).
 *
 * ═══════════════════════════════════════════════════════════════════════════
 *  WHY QAbstractItemModel instead of QAbstractTableModel?
 * ═══════════════════════════════════════════════════════════════════════════
 *  A flat QAbstractTableModel cannot express parent-child relationships.
 *  We need a 2-level tree so QML TreeView can expand/collapse frames:
 *
 *    Root (invisible)
 *    ├─ Frame 0  [EngineData  | 0C4h | CH1 | CAN FD | Rx | 8 | AA BB ...]
 *    │    ├─ Signal: EngineSpeed = 1450 rpm   (raw: 0x05A6)
 *    │    └─ Signal: ThrottlePos = 42.5 %     (raw: 0x00AB)
 *    ├─ Frame 1  [BrakeStatus | 0B2h | CH2 | CAN    | Rx | 4 | 01 00 ...]
 *    └─ Frame 2  [---         | 7DFh | CH1 | CAN    | Rx | 8 | 02 01 ...]
 *
 * ═══════════════════════════════════════════════════════════════════════════
 *  ZERO-ALLOCATION ENCODING TRICK
 * ═══════════════════════════════════════════════════════════════════════════
 *  The tree structure is encoded entirely in QModelIndex::internalPointer —
 *  no heap-allocated "node" objects needed at all:
 *
 *    Frame  index: createIndex(frameRow, col, nullptr)
 *                                               ^^ nullptr = "I am a frame"
 *
 *    Signal index: createIndex(sigRow, col, (void*)(frameRow + 1))
 *                                               ^^ non-null = signal
 *                                               +1 so it's never nullptr
 *
 *  This lets the model hold 100 000+ frames + millions of signals with
 *  zero per-item heap overhead beyond the m_frames QVector itself.
 *
 * ═══════════════════════════════════════════════════════════════════════════
 *  8-COLUMN LAYOUT  (matches Vector CANalyzer / CANoe trace window)
 * ═══════════════════════════════════════════════════════════════════════════
 *   Col 0  Time        "   1234.567890"  right-aligned, monospace
 *   Col 1  Name        DBC message name  blue=decoded, grey=unknown
 *   Col 2  ID          "0C4h" / "18DB33F1h"  CANoe-style hex+h suffix
 *   Col 3  Chn         "1" / "2"  coloured by channel number
 *   Col 4  Event Type  "CAN FD" / "CAN" / "Error Frame" / "Remote Frame"
 *   Col 5  Dir         "Rx" / "Tx"
 *   Col 6  DLC         "8" / "64"
 *   Col 7  Data        "AA BB CC DD 00 00 FF 12"  monospace hex
 *
 * ═══════════════════════════════════════════════════════════════════════════
 *  SIGNAL ROWS (depth = 1 in QML TreeView)
 * ═══════════════════════════════════════════════════════════════════════════
 *   Signal rows display their data across the same 8 columns:
 *   Col 1  Signal name   (indented under frame name)
 *   Col 2  Physical value  "1450 rpm"
 *   Col 7  Raw value       "0x05A6"
 *   All other columns: empty string
 */

#include <QAbstractItemModel>
#include <QHash>
#include <QVector>
#include <QString>
#include <cstdint>

#include "hardware/CANInterface.h"
#include "dbc/DBCParser.h"

// ─────────────────────────────────────────────────────────────────────────────
//  SignalRow — one decoded DBC signal (appears as a child tree row)
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Data for one decoded signal shown as a child row under its parent frame.
 *
 * Stored by value inside TraceEntry::signals — no heap allocation per signal.
 * All strings are pre-formatted at insertion time so data() is a fast lookup.
 */
struct SignalRow
{
    QString name;       ///< Signal name,       e.g. "EngineSpeed"
    QString valueStr;   ///< Physical value,    e.g. "1450 rpm"
    QString rawStr;     ///< Raw hex value,     e.g. "0x05A6"
};

// ─────────────────────────────────────────────────────────────────────────────
//  TraceEntry — all display data for one CAN frame (pre-formatted)
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief One frame row in the trace tree.
 *
 * All display strings are pre-computed in AppController::buildEntry()
 * at insertion time so TraceModel::data() is a trivial array lookup
 * (O(1), no QString formatting on the hot render path).
 *
 * PERFORMANCE: The entire TraceEntry is stored by value in a QVector,
 * giving tight memory layout and great cache performance.
 */
struct TraceEntry
{
    // ── Raw frame (kept for color / flag decisions in data()) ────────────────
    CANManager::CANMessage msg;

    // ── Pre-formatted column strings ─────────────────────────────────────────
    QString timeStr;        ///< Col 0  "   1234.567890"  (leading spaces for alignment)
    QString nameStr;        ///< Col 1  "EngineData" or "" if not in DBC
    QString idStr;          ///< Col 2  "0C4h" or "18DB33F1h"
    QString chnStr;         ///< Col 3  "1" or "2"
    QString eventTypeStr;   ///< Col 4  "CAN FD" / "CAN" / "Error Frame" / "Remote Frame"
    QString dirStr;         ///< Col 5  "Rx" or "Tx"
    QString dlcStr;         ///< Col 6  "8" or "64"
    QString dataStr;        ///< Col 7  "AA BB CC DD ..."  (hex bytes, space-separated)

    // ── Decoded signals — child rows when frame is expanded ──────────────────
    // NOTE: Named "decodedSignals" — cannot use "signals" (Qt moc keyword).
    QVector<SignalRow> decodedSignals;
};

// ─────────────────────────────────────────────────────────────────────────────
//  TraceModel — QAbstractItemModel for a 2-level CAN trace tree
// ─────────────────────────────────────────────────────────────────────────────

class TraceModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    // ── Column identifiers ────────────────────────────────────────────────────
    /**
     * @brief Column index constants.
     *
     * Used in:
     *  • data()       switch(col) to pick the right field
     *  • headerData() switch(section) for column header labels
     *  • QML delegate switch(column) for per-column styling
     */
    enum class DisplayMode {
        Append = 0,
        InPlace = 1
    };

    enum Column {
        ColTime      = 0,   ///< Relative timestamp
        ColName      = 1,   ///< DBC message name
        ColID        = 2,   ///< CAN arbitration ID
        ColChn       = 3,   ///< Hardware channel number
        ColEventType = 4,   ///< Frame type (CAN / CAN FD / Error / Remote)
        ColDir       = 5,   ///< Direction (Rx / Tx)
        ColDLC       = 6,   ///< Data length code
        ColData      = 7,   ///< Hex data bytes
        ColCount     = 8    ///< Sentinel — always keep last
    };
    Q_ENUM(Column)

    // ── Custom QML roles ──────────────────────────────────────────────────────
    /**
     * @brief Roles beyond Qt built-ins (Qt::DisplayRole = "display", etc.)
     *
     * These are returned by roleNames() so QML can bind:
     *   color: model.isError ? "red" : "white"
     *   color: model.channel === 2 ? "#ff8c4d" : "#4da8ff"
     */
    enum Role {
        IsFrameRole   = Qt::UserRole + 1,  ///< bool: true=frame row, false=signal row
        IsErrorRole,                        ///< bool: error frame
        IsFDRole,                           ///< bool: CAN FD frame (EDL bit set)
        IsDecodedRole,                      ///< bool: DBC name was found
        ChannelRole,                        ///< int:  hardware channel number (1 or 2)
        SignalNameRole,                     ///< QString: (signal rows) signal name
        SignalValueRole,                    ///< QString: (signal rows) "1450 rpm"
        SignalRawRole                       ///< QString: (signal rows) "0x05A6"
    };

    // ── Configuration constants ───────────────────────────────────────────────

    /**
     * @brief Maximum number of frames to keep in memory.
     *
     * When exceeded, PURGE_CHUNK oldest frames are removed at once
     * (bulk remove is cheaper than per-frame removes).
     */
    static constexpr int MAX_ROWS    = 100000;
    static constexpr int PURGE_CHUNK = 5000;

    explicit TraceModel(QObject* parent = nullptr);
    ~TraceModel() override = default;

    void setDisplayMode(DisplayMode mode);
    DisplayMode displayMode() const { return m_displayMode; }

    // ── QAbstractItemModel interface (required overrides) ─────────────────────

    /**
     * @brief Create a QModelIndex for the item at (row, col) under parent.
     *
     * WHY: Qt's model-view system calls this constantly to locate items.
     * Must be O(1) — we encode the hierarchy in internalPointer directly.
     *
     *  parent invalid  →  frame item  →  createIndex(row, col, nullptr)
     *  parent = frame  →  signal item →  createIndex(row, col, frameRow+1)
     */
    QModelIndex index(int row, int col,
                      const QModelIndex& parent = {}) const override;

    /**
     * @brief Return the parent index of child.
     *
     *  Frame  items → invalid (they are root-level)
     *  Signal items → the frame index (read frameRow from internalPointer)
     */
    QModelIndex parent(const QModelIndex& child) const override;

    /**
     * @brief Number of child rows under parent.
     *
     *  parent invalid  →  total frame count
     *  parent = frame  →  number of decoded signals in that frame
     *  parent = signal →  0 (signals have no children)
     */
    int rowCount(const QModelIndex& parent = {}) const override;

    /**
     * @brief Fixed column count (same for all items).
     */
    int columnCount(const QModelIndex& parent = {}) const override;

    /**
     * @brief Return display data, color data, or custom role data for a cell.
     *
     * PERFORMANCE: All strings are pre-formatted at insertion time.
     * This function only does array lookups — no QString formatting.
     */
    QVariant data(const QModelIndex& index,
                  int role = Qt::DisplayRole) const override;

    /**
     * @brief Column header labels and styling.
     */
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    /**
     * @brief Maps role integers → QML property names.
     *
     * Example in QML delegate:
     *   color: model.isError ? "#ff5555" : "#c8daf0"
     *   color: model.channel === 2 ? "#ff8c4d" : "#4da8ff"
     */
    QHash<int, QByteArray> roleNames() const override;

    // ── Data management ───────────────────────────────────────────────────────

    /**
     * @brief Batch-insert frames (called by AppController every 50 ms).
     *
     * Does ONE beginInsertRows / endInsertRows for the whole batch —
     * much cheaper than one call per frame at high bus loads.
     * Purges the oldest PURGE_CHUNK frames first if MAX_ROWS would be exceeded.
     *
     * @param entries Pre-built entries (strings already formatted).
     */
    void addEntries(const QVector<TraceEntry>& entries);

    /** Remove all frames from the model. */
    void clear();

    /** Current frame count (for status bar display). */
    int frameCount() const { return m_frames.size(); }

private:
    static quint64 makeEntryKey(const TraceEntry& entry);
    void rebuildInPlaceIndex();
    void purgeOldestRows(int count);
    void addEntriesAppend(const QVector<TraceEntry>& entries);
    void addEntriesInPlace(const QVector<TraceEntry>& entries);
    void updateInPlaceRow(int row, const TraceEntry& entry);
    // ── Internal helpers ──────────────────────────────────────────────────────

    /**
     * @brief Returns true if index represents a signal (child) row.
     *
     * Detection: internalPointer != nullptr → it's a signal.
     * (Frame items always have internalPointer == nullptr.)
     */
    static bool isSignalIndex(const QModelIndex& idx)
    {
        // internalPointer == nullptr → frame (root-level) item
        // internalPointer != nullptr → signal (child) item
        return idx.isValid() && idx.internalPointer() != nullptr;
    }

    /**
     * @brief Decode the frame row from a signal index's internalPointer.
     *
     * We stored (frameRow + 1) to guarantee the pointer is non-null.
     * Subtract 1 here to recover the real row number.
     */
    static int frameRowOf(const QModelIndex& idx)
    {
        // internalPointer holds (frameRow + 1) as a pointer-sized integer
        return static_cast<int>(
                   reinterpret_cast<quintptr>(idx.internalPointer())) - 1;
    }

    QVector<TraceEntry> m_frames;   ///< All stored frames (root-level items)
    DisplayMode         m_displayMode = DisplayMode::Append;
    QHash<quint64, int> m_inPlaceRows; ///< key -> row index (only used in in-place mode)
};
