#pragma once
/**
 * @file AppController.h
 * @brief Central C++↔QML bridge for AutoLens.
 *
 * AppController is the single object that QML talks to for everything:
 *
 *   QML reads properties:
 *     AppController.connected       — is hardware open?
 *     AppController.measuring       — are frames being received?
 *     AppController.driverName      — "Vector XL" or "Demo"
 *     AppController.channelList     — ["Ch1 (VN1630)", "Ch2 (VN1630)"]
 *     AppController.dbcLoaded       — true once a .dbc is parsed
 *     AppController.dbcInfo         — "vehicle.dbc | 42 msg | 312 sig"
 *     AppController.statusText      — one-line status for the toolbar
 *     AppController.frameCount      — total frames in trace
 *     AppController.frameRate       — frames/s (updated every second)
 *     AppController.traceModel      — bound to the QML TableView
 *
 *   QML calls methods:
 *     AppController.refreshChannels()          — scan HW, populate channelList
 *     AppController.connectChannel(index)      — open + start receive
 *     AppController.disconnectChannel()        — stop + close
 *     AppController.loadDbc(filePath)          — parse a .dbc file
 *     AppController.clearTrace()              — empty the trace table
 *     AppController.sendFrame(id, data, ext)  — transmit one frame
 *
 * Threading
 * ─────────
 *  AppController lives on the UI thread.
 *  VectorCANDriver's async thread emits messageReceived(CANMessage) which
 *  Qt delivers to onFrameReceived() via a queued connection (automatic for
 *  cross-thread).  AppController accumulates frames in m_pending and a
 *  50 ms QTimer flushes them into TraceModel in a single batch, keeping
 *  the UI smooth even at high bus loads.
 *
 *  DemoCANDriver uses a QTimer on the UI thread, so its emissions are
 *  direct connections — no queuing, but that's fine since the same
 *  batching timer handles them the same way.
 */

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QTimer>
#include <QThread>
#include <QElapsedTimer>

#include "hardware/CANInterface.h"
#include "dbc/DBCParser.h"
#include "trace/TraceModel.h"

class AppController : public QObject
{
    Q_OBJECT

    // -----------------------------------------------------------------------
    //  Q_PROPERTY — each property is readable from QML
    //
    //  Syntax: Q_PROPERTY(Type name READ getter [WRITE setter] NOTIFY signal)
    //  The NOTIFY signal is emitted whenever the value changes so QML bindings
    //  automatically re-evaluate.
    // -----------------------------------------------------------------------

    Q_PROPERTY(bool       connected    READ connected    NOTIFY connectedChanged)
    Q_PROPERTY(bool       measuring    READ measuring    NOTIFY measuringChanged)
    Q_PROPERTY(QString    driverName   READ driverName   NOTIFY driverNameChanged)
    Q_PROPERTY(QStringList channelList READ channelList  NOTIFY channelListChanged)
    Q_PROPERTY(int        selectedChannel READ selectedChannel
                                          WRITE setSelectedChannel
                                          NOTIFY selectedChannelChanged)

    Q_PROPERTY(bool    dbcLoaded  READ dbcLoaded  NOTIFY dbcLoadedChanged)
    Q_PROPERTY(QString dbcInfo    READ dbcInfo    NOTIFY dbcInfoChanged)

    Q_PROPERTY(QString statusText  READ statusText  NOTIFY statusTextChanged)
    Q_PROPERTY(int     frameCount  READ frameCount  NOTIFY frameCountChanged)
    Q_PROPERTY(int     frameRate   READ frameRate   NOTIFY frameRateChanged)

    /** The model that QML's TableView binds to. CONSTANT = pointer never changes. */
    Q_PROPERTY(TraceModel* traceModel READ traceModel CONSTANT)

public:
    explicit AppController(QObject* parent = nullptr);
    ~AppController() override;

    // --- Property getters ---
    bool        connected()       const { return m_connected; }
    bool        measuring()       const { return m_measuring; }
    QString     driverName()      const;
    QStringList channelList()     const { return m_channelList; }
    int         selectedChannel() const { return m_selectedChannel; }
    bool        dbcLoaded()       const { return !m_dbcDb.isEmpty(); }
    QString     dbcInfo()         const { return m_dbcInfo; }
    QString     statusText()      const { return m_statusText; }
    int         frameCount()      const { return m_traceModel.frameCount(); }
    int         frameRate()       const { return m_frameRate; }
    TraceModel* traceModel()            { return &m_traceModel; }

    // --- Property setter ---
    void setSelectedChannel(int index);

public slots:
    // -----------------------------------------------------------------------
    //  Q_INVOKABLE vs slot
    //  Both can be called from QML.  We use slots here so they can also be
    //  connected to Qt signals from other C++ objects.
    // -----------------------------------------------------------------------

    /** Re-scan hardware for available CAN channels. */
    void refreshChannels();

    /** Open the selected channel and begin receiving frames. */
    void connectChannel();

    /** Stop receiving and close the channel. */
    void disconnectChannel();

    /**
     * @brief Parse a DBC file and enable signal decoding.
     * @param filePath  Local filesystem path, e.g. "C:/proj/vehicle.dbc"
     *                  (QML file dialogs return "file:///C:/..." — we strip the prefix)
     */
    void loadDbc(const QString& filePath);

    /** Remove all rows from the trace table. */
    void clearTrace();

    /**
     * @brief Transmit one CAN frame.
     * @param id       CAN arbitration ID (decimal or hex from QML)
     * @param hexData  Hex string of bytes, e.g. "AA BB CC 00"
     * @param extended true for 29-bit extended ID
     */
    Q_INVOKABLE void sendFrame(quint32 id, const QString& hexData, bool extended = false);

signals:
    void connectedChanged();
    void measuringChanged();
    void driverNameChanged();
    void channelListChanged();
    void selectedChannelChanged();
    void dbcLoadedChanged();
    void dbcInfoChanged();
    void statusTextChanged();
    void frameCountChanged();
    void frameRateChanged();

    /** Emitted with a short message to show in a UI toast / log */
    void errorOccurred(const QString& message);

private slots:
    /** Receives frames from the driver (via queued or direct connection). */
    void onFrameReceived(const CANManager::CANMessage& msg);

    /** Flushes the m_pending buffer into TraceModel — called by m_flushTimer. */
    void flushPendingFrames();

    /** Updates m_frameRate from m_framesSinceLastSec — called by m_rateTimer. */
    void updateFrameRate();

private:
    // --- Helpers ---
    void setStatus(const QString& text);
    TraceEntry buildEntry(const CANManager::CANMessage& msg) const;

    /**
     * @brief Called (on the UI thread) when the async init thread completes.
     *
     * WHY a separate method instead of inline lambda: QMetaObject::invokeMethod
     * needs to marshal the call across threads. Passing channel data through a
     * lambda capture is the safest way since QList is implicitly shared (copy-on-
     * write), so the background thread's copy stays valid even after the thread ends.
     */
    void applyDriverInitResult(bool ok,
                               const QList<CANManager::CANChannelInfo>& channels);

    // --- Driver ---
    CANManager::ICANDriver*            m_driver     = nullptr;
    QThread*                           m_initThread = nullptr; ///< Background init thread (temp)
    QList<CANManager::CANChannelInfo>  m_channelInfos;   ///< Raw info from detectChannels()
    QStringList                        m_channelList;    ///< Display strings for QML

    // --- State ---
    bool    m_connected        = false;
    bool    m_measuring        = false;
    int     m_selectedChannel  = 0;
    QString m_statusText;

    // --- DBC ---
    DBCManager::DBCDatabase  m_dbcDb;
    QString                  m_dbcInfo;

    // --- Trace model ---
    TraceModel m_traceModel;

    // --- Batching ---
    QVector<CANManager::CANMessage> m_pending;   ///< Frames waiting to be flushed
    QTimer   m_flushTimer;                        ///< 50 ms → flushPendingFrames()
    QTimer   m_rateTimer;                         ///< 1000 ms → updateFrameRate()
    QElapsedTimer m_measureStart;                 ///< Timestamp t=0 for the trace

    // --- Stats ---
    int m_frameRate          = 0;
    int m_framesSinceLastSec = 0;
};
