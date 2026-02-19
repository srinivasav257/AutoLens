#pragma once
/**
 * @file TraceModel.h
 * @brief Qt data model for the CAN trace window (TableView backend).
 *
 * Qt Model-View overview
 * ──────────────────────
 * Qt separates *data* (model) from *display* (view):
 *
 *   ┌──────────────┐       data()        ┌────────────────┐
 *   │  TraceModel  │ ◄──────────────────► │ QML TableView  │
 *   │ (this class) │  rowsInserted signal │  (in TracePage)│
 *   └──────────────┘                      └────────────────┘
 *
 * TraceModel inherits QAbstractTableModel and overrides:
 *   • rowCount()    — how many frames are stored
 *   • columnCount() — fixed at ColCount (7 columns)
 *   • data()        — returns display text or colour for a given cell
 *   • headerData()  — column headers shown in HorizontalHeaderView
 *   • roleNames()   — maps Qt role integers to QML property names
 *                     e.g. Qt::DisplayRole → "display"
 *
 * QML TableView accesses role data via the "display" property:
 *   delegate: Label { text: model.display }
 *
 * Performance strategy
 * ────────────────────
 * At high bus loads (10 000+ frames/s), we must NOT call
 * beginInsertRows/endInsertRows for every single frame — that triggers
 * QML to re-layout the table on each call, causing jitter.
 *
 * Instead, AppController batches frames and calls addFrames() every 50 ms.
 * addFrames() does one beginInsertRows/endInsertRows for the whole batch.
 *
 * Maximum size is capped at MAX_ROWS; oldest rows are purged in bulk when
 * the cap is hit.
 */

#include <QAbstractTableModel>
#include <QVector>
#include <QString>
#include <cstdint>

#include "hardware/CANInterface.h"
#include "dbc/DBCParser.h"

// ============================================================================
//  TraceEntry — one row in the table
// ============================================================================

/**
 * @brief All data for one row in the trace table.
 *
 * We pre-compute string representations at insertion time so the data()
 * function is a simple array lookup — critical for smooth 60 Hz scrolling.
 */
struct TraceEntry
{
    CANManager::CANMessage msg;     ///< Raw frame (id, data, timestamp …)

    // Pre-formatted display strings (computed once on insertion)
    QString timeStr;        ///< "1234.567" ms relative to measurement start
    QString channelStr;     ///< "CH1"
    QString idStr;          ///< "0x0C4"  (or "0x18DB33F1 [Ext]")
    QString dlcStr;         ///< "8"
    QString dataStr;        ///< "AA BB CC DD 00 00 FF 12"
    QString msgName;        ///< DBC message name, e.g. "EngineData"
    QString signalsText;    ///< "RPM=1450rpm Thr=42%"
};

// ============================================================================
//  TraceModel
// ============================================================================

class TraceModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    // Column indices (also used in headerData / data switch statements)
    enum Column {
        ColTime = 0,    ///< Relative timestamp  "1234.567 ms"
        ColChannel,     ///< Hardware channel     "CH1"
        ColID,          ///< CAN ID (hex)         "0x0C4"
        ColDLC,         ///< Data length          "8"
        ColData,        ///< Raw bytes (hex)      "AA BB CC…"
        ColMsgName,     ///< DBC name             "EngineData"
        ColSignals,     ///< Decoded signals       "RPM=1450rpm"
        ColCount        ///< Sentinel — always last
    };

    static constexpr int MAX_ROWS = 50000;  ///< Cap to prevent unbounded memory use

    explicit TraceModel(QObject* parent = nullptr);

    // --- QAbstractTableModel interface ---
    int      rowCount(const QModelIndex& parent = {}) const override;
    int      columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // --- Data management ---

    /**
     * @brief Add a batch of pre-built TraceEntry objects.
     *
     * Called by AppController::flushPendingFrames() every 50 ms.
     * Emits beginInsertRows / endInsertRows once for the whole batch.
     *
     * @param entries  Entries already decoded (strings filled in).
     */
    void addEntries(const QVector<TraceEntry>& entries);

    /** Remove all rows from the model. */
    void clear();

    /** Current row count (for status bar display). */
    int frameCount() const { return m_rows.size(); }

private:
    QVector<TraceEntry> m_rows;
};
