#pragma once
/**
 * @file TraceFilterProxy.h
 * @brief QSortFilterProxyModel for trace view — column sorting and text filtering.
 *
 * Sits between TraceModel and QML TreeView.  Provides:
 *  - Column sorting (by Time, ID, Channel, Event Type, etc.)
 *  - Free-text filter matching against ID, Name, and Data columns
 *
 * WHY a proxy instead of filtering in TraceModel directly:
 *  - TraceModel stores the canonical data; the proxy provides a _view_ of it.
 *  - Sorting/filtering can be toggled without copying or re-indexing the data.
 *  - Qt's model/view architecture is designed for exactly this pattern.
 */

#include <QSortFilterProxyModel>
#include <QString>

class TraceFilterProxy : public QSortFilterProxyModel
{
    Q_OBJECT

    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterTextChanged)

public:
    explicit TraceFilterProxy(QObject* parent = nullptr);

    QString filterText() const { return m_filterText; }
    void setFilterText(const QString& text);

    /**
     * @brief Enable / disable sorting by column.
     *
     * Called from QML when user clicks a column header.
     * Passing -1 disables sorting and restores insertion order.
     */
    Q_INVOKABLE void sortByColumn(int column, bool ascending = true);

    /** @brief Clear current sort order (return to insertion order). */
    Q_INVOKABLE void clearSort();

signals:
    void filterTextChanged();

protected:
    /**
     * @brief Accept or reject a source row based on filterText.
     *
     * Matches against columns: Name (1), ID (2), Channel (3),
     * Event Type (4), Direction (5), Data (7).
     */
    bool filterAcceptsRow(int sourceRow,
                          const QModelIndex& sourceParent) const override;

    /**
     * @brief Compare two source rows for sorting.
     *
     * Column-specific comparisons:
     *  - Time (0): numeric comparison (parse float from display string)
     *  - ID (2):   hex comparison (parse ID from "0C4h" format)
     *  - Channel (3): numeric
     *  - Others:  case-insensitive string comparison
     */
    bool lessThan(const QModelIndex& left,
                  const QModelIndex& right) const override;

private:
    QString m_filterText;
};
