/**
 * @file TraceModel.cpp
 * @brief TraceModel implementation.
 */

#include "TraceModel.h"
#include <QColor>

// ============================================================================
//  Constructor
// ============================================================================

TraceModel::TraceModel(QObject* parent) : QAbstractTableModel(parent) {}

// ============================================================================
//  QAbstractTableModel interface
// ============================================================================

int TraceModel::rowCount(const QModelIndex& parent) const
{
    // Convention: for table models, return 0 if parent is valid
    // (flat table — no tree hierarchy).
    if (parent.isValid()) return 0;
    return m_rows.size();
}

int TraceModel::columnCount(const QModelIndex& parent) const
{
    if (parent.isValid()) return 0;
    return ColCount;
}

QVariant TraceModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()) return {};

    const int row = index.row();
    const int col = index.column();

    if (row < 0 || row >= m_rows.size()) return {};

    const TraceEntry& e = m_rows[row];

    // -----------------------------------------------------------------------
    //  Qt::DisplayRole — text shown in the cell
    // -----------------------------------------------------------------------
    if (role == Qt::DisplayRole) {
        switch (col) {
        case ColTime:     return e.timeStr;
        case ColChannel:  return e.channelStr;
        case ColID:       return e.idStr;
        case ColDLC:      return e.dlcStr;
        case ColData:     return e.dataStr;
        case ColMsgName:  return e.msgName.isEmpty() ? QStringLiteral("—") : e.msgName;
        case ColSignals:  return e.signalsText.isEmpty() ? QStringLiteral("—") : e.signalsText;
        default:          return {};
        }
    }

    // -----------------------------------------------------------------------
    //  Qt::ForegroundRole — text colour
    //  Error frames → red, TX echoes → grey, normal → default (white in dark theme)
    // -----------------------------------------------------------------------
    if (role == Qt::ForegroundRole) {
        if (e.msg.isError)      return QColor(Qt::red);
        if (e.msg.isTxConfirm)  return QColor(0x88, 0x99, 0xaa);  // muted grey
        if (!e.msgName.isEmpty()) return QColor(0x5f, 0xd4, 0x8a); // green for decoded
        return QColor(0xea, 0xea, 0xea);                            // default light
    }

    // -----------------------------------------------------------------------
    //  Qt::BackgroundRole — alternating row colours for readability
    // -----------------------------------------------------------------------
    if (role == Qt::BackgroundRole) {
        return (row % 2 == 0) ? QColor(0x1a, 0x1a, 0x2e) : QColor(0x16, 0x21, 0x3e);
    }

    // -----------------------------------------------------------------------
    //  Qt::TextAlignmentRole — centre-align short columns
    // -----------------------------------------------------------------------
    if (role == Qt::TextAlignmentRole) {
        if (col == ColChannel || col == ColDLC)
            return static_cast<int>(Qt::AlignCenter);
        return static_cast<int>(Qt::AlignVCenter | Qt::AlignLeft);
    }

    return {};
}

QVariant TraceModel::headerData(int section, Qt::Orientation orientation,
                                 int role) const
{
    if (orientation == Qt::Vertical) return {};         // no row numbers

    if (role == Qt::DisplayRole) {
        switch (section) {
        case ColTime:    return QStringLiteral("Time (ms)");
        case ColChannel: return QStringLiteral("Ch");
        case ColID:      return QStringLiteral("ID");
        case ColDLC:     return QStringLiteral("DLC");
        case ColData:    return QStringLiteral("Data");
        case ColMsgName: return QStringLiteral("Message");
        case ColSignals: return QStringLiteral("Signals");
        default:         return {};
        }
    }

    if (role == Qt::ForegroundRole) return QColor(0xbb, 0xcc, 0xdd);
    if (role == Qt::BackgroundRole) return QColor(0x0f, 0x34, 0x60);

    return {};
}

/**
 * roleNames() maps Qt role integers to QML property names.
 *
 * In a QML TableView delegate:
 *   Text { text: model.display }   — maps to Qt::DisplayRole
 *
 * Qt::DisplayRole has the built-in name "display".
 * We don't need to add it — QAbstractItemModel handles it automatically.
 * Custom roles (if added later) would need entries here.
 */
QHash<int, QByteArray> TraceModel::roleNames() const
{
    auto roles = QAbstractTableModel::roleNames();
    // Built-in roles already included:
    //   Qt::DisplayRole    → "display"
    //   Qt::DecorationRole → "decoration"
    //   Qt::EditRole       → "edit"
    //   Qt::ToolTipRole    → "toolTip"
    //   Qt::BackgroundRole → "background"
    //   Qt::ForegroundRole → "foreground"
    return roles;
}

// ============================================================================
//  Data Management
// ============================================================================

void TraceModel::addEntries(const QVector<TraceEntry>& entries)
{
    if (entries.isEmpty()) return;

    // ---------- Purge oldest rows if we would exceed MAX_ROWS ----------
    const int incoming = entries.size();
    const int current  = m_rows.size();
    const int total    = current + incoming;

    if (total > MAX_ROWS) {
        // Remove enough rows from the front to make room.
        // Purge in chunks of 5000 to avoid too-frequent layout shifts.
        int toRemove = qMax(total - MAX_ROWS, 5000);
        toRemove = qMin(toRemove, current);  // can't remove more than we have

        beginRemoveRows(QModelIndex(), 0, toRemove - 1);
        m_rows.remove(0, toRemove);
        endRemoveRows();
    }

    // ---------- Append the new batch ----------
    const int firstNew = m_rows.size();
    const int lastNew  = firstNew + incoming - 1;

    beginInsertRows(QModelIndex(), firstNew, lastNew);
    m_rows.append(entries);
    endInsertRows();
}

void TraceModel::clear()
{
    if (m_rows.isEmpty()) return;
    beginResetModel();
    m_rows.clear();
    endResetModel();
}
