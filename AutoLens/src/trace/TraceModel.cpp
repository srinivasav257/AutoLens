/**
 * @file TraceModel.cpp
 * @brief Implementation of the 2-level CAN trace tree model.
 *
 * ═══════════════════════════════════════════════════════════════════════════
 *  TREE INDEX SCHEME (recap)
 * ═══════════════════════════════════════════════════════════════════════════
 *
 *  The QModelIndex stores position + a raw pointer used as an integer tag:
 *
 *   createIndex(frameRow, col, nullptr)          → frame item
 *   createIndex(sigRow,   col, void*(frameRow+1)) → signal item
 *
 *  index() builds these.  parent() reads them to reconstruct the parent.
 *  data()  uses isSignalIndex() to branch between frame and signal display.
 *
 * ═══════════════════════════════════════════════════════════════════════════
 *  RENDERING COLOURS  (dark CANoe-like theme)
 * ═══════════════════════════════════════════════════════════════════════════
 *
 *  Frame rows:
 *    Decoded (DBC hit)  →  name text: #56b4f5  (bright blue)
 *    CAN FD             →  event col: #ffd070  (amber)
 *    Error frame        →  all text:  #ff6666  (red);  bg: #200f10
 *    TX echo            →  all text:  #7a9ab8  (muted grey-blue)
 *    Channel 1          →  default colours
 *    Channel 2          →  chn text:  #ff8c4d  (orange)
 *    Even / odd row     →  bg: #0f1825 / #121e2e  (alternating navy)
 *
 *  Signal child rows:
 *    Name column        →  #7dcfff  (light blue)
 *    Background         →  #0c1422  (slightly darker/bluer)
 */

#include "TraceModel.h"
#include <QColor>
#include <QDebug>

// ─────────────────────────────────────────────────────────────────────────────
//  Constructor
// ─────────────────────────────────────────────────────────────────────────────

TraceModel::TraceModel(QObject* parent)
    : QAbstractItemModel(parent)
    , m_displayMode(DisplayMode::Append)
{}

quint64 TraceModel::makeEntryKey(const TraceEntry& entry)
{
    const auto& msg = entry.msg;
    quint64 key = static_cast<quint64>(msg.id);
    key |= (static_cast<quint64>(msg.channel) & 0xFFull) << 32;
    key |= (msg.isExtended  ? 1ull : 0ull) << 40;
    key |= (msg.isRemote    ? 1ull : 0ull) << 41;
    key |= (msg.isError     ? 1ull : 0ull) << 42;
    key |= (msg.isFD        ? 1ull : 0ull) << 43;
    key |= (msg.isTxConfirm ? 1ull : 0ull) << 44;
    return key;
}

void TraceModel::setDisplayMode(DisplayMode mode)
{
    if (m_displayMode == mode) return;

    m_displayMode = mode;

    if (m_displayMode == DisplayMode::Append) {
        m_inPlaceRows.clear();
        return;
    }

    if (m_frames.isEmpty()) {
        m_inPlaceRows.clear();
        return;
    }

    // Entering in-place mode: collapse duplicates so each key has one visible row.
    beginResetModel();

    QVector<TraceEntry> compact;
    compact.reserve(m_frames.size());

    QHash<quint64, int> keyToRow;
    keyToRow.reserve(m_frames.size());

    for (const TraceEntry& frame : m_frames) {
        const quint64 key = makeEntryKey(frame);
        auto it = keyToRow.find(key);
        if (it == keyToRow.end()) {
            keyToRow.insert(key, compact.size());
            compact.append(frame);
        } else {
            compact[it.value()] = frame;
        }
    }

    m_frames = compact;
    m_inPlaceRows = keyToRow;

    endResetModel();
}

void TraceModel::rebuildInPlaceIndex()
{
    m_inPlaceRows.clear();
    if (m_displayMode != DisplayMode::InPlace) return;

    m_inPlaceRows.reserve(m_frames.size());
    for (int row = 0; row < m_frames.size(); ++row)
        m_inPlaceRows.insert(makeEntryKey(m_frames[row]), row);
}

void TraceModel::purgeOldestRows(int count)
{
    if (count <= 0 || m_frames.isEmpty()) return;

    count = qMin(count, m_frames.size());
    beginRemoveRows(QModelIndex{}, 0, count - 1);
    m_frames.remove(0, count);
    endRemoveRows();

    if (m_displayMode == DisplayMode::InPlace)
        rebuildInPlaceIndex();
}

void TraceModel::updateInPlaceRow(int row, const TraceEntry& entry)
{
    if (row < 0 || row >= m_frames.size()) return;

    const int oldChildCount = m_frames[row].decodedSignals.size();
    const int newChildCount = entry.decodedSignals.size();
    const QModelIndex parentFrame = index(row, 0, QModelIndex{});

    if (newChildCount < oldChildCount) {
        beginRemoveRows(parentFrame, newChildCount, oldChildCount - 1);
        m_frames[row].decodedSignals.remove(newChildCount, oldChildCount - newChildCount);
        endRemoveRows();
    }

    if (newChildCount > oldChildCount) {
        beginInsertRows(parentFrame, oldChildCount, newChildCount - 1);
        for (int i = oldChildCount; i < newChildCount; ++i)
            m_frames[row].decodedSignals.append(entry.decodedSignals[i]);
        endInsertRows();
    }

    m_frames[row] = entry;
    emit dataChanged(index(row, 0, QModelIndex{}),
                     index(row, ColCount - 1, QModelIndex{}));

    if (newChildCount > 0) {
        emit dataChanged(index(0, 0, parentFrame),
                         index(newChildCount - 1, ColCount - 1, parentFrame));
    }
}

void TraceModel::addEntriesAppend(const QVector<TraceEntry>& entries)
{
    if (entries.isEmpty()) return;

    const int incoming = entries.size();
    const int current  = m_frames.size();

    qDebug() << "[TraceModel::Append] incoming=" << incoming
             << "current=" << current << "mode=Append";

    if (current + incoming > MAX_ROWS)
    {
        int toRemove = qMax(current + incoming - MAX_ROWS, PURGE_CHUNK);
        toRemove = qMin(toRemove, current);     // can't remove more than we have
        purgeOldestRows(toRemove);
    }

    const int first = m_frames.size();
    const int last  = first + incoming - 1;

    beginInsertRows(QModelIndex{}, first, last);
    m_frames.append(entries);
    endInsertRows();

    qDebug() << "[TraceModel::Append] after insert, m_frames.size()=" << m_frames.size();
}

void TraceModel::addEntriesInPlace(const QVector<TraceEntry>& entries)
{
    if (entries.isEmpty()) return;

    qDebug() << "[TraceModel::InPlace] incoming=" << entries.size()
             << "current=" << m_frames.size() << "mapSize=" << m_inPlaceRows.size();

    for (const TraceEntry& entry : entries) {
        const quint64 key = makeEntryKey(entry);
        const auto it = m_inPlaceRows.constFind(key);

        if (it != m_inPlaceRows.cend()) {
            const int row = it.value();
            if (row >= 0 && row < m_frames.size()) {
                updateInPlaceRow(row, entry);
                continue;
            }
            // Self-heal stale map entries instead of dropping frames.
            m_inPlaceRows.remove(key);
        }

        if (m_frames.size() >= MAX_ROWS) {
            const int toRemove = qMin(PURGE_CHUNK, m_frames.size());
            purgeOldestRows(toRemove);
        }

        const int row = m_frames.size();
        beginInsertRows(QModelIndex{}, row, row);
        m_frames.append(entry);
        endInsertRows();
        m_inPlaceRows.insert(key, row);
    }

    qDebug() << "[TraceModel::InPlace] after, m_frames.size()=" << m_frames.size()
             << "mapSize=" << m_inPlaceRows.size();
}

// ─────────────────────────────────────────────────────────────────────────────
//  index() — O(1) index factory
// ─────────────────────────────────────────────────────────────────────────────

QModelIndex TraceModel::index(int row, int col, const QModelIndex& parent) const
{
    // Reject invalid column numbers up front.
    if (col < 0 || col >= ColCount) return {};

    if (!parent.isValid())
    {
        // ── Root level → frame items ─────────────────────────────────────────
        // parent invalid means "give me a root-level child".
        if (row < 0 || row >= m_frames.size()) return {};

        // nullptr internalPointer = sentinel meaning "I am a frame item"
        return createIndex(row, col, nullptr);
    }

    // ── Second level → signal items ─────────────────────────────────────────
    // Signal items can only exist under frame items, not under other signals.
    if (isSignalIndex(parent)) return {};   // signals have no children

    const int frameRow = parent.row();
    if (frameRow < 0 || frameRow >= m_frames.size()) return {};

    const QVector<SignalRow>& sigs = m_frames[frameRow].decodedSignals;
    if (row < 0 || row >= sigs.size()) return {};

    // Encode (frameRow + 1) as an integer into the pointer field.
    // The +1 ensures it's never nullptr (nullptr is reserved for frame items).
    void* ptr = reinterpret_cast<void*>(static_cast<quintptr>(frameRow + 1));
    return createIndex(row, col, ptr);
}

// ─────────────────────────────────────────────────────────────────────────────
//  parent() — walk up the tree
// ─────────────────────────────────────────────────────────────────────────────

QModelIndex TraceModel::parent(const QModelIndex& child) const
{
    if (!child.isValid()) return {};

    if (!isSignalIndex(child))
        return {};  // frame items are root-level — no parent

    // Signal item: recover the frame row from internalPointer
    const int frameRow = frameRowOf(child);
    if (frameRow < 0 || frameRow >= m_frames.size()) return {};

    // Qt convention: parent indices always use column 0.
    return createIndex(frameRow, 0, nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
//  rowCount() — children at each level
// ─────────────────────────────────────────────────────────────────────────────

int TraceModel::rowCount(const QModelIndex& parent) const
{
    if (!parent.isValid())
        return m_frames.size();          // root → total frame count

    if (isSignalIndex(parent))
        return 0;                        // signal rows have no children

    // Frame item → number of decoded signals (child rows when expanded)
    const int frameRow = parent.row();
    if (frameRow < 0 || frameRow >= m_frames.size()) return 0;
    return m_frames[frameRow].decodedSignals.size();
}

// ─────────────────────────────────────────────────────────────────────────────
//  columnCount() — fixed 8 columns for all items
// ─────────────────────────────────────────────────────────────────────────────

int TraceModel::columnCount(const QModelIndex& /*parent*/) const
{
    return ColCount;    // always 8 — same for frames and signals
}

// ─────────────────────────────────────────────────────────────────────────────
//  data() — the hot path; called for every visible cell on every repaint
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Return display or style data for one cell.
 *
 * PERFORMANCE CONTRACT: O(1) — no string formatting here.
 * All display strings were pre-built in AppController::buildEntry()
 * at insertion time and stored in TraceEntry / SignalRow.
 *
 * Role dispatch order:
 *   1. DisplayRole  — most common, handled first
 *   2. ForegroundRole / BackgroundRole / TextAlignmentRole  — style
 *   3. Custom roles (IsFrameRole, IsFDRole, ChannelRole …)  — QML queries
 */
QVariant TraceModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()) return {};

    const int col = index.column();

    // ══════════════════════════════════════════════════════════════════════════
    //  SIGNAL ROW (depth = 1): child items under a frame
    // ══════════════════════════════════════════════════════════════════════════

    if (isSignalIndex(index))
    {
        const int frameRow = frameRowOf(index);
        if (frameRow < 0 || frameRow >= m_frames.size()) return {};

        const QVector<SignalRow>& sigs = m_frames[frameRow].decodedSignals;
        const int sigRow = index.row();
        if (sigRow < 0 || sigRow >= sigs.size()) return {};

        const SignalRow& sig = sigs[sigRow];

        // ── Display text ─────────────────────────────────────────────────────
        if (role == Qt::DisplayRole)
        {
            switch (col)
            {
            // Col 1: signal name (the "name" column becomes signal identifier)
            case ColName:  return sig.name;

            // Col 2: physical decoded value "1450 rpm"
            //        Reuse the ID column because signals don't have a CAN ID.
            case ColID:    return sig.valueStr;

            // Col 7: raw integer value "0x05A6"
            //        Reuse the Data column for the raw hex.
            case ColData:  return sig.rawStr;

            default:       return {};        // other columns blank for signals
            }
        }

        // ── Foreground colour (all signal rows: light steel-blue) ─────────────
        if (role == Qt::ForegroundRole)
            return QColor(0x7d, 0xcf, 0xff);    // #7dcfff — light blue

        // ── Background (slightly darker/more blue than frame rows) ────────────
        if (role == Qt::BackgroundRole)
            return QColor(0x0c, 0x14, 0x22);    // #0c1422

        // ── Custom roles ──────────────────────────────────────────────────────
        if (role == IsFrameRole)    return false;
        if (role == SignalNameRole)  return sig.name;
        if (role == SignalValueRole) return sig.valueStr;
        if (role == SignalRawRole)   return sig.rawStr;

        return {};
    }

    // ══════════════════════════════════════════════════════════════════════════
    //  FRAME ROW (depth = 0): top-level items
    // ══════════════════════════════════════════════════════════════════════════

    const int row = index.row();
    if (row < 0 || row >= m_frames.size()) return {};

    const TraceEntry& e = m_frames[row];

    // ── Qt::DisplayRole — text shown in cell ─────────────────────────────────
    if (role == Qt::DisplayRole)
    {
        switch (col)
        {
        case ColTime:      return e.timeStr;
        case ColName:      return e.nameStr;
        case ColID:        return e.idStr;
        case ColChn:       return e.chnStr;
        case ColEventType: return e.eventTypeStr;
        case ColDir:       return e.dirStr;
        case ColDLC:       return e.dlcStr;
        case ColData:      return e.dataStr;
        default:           return {};
        }
    }

    // ── Qt::TextAlignmentRole ─────────────────────────────────────────────────
    if (role == Qt::TextAlignmentRole)
    {
        switch (col)
        {
        case ColTime:   return int(Qt::AlignRight  | Qt::AlignVCenter);
        case ColChn:
        case ColDir:
        case ColDLC:    return int(Qt::AlignHCenter | Qt::AlignVCenter);
        default:        return int(Qt::AlignLeft   | Qt::AlignVCenter);
        }
    }

    // ── Qt::ForegroundRole — text colour ─────────────────────────────────────
    if (role == Qt::ForegroundRole)
    {
        if (e.msg.isError)
            return QColor(0xff, 0x66, 0x66);   // red — error frame

        if (e.msg.isTxConfirm)
            return QColor(0x7a, 0x9a, 0xb8);   // muted blue-grey — TX echo

        // Per-column colour overrides for normal frames:
        switch (col)
        {
        case ColName:
            // Blue for DBC-decoded frames, grey-white for unknown
            return e.nameStr.isEmpty()
                ? QColor(0xc8, 0xda, 0xf0)     // #c8daf0 — off-white unknown
                : QColor(0x56, 0xb4, 0xf5);    // #56b4f5 — bright blue decoded

        case ColEventType:
            // Amber for CAN FD frames (they have extended payloads)
            return e.msg.isFD
                ? QColor(0xff, 0xd0, 0x70)     // #ffd070 — amber
                : QColor(0xc8, 0xda, 0xf0);

        case ColChn:
            // Channel 1 = blue accent, Channel 2 = orange accent
            return e.msg.channel == 2
                ? QColor(0xff, 0x8c, 0x4d)     // #ff8c4d — orange
                : QColor(0x4d, 0xa8, 0xff);    // #4da8ff — blue

        case ColDir:
            // TX transmissions stand out in amber
            return e.msg.isTxConfirm
                ? QColor(0xff, 0xd0, 0x70)
                : QColor(0xc8, 0xda, 0xf0);

        default:
            return QColor(0xc8, 0xda, 0xf0);   // #c8daf0 — default off-white
        }
    }

    // ── Qt::BackgroundRole — alternating row colours ──────────────────────────
    if (role == Qt::BackgroundRole)
    {
        if (e.msg.isError)
            return QColor(0x20, 0x0f, 0x10);   // #200f10 — dark red tint

        // Alternating navy shades (subtle — CANoe style)
        return (row % 2 == 0)
            ? QColor(0x0f, 0x18, 0x25)         // #0f1825 — even rows
            : QColor(0x12, 0x1e, 0x2e);        // #121e2e — odd rows
    }

    // ── Custom roles ──────────────────────────────────────────────────────────
    if (role == IsFrameRole)   return true;
    if (role == IsErrorRole)   return e.msg.isError;
    if (role == IsFDRole)      return e.msg.isFD;
    if (role == IsDecodedRole) return !e.nameStr.isEmpty();
    if (role == ChannelRole)   return static_cast<int>(e.msg.channel);

    return {};
}

// ─────────────────────────────────────────────────────────────────────────────
//  headerData() — column header text and styling
// ─────────────────────────────────────────────────────────────────────────────

QVariant TraceModel::headerData(int section, Qt::Orientation orientation,
                                int role) const
{
    if (orientation == Qt::Vertical) return {};     // no row numbers

    if (role == Qt::DisplayRole)
    {
        switch (section)
        {
        case ColTime:      return QStringLiteral("Time (ms)");
        case ColName:      return QStringLiteral("Name");
        case ColID:        return QStringLiteral("ID");
        case ColChn:       return QStringLiteral("Chn");
        case ColEventType: return QStringLiteral("Event Type");
        case ColDir:       return QStringLiteral("Dir");
        case ColDLC:       return QStringLiteral("DLC");
        case ColData:      return QStringLiteral("Data");
        default:           return {};
        }
    }

    if (role == Qt::TextAlignmentRole)
    {
        switch (section)
        {
        case ColTime:   return int(Qt::AlignRight  | Qt::AlignVCenter);
        case ColChn:
        case ColDir:
        case ColDLC:    return int(Qt::AlignHCenter | Qt::AlignVCenter);
        default:        return int(Qt::AlignLeft   | Qt::AlignVCenter);
        }
    }

    if (role == Qt::ForegroundRole) return QColor(0x90, 0xa8, 0xc4);   // header text
    if (role == Qt::BackgroundRole) return QColor(0x0a, 0x10, 0x18);   // header bg

    return {};
}

// ─────────────────────────────────────────────────────────────────────────────
//  roleNames() — custom role → QML property name mapping
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Register custom roles so QML delegates can access them by name.
 *
 * Qt's built-in roles are already mapped:
 *   Qt::DisplayRole    → "display"
 *   Qt::ForegroundRole → "foreground"
 *   Qt::BackgroundRole → "background"
 *   Qt::DecorationRole → "decoration"
 *
 * Our custom roles enable QML bindings like:
 *   color: model.isError  ? "#ff6666" : "#c8daf0"
 *   color: model.channel  === 2 ? "#ff8c4d" : "#4da8ff"
 *   visible: model.isFrame && model.isDecoded
 */
QHash<int, QByteArray> TraceModel::roleNames() const
{
    auto roles = QAbstractItemModel::roleNames();
    roles[IsFrameRole]   = "isFrame";
    roles[IsErrorRole]   = "isError";
    roles[IsFDRole]      = "isFD";
    roles[IsDecodedRole] = "isDecoded";
    roles[ChannelRole]   = "channel";
    roles[SignalNameRole]  = "sigName";
    roles[SignalValueRole] = "sigValue";
    roles[SignalRawRole]   = "sigRaw";
    return roles;
}

// ─────────────────────────────────────────────────────────────────────────────
//  addEntries() — batch insert frames (50 ms flush from AppController)
// ─────────────────────────────────────────────────────────────────────────────

void TraceModel::addEntries(const QVector<TraceEntry>& entries)
{
    if (m_displayMode == DisplayMode::InPlace)
        addEntriesInPlace(entries);
    else
        addEntriesAppend(entries);
}

// ─────────────────────────────────────────────────────────────────────────────
//  clear() — remove all frames
// ─────────────────────────────────────────────────────────────────────────────

void TraceModel::clear()
{
    if (m_frames.isEmpty() && m_inPlaceRows.isEmpty()) return;

    // beginResetModel / endResetModel is the most efficient way to clear —
    // it tells the view to discard all cached positions and start fresh.
    beginResetModel();
    m_frames.clear();
    m_inPlaceRows.clear();
    endResetModel();
}
