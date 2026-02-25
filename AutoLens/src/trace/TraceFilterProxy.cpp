/**
 * @file TraceFilterProxy.cpp
 * @brief Sorting and free-text filtering for the CAN trace view.
 */

#include "trace/TraceFilterProxy.h"
#include "trace/TraceModel.h"

#include <QModelIndex>
#include <QString>

TraceFilterProxy::TraceFilterProxy(QObject* parent)
    : QSortFilterProxyModel(parent)
{
    // Filter across all columns by default
    setFilterCaseSensitivity(Qt::CaseInsensitive);
    setRecursiveFilteringEnabled(true);   // show signal rows if parent matches
    setSortCaseSensitivity(Qt::CaseInsensitive);
}

void TraceFilterProxy::setFilterText(const QString& text)
{
    if (m_filterText == text) return;
    m_filterText = text;
    emit filterTextChanged();
    invalidateFilter();
}

void TraceFilterProxy::sortByColumn(int column, bool ascending)
{
    sort(column, ascending ? Qt::AscendingOrder : Qt::DescendingOrder);
}

void TraceFilterProxy::clearSort()
{
    // Qt: calling sort(-1) removes any sort proxy and restores source order
    sort(-1);
}

// ─────────────────────────────────────────────────────────────────────────────
//  filterAcceptsRow — free-text filter across key columns
// ─────────────────────────────────────────────────────────────────────────────

bool TraceFilterProxy::filterAcceptsRow(int sourceRow,
                                         const QModelIndex& sourceParent) const
{
    if (m_filterText.isEmpty())
        return true;

    // For signal rows (children): defer to parent frame's acceptance
    if (sourceParent.isValid())
        return true;   // show signal if its parent frame is shown

    const QAbstractItemModel* model = sourceModel();
    if (!model) return true;

    // Check columns: Name(1), ID(2), Channel(3), EventType(4), Dir(5), Data(7)
    static const int searchCols[] = { 1, 2, 3, 4, 5, 7 };
    for (int col : searchCols) {
        const QModelIndex idx = model->index(sourceRow, col, sourceParent);
        const QString text = model->data(idx, Qt::DisplayRole).toString();
        if (text.contains(m_filterText, Qt::CaseInsensitive))
            return true;
    }

    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
//  lessThan — column-aware comparison for sorting
// ─────────────────────────────────────────────────────────────────────────────

bool TraceFilterProxy::lessThan(const QModelIndex& left,
                                 const QModelIndex& right) const
{
    const int col = left.column();
    const QVariant leftData  = sourceModel()->data(left,  Qt::DisplayRole);
    const QVariant rightData = sourceModel()->data(right, Qt::DisplayRole);

    switch (col) {
    case TraceModel::ColTime: {
        // Numeric comparison for timestamps
        bool okL = false, okR = false;
        const double lv = leftData.toString().toDouble(&okL);
        const double rv = rightData.toString().toDouble(&okR);
        if (okL && okR) return lv < rv;
        break;
    }

    case TraceModel::ColID: {
        // Strip trailing 'h' and compare as hex integers
        QString ls = leftData.toString().trimmed();
        QString rs = rightData.toString().trimmed();
        if (ls.endsWith('h', Qt::CaseInsensitive)) ls.chop(1);
        if (rs.endsWith('h', Qt::CaseInsensitive)) rs.chop(1);

        bool okL = false, okR = false;
        const quint32 lv = ls.toUInt(&okL, 16);
        const quint32 rv = rs.toUInt(&okR, 16);
        if (okL && okR) return lv < rv;
        break;
    }

    case TraceModel::ColChn:
    case TraceModel::ColDLC: {
        // Numeric comparison
        bool okL = false, okR = false;
        const int lv = leftData.toInt(&okL);
        const int rv = rightData.toInt(&okR);
        if (okL && okR) return lv < rv;
        break;
    }

    default:
        break;
    }

    // Default: case-insensitive string comparison
    return QString::localeAwareCompare(leftData.toString(),
                                       rightData.toString()) < 0;
}
