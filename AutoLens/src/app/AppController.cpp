/**
 * @file AppController.cpp
 * @brief AppController implementation.
 */

#include "AppController.h"

#include "hardware/VectorCANDriver.h"
#include "hardware/DemoCANDriver.h"

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QVariantMap>
#include <atomic>
#include <memory>

using namespace CANManager;
using namespace DBCManager;

// ============================================================================
//  Constructor / Destructor
// ============================================================================

AppController::AppController(QObject* parent)
    : QObject(parent)
{
    // -----------------------------------------------------------------------
    //  Select driver
    //  Try Vector XL first. If the DLL is not found (dev machine without HW),
    //  fall back to the Demo driver so the UI always works.
    // -----------------------------------------------------------------------
    auto* vectorDrv = new VectorCANDriver(this);
    if (vectorDrv->isAvailable()) {
        m_driver = vectorDrv;
        qDebug() << "[AppController] Using Vector XL driver";
    } else {
        vectorDrv->deleteLater();
        m_driver = new DemoCANDriver(this);
        qDebug() << "[AppController] Vector XL not available — using Demo driver";
    }

    // -----------------------------------------------------------------------
    //  Connect driver signals → our slots
    //
    //  Qt::AutoConnection (the default) automatically becomes:
    //   • DirectConnection   if signal/slot live on the same thread
    //   • QueuedConnection   if they live on different threads
    //
    //  VectorCANDriver's m_rxThread emits messageReceived() from a worker
    //  thread, so Qt will use a queued connection → safe delivery to the
    //  UI thread where AppController lives.
    // -----------------------------------------------------------------------
    connect(m_driver, &ICANDriver::messageReceived,
            this,     &AppController::onFrameReceived);

    connect(m_driver, &ICANDriver::errorOccurred,
            this,     &AppController::errorOccurred);

    // -----------------------------------------------------------------------
    //  Batch-flush timer (50 ms period = 20 Hz UI updates)
    //
    //  Instead of inserting a row into TraceModel for every single incoming
    //  frame (which would re-layout the QML TableView thousands of times
    //  per second), we queue frames in m_pending and insert the whole batch
    //  once per 50 ms.  This is the key trick for smooth high-rate trace.
    // -----------------------------------------------------------------------
    m_flushTimer.setInterval(50);
    m_flushTimer.setTimerType(Qt::CoarseTimer);
    connect(&m_flushTimer, &QTimer::timeout, this, &AppController::flushPendingFrames);

    // Frame-rate counter reset every second
    m_rateTimer.setInterval(1000);
    connect(&m_rateTimer, &QTimer::timeout, this, &AppController::updateFrameRate);

    // -----------------------------------------------------------------------
    //  Defer the channel scan until AFTER the event loop starts.
    //
    //  WHY: VectorCANDriver::initialize() calls xlOpenDriver(), which talks
    //  to the Vector kernel driver service. On a machine where that service
    //  is NOT running (e.g. this dev PC — DLL present, but no hardware
    //  installed), xlOpenDriver() BLOCKS INDEFINITELY.
    //
    //  Calling refreshChannels() here (in the constructor, on the main thread)
    //  would freeze the entire UI before the window even paints.
    //
    //  QTimer::singleShot(0, ...) posts a "fire as soon as the event loop
    //  is idle" message. The constructor returns, QML renders the window,
    //  and THEN refreshChannels() is called — so the user sees the UI first.
    //  refreshChannels() itself runs the blocking call on a background thread.
    // -----------------------------------------------------------------------
    setStatus("Detecting CAN hardware...");
    QTimer::singleShot(0, this, &AppController::refreshChannels);
}

AppController::~AppController()
{
    disconnectChannel();
}

// ============================================================================
//  Property Accessors
// ============================================================================

QString AppController::driverName() const
{
    return m_driver ? m_driver->driverName() : QStringLiteral("None");
}

void AppController::setSelectedChannel(int index)
{
    if (m_selectedChannel == index) return;
    m_selectedChannel = index;
    emit selectedChannelChanged();
}

// ============================================================================
//  Channel Management
// ============================================================================

void AppController::refreshChannels()
{
    // Guard: don't start a second init thread if one is already running.
    if (m_initThread && m_initThread->isRunning()) {
        qDebug() << "[AppController] refreshChannels: init already in progress, skipping";
        return;
    }

    setStatus("Initializing driver...");

    // -----------------------------------------------------------------------
    //  Cancellation flag — shared between the watchdog and the thread.
    //
    //  WHY std::shared_ptr<std::atomic<bool>>:
    //   • shared_ptr: both the thread lambda and the watchdog lambda capture
    //     it by value. The object lives as long as either lambda exists, so
    //     there's no dangling pointer regardless of which side outlives the other.
    //   • atomic<bool>: the watchdog writes it on the main thread while the
    //     background thread reads it — must be atomic to avoid a data race.
    // -----------------------------------------------------------------------
    auto cancelled = std::make_shared<std::atomic<bool>>(false);

    // -----------------------------------------------------------------------
    //  Watchdog timer — 3 second timeout.
    //
    //  WHY we do NOT call terminate() + deleteLater() on the stuck driver:
    //
    //  initialize() holds m_mutex (via QMutexLocker) for its entire duration,
    //  including while xlOpenDriver() is blocking. If we call terminate() to
    //  kill the thread, m_mutex is left permanently locked (owned by a dead
    //  thread — Windows does NOT auto-release it).
    //
    //  Then deleteLater() → ~VectorCANDriver() → shutdown() tries to acquire
    //  m_mutex → DEADLOCK / crash (the mutex owner is gone forever).
    //
    //  The safe solution: abandon the driver.
    //   • setParent(nullptr) removes it from AppController's child list so Qt
    //     won't try to auto-delete it when AppController is destroyed.
    //   • We just forget the pointer. The driver + thread become zombies and
    //     are reclaimed by the OS when the process exits. On a dev machine
    //     without Vector HW this is an acceptable one-time leak.
    // -----------------------------------------------------------------------
    auto* watchdog = new QTimer(this);
    watchdog->setSingleShot(true);
    watchdog->setInterval(3000);   // 3 s

    // -----------------------------------------------------------------------
    //  Background thread: runs initialize() + detectChannels() off-main.
    //
    //  We capture 'cancelled' by shared_ptr value so the flag outlives both
    //  the watchdog and the thread regardless of order of destruction.
    // -----------------------------------------------------------------------
    m_initThread = QThread::create([this, cancelled]() {

        bool ok = m_driver->initialize();

        // If the watchdog already fired while we were blocked, discard the
        // result — AppController has already moved on to the Demo driver.
        if (cancelled->load()) return;

        QList<CANManager::CANChannelInfo> channels;
        if (ok)
            channels = m_driver->detectChannels();

        if (cancelled->load()) return;

        // Marshal result to the UI thread (queued = posted to event loop).
        QMetaObject::invokeMethod(this, [this, ok, channels, cancelled]() {
            // Double-check: watchdog could fire between invokeMethod() and here.
            if (!cancelled->load())
                applyDriverInitResult(ok, channels);
        }, Qt::QueuedConnection);
    });
    m_initThread->setObjectName(QStringLiteral("AutoLens_DriverInit"));

    // -- Watchdog fires if init hasn't completed within 3 s --
    connect(watchdog, &QTimer::timeout, this, [this, watchdog, cancelled]() {
        watchdog->deleteLater();

        if (!m_initThread || !m_initThread->isRunning())
            return;  // thread finished normally just before watchdog fired

        qWarning() << "[AppController] Vector driver init timed out — falling back to Demo";

        // Signal the thread to ignore its result when (if) it eventually returns.
        cancelled->store(true);

        // ------------------------------------------------------------------
        //  Abandon the stuck Vector driver — DO NOT terminate() or delete().
        //  See the long comment above explaining why terminate()+delete
        //  deadlocks due to m_mutex being held by the blocked thread.
        // ------------------------------------------------------------------
        disconnect(m_driver, nullptr, this, nullptr); // stop any signals from zombie driver
        m_driver->setParent(nullptr);  // detach from AppController → not auto-deleted
        // m_driver pointer is now abandoned (intentional leak for dev-machine scenario)

        // Forget the thread pointer too — it's stuck in a kernel call and
        // will never finish on this machine. Let it die with the process.
        m_initThread = nullptr;

        // Create Demo driver, re-wire signals.
        m_driver = new DemoCANDriver(this);
        connect(m_driver, &ICANDriver::messageReceived,
                this,     &AppController::onFrameReceived);
        connect(m_driver, &ICANDriver::errorOccurred,
                this,     &AppController::errorOccurred);

        // Demo driver init is instant (no kernel calls).
        m_driver->initialize();
        applyDriverInitResult(true, m_driver->detectChannels());

        setStatus(QString("Vector HW unavailable (timeout) — using Demo driver | %1 channel(s)")
                      .arg(m_channelList.size()));
        emit driverNameChanged();
    });

    // Stop watchdog when the thread finishes normally (context = watchdog, so
    // Qt auto-disconnects this when watchdog is deleteLater()'d — safe).
    connect(m_initThread, &QThread::finished, watchdog, [watchdog]() {
        watchdog->stop();
        watchdog->deleteLater();
    });

    // Clean up thread object after normal completion.
    connect(m_initThread, &QThread::finished, this, [this]() {
        if (m_initThread) {
            m_initThread->deleteLater();
            m_initThread = nullptr;
        }
    });

    m_initThread->start();
    watchdog->start();
}

// ============================================================================
//  Apply driver init result (called on the UI thread by the background thread)
// ============================================================================

void AppController::applyDriverInitResult(bool ok,
                                           const QList<CANManager::CANChannelInfo>& channels)
{
    if (!ok) {
        setStatus(QString("Driver init failed: %1").arg(m_driver->lastError()));
        return;
    }

    m_channelInfos = channels;
    m_channelList.clear();
    for (const auto& ch : m_channelInfos)
        m_channelList.append(ch.displayString());

    emit channelListChanged();
    emit driverNameChanged();

    if (m_channelList.isEmpty())
        setStatus("No CAN channels found");
    else
        setStatus(QString("%1 | %2 channel(s)").arg(driverName()).arg(m_channelList.size()));
}

void AppController::connectChannel()
{
    if (m_connected) {
        disconnectChannel();
        return;
    }

    if (!m_driver->initialize()) {
        setStatus("Driver init failed: " + m_driver->lastError());
        return;
    }

    // If demo mode is active and a DBC is already loaded, drive simulation
    // from real DBC messages so runtime decoding can be verified.
    if (auto* demoDrv = qobject_cast<DemoCANDriver*>(m_driver))
        demoDrv->setSimulationDatabase(m_dbcDb);

    if (m_channelInfos.isEmpty()) {
        refreshChannels();
        if (m_channelInfos.isEmpty()) {
            setStatus("No channels available");
            return;
        }
    }

    int idx = qBound(0, m_selectedChannel, m_channelInfos.size() - 1);
    const auto& ch = m_channelInfos[idx];

    CANBusConfig cfg;
    cfg.bitrate    = 500000;
    cfg.fdEnabled  = false;
    cfg.listenOnly = true;   // safe default: don't disturb the bus

    auto result = m_driver->openChannel(ch, cfg);
    if (!result.success) {
        setStatus("Connect failed: " + result.errorMessage);
        emit errorOccurred(result.errorMessage);
        return;
    }

    m_connected = true;
    emit connectedChanged();

    // Start receiving
    m_measuring = true;
    m_measureStart.start();
    m_flushTimer.start();
    m_rateTimer.start();

    // VectorCANDriver needs explicit startAsyncReceive().
    // DemoCANDriver starts its timer inside openChannel() — no extra call needed.
    if (auto* vdrv = qobject_cast<CANManager::VectorCANDriver*>(m_driver))
        vdrv->startAsyncReceive();

    emit measuringChanged();
    setStatus(QString("Connected: %1  |  500 kbit/s  |  listen-only").arg(ch.name));
}

void AppController::disconnectChannel()
{
    if (!m_connected) return;

    m_flushTimer.stop();
    m_rateTimer.stop();

    // Stop async receive (VectorCANDriver only; DemoCANDriver ignores the cast)
    if (auto* vdrv = qobject_cast<CANManager::VectorCANDriver*>(m_driver))
        vdrv->stopAsyncReceive();

    m_driver->closeChannel();

    m_connected = false;
    m_measuring = false;
    emit connectedChanged();
    emit measuringChanged();

    setStatus("Disconnected");
}

// ============================================================================
//  DBC Loading
// ============================================================================

void AppController::loadDbc(const QString& filePath)
{
    // QML FileDialog returns "file:///C:/path/to/file.dbc" — strip the scheme
    QString path = filePath;
    if (path.startsWith(QStringLiteral("file:///")))
        path = path.mid(8);                   // Windows: "C:/..."
    else if (path.startsWith(QStringLiteral("file://")))
        path = path.mid(7);                   // Linux/Mac: "/home/..."

    QFileInfo fi(path);
    if (!fi.exists()) {
        setStatus("DBC file not found: " + path);
        emit errorOccurred("File not found: " + path);
        return;
    }

    DBCParser parser;
    m_dbcDb = parser.parseFile(path);

    if (parser.hasErrors()) {
        qWarning() << "[AppController] DBC parse warnings:";
        for (const auto& e : parser.errors())
            qWarning() << "  Line" << e.line << ":" << e.message;
    }

    // Update demo traffic generator with real IDs/signals from this DBC.
    if (auto* demoDrv = qobject_cast<DemoCANDriver*>(m_driver))
        demoDrv->setSimulationDatabase(m_dbcDb);

    // Build info string for the toolbar
    m_dbcInfo = QString("%1  |  %2 msg  |  %3 sig")
                    .arg(fi.fileName())
                    .arg(m_dbcDb.messages.size())
                    .arg(m_dbcDb.totalSignalCount());

    emit dbcLoadedChanged();
    emit dbcInfoChanged();
    setStatus("DBC loaded: " + m_dbcInfo);

    qDebug() << "[AppController] DBC loaded:" << m_dbcInfo;
}

// ============================================================================
//  Trace Operations
// ============================================================================

void AppController::clearTrace()
{
    m_traceModel.clear();
    emit frameCountChanged();
    setStatus("Trace cleared");
}

// ============================================================================
//  Pause / Resume
// ============================================================================

void AppController::pauseMeasurement()
{
    if (!m_measuring) return;   // nothing to pause

    m_paused = !m_paused;
    emit pausedChanged();

    if (!m_paused)
    {
        // On resume: immediately flush any frames that accumulated while paused.
        // This ensures the user sees all missed frames when they unpause.
        flushPendingFrames();
        setStatus("Measurement resumed");
    }
    else
    {
        setStatus("Measurement paused — frames queuing");
    }
}

// ============================================================================
//  Save Trace to CSV
// ============================================================================

/**
 * @brief Export the current trace to a CSV file.
 *
 * Format:  Time,Name,ID,Chn,EventType,Dir,DLC,Data
 *
 * @param filePath  Local file path (may have "file:///" prefix from QML FileDialog).
 */
void AppController::saveTrace(const QString& filePath)
{
    // Strip the URL scheme prefix that QML FileDialog adds
    QString path = filePath;
    if (path.startsWith(QStringLiteral("file:///")))
        path = path.mid(8);
    else if (path.startsWith(QStringLiteral("file://")))
        path = path.mid(7);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        setStatus("Save failed: cannot open " + path);
        emit errorOccurred("Cannot write to: " + path);
        return;
    }

    QTextStream out(&file);

    // CSV header
    out << "Time(ms),Name,ID,Chn,EventType,Dir,DLC,Data\n";

    // Write each frame (no signal rows — they can be re-decoded from DBC)
    const TraceModel* model = &m_traceModel;
    const int rows = model->frameCount();

    // Access internal frames via the model's data() interface
    // We iterate via QAbstractItemModel for clean separation
    for (int r = 0; r < rows; ++r)
    {
        auto idx = [&](int col) { return model->index(r, col); };

        auto cell = [&](int col) -> QString {
            return model->data(idx(col), Qt::DisplayRole).toString();
        };

        // Quote fields that may contain commas (e.g. Data column)
        auto quoted = [](const QString& s) -> QString {
            if (s.contains(',') || s.contains('"'))
                return "\"" + QString(s).replace("\"", "\"\"") + "\"";
            return s;
        };

        out << cell(0) << ","
            << cell(1) << ","
            << cell(2) << ","
            << cell(3) << ","
            << cell(4) << ","
            << cell(5) << ","
            << cell(6) << ","
            << quoted(cell(7)) << "\n";
    }

    file.close();

    const QFileInfo fi(path);
    setStatus(QString("Trace saved: %1  (%2 frames)").arg(fi.fileName()).arg(rows));
}

void AppController::sendFrame(quint32 id, const QString& hexData, bool extended)
{
    if (!m_connected) {
        emit errorOccurred("Not connected — cannot send");
        return;
    }

    // Parse hex string "AA BB CC DD" → bytes
    CANMessage msg;
    msg.id         = id;
    msg.isExtended = extended;

    QStringList tokens = hexData.split(' ', Qt::SkipEmptyParts);
    msg.dlc = static_cast<uint8_t>(qMin(tokens.size(), 8));
    for (int i = 0; i < msg.dlc; ++i) {
        bool ok;
        msg.data[i] = static_cast<uint8_t>(tokens[i].toUInt(&ok, 16));
    }

    auto result = m_driver->transmit(msg);
    if (!result.success)
        emit errorOccurred("TX failed: " + result.errorMessage);
}

// ============================================================================
//  Frame Reception (called on UI thread via queued connection)
// ============================================================================

void AppController::onFrameReceived(const CANMessage& msg)
{
    if (!m_measuring) return;

    // Discard TX echoes from the trace (optional — can expose as a setting later)
    if (msg.isTxConfirm) return;

    m_pending.append(msg);
    ++m_framesSinceLastSec;
}

// ============================================================================
//  50 ms flush timer — batch insert into TraceModel
// ============================================================================

void AppController::flushPendingFrames()
{
    // While paused, frames accumulate in m_pending but are not pushed to the
    // model. pauseMeasurement() flushes the backlog when the user unpauses.
    if (m_pending.isEmpty() || m_paused) return;

    // Take the accumulated batch and clear the pending list
    QVector<CANMessage> batch = std::move(m_pending);
    m_pending.clear();

    // Build TraceEntry objects (decode DBC, format strings)
    QVector<TraceEntry> entries;
    entries.reserve(batch.size());
    for (const auto& msg : batch)
        entries.append(buildEntry(msg));

    m_traceModel.addEntries(entries);
    emit frameCountChanged();
}

// ============================================================================
//  Build one TraceEntry from a raw CANMessage
// ============================================================================

TraceEntry AppController::buildEntry(const CANMessage& msg) const
{
    TraceEntry e;
    e.msg = msg;

    // ── Col 0: Relative timestamp ────────────────────────────────────────────
    // Hardware timestamps arrive in nanoseconds.
    // We display as milliseconds with 6 decimal places to match CANoe precision.
    const double relMs = static_cast<double>(msg.timestamp) / 1.0e6;
    e.timeStr = QString::number(relMs, 'f', 6);

    // ── Col 1: DBC name (filled in below if DBC is loaded) ───────────────────
    // e.nameStr defaults to "" — unknown frames show blank in Name column.

    // ── Col 2: CAN ID — CANoe format: "0C4h" (standard) / "18DB33F1h" (ext) ─
    // CANoe uses lowercase hex with an 'h' suffix, no "0x" prefix.
    if (msg.isExtended)
        e.idStr = QString("%1h").arg(msg.id, 8, 16, QChar('0')).toUpper();
    else
        e.idStr = QString("%1h").arg(msg.id, 3, 16, QChar('0')).toUpper();

    // ── Col 3: Channel number ─────────────────────────────────────────────────
    e.chnStr = QString::number(msg.channel);

    // ── Col 4: Event type ────────────────────────────────────────────────────
    // Priority: Error > Remote > FD > classic CAN
    if (msg.isError)
        e.eventTypeStr = QStringLiteral("Error Frame");
    else if (msg.isRemote)
        e.eventTypeStr = QStringLiteral("Remote Frame");
    else if (msg.isFD)
        e.eventTypeStr = msg.isBRS
            ? QStringLiteral("CAN FD BRS")    // Bit-Rate Switch active
            : QStringLiteral("CAN FD");
    else
        e.eventTypeStr = QStringLiteral("CAN");

    // ── Col 5: Direction ─────────────────────────────────────────────────────
    e.dirStr = msg.isTxConfirm ? QStringLiteral("Tx") : QStringLiteral("Rx");

    // ── Col 6: DLC ───────────────────────────────────────────────────────────
    // For CAN FD, show the actual byte count (e.g. DLC=9 → "12 bytes")
    // to avoid confusion. Classic CAN shows 0-8 directly.
    if (msg.isFD && msg.dlc > 8)
        e.dlcStr = QString::number(msg.dataLength());   // "12", "16", … "64"
    else
        e.dlcStr = QString::number(msg.dlc);

    // ── Col 7: Raw data bytes (hex dump, space-separated, uppercase) ──────────
    // CAN FD frames can have up to 64 bytes — format them all.
    {
        const int len = msg.dataLength();
        QString dataStr;
        dataStr.reserve(len * 3);   // "AA " × len

        for (int i = 0; i < len; ++i)
        {
            if (i > 0) dataStr += ' ';
            dataStr += QString("%1").arg(msg.data[i], 2, 16, QChar('0')).toUpper();
        }
        e.dataStr = dataStr;
    }

    // ── DBC decode → nameStr + signals (child rows) ──────────────────────────
    if (!m_dbcDb.isEmpty())
    {
        const DBCMessage* dbcMsg = m_dbcDb.messageById(msg.id);
        if (dbcMsg)
        {
            e.nameStr = dbcMsg->name;   // fills Col 1

            const int dataLen = msg.dataLength();
            e.decodedSignals.reserve(dbcMsg->signalList.size());

            // ── Evaluate multiplexor selector first ──────────────────────────
            // Multiplexed frames have one "selector" signal (muxIndicator=="M")
            // that determines which muxed signals are active. We decode the
            // selector first so we can skip inactive mux branches below.
            bool    hasMuxSelector = false;
            int64_t activeMuxRaw   = -1;
            for (const auto& sig : dbcMsg->signalList)
            {
                if (sig.muxIndicator == QStringLiteral("M"))
                {
                    hasMuxSelector = true;
                    activeMuxRaw   = sig.rawValue(msg.data, dataLen);
                    break;
                }
            }

            for (const auto& sig : dbcMsg->signalList)
            {
                const bool isMuxSelector = (sig.muxIndicator == QStringLiteral("M"));
                const bool isMuxedSignal =
                    !sig.muxIndicator.isEmpty() && !isMuxSelector;

                // Skip muxed signals that don't belong to the active mux branch
                if (isMuxedSignal && hasMuxSelector
                    && sig.muxValue >= 0 && sig.muxValue != activeMuxRaw)
                {
                    continue;
                }

                // Decode the signal
                const int64_t rawValue      = sig.rawValue(msg.data, dataLen);
                const double  physicalValue = sig.decode(msg.data, dataLen);

                // Build physical value string "1450 rpm"
                QString valueText = QString::number(physicalValue, 'g', 8);
                if (!sig.unit.isEmpty())
                    valueText += " " + sig.unit;

                // Append enum description if available: "1 (Active)"
                if (sig.valueDescriptions.contains(rawValue))
                    valueText += QString(" (%1)").arg(
                        sig.valueDescriptions.value(rawValue));

                // Raw value in hex — "0x05A6"
                const QString rawText =
                    QString("0x%1").arg(rawValue, 0, 16, QChar('0')).toUpper();

                SignalRow sr;
                sr.name     = sig.name;
                sr.valueStr = valueText;
                sr.rawStr   = rawText;
                e.decodedSignals.append(sr);
            }
        }
    }

    return e;
}

// ============================================================================
//  Frame Rate (1 s tick)
// ============================================================================

void AppController::updateFrameRate()
{
    m_frameRate = m_framesSinceLastSec;
    m_framesSinceLastSec = 0;
    emit frameRateChanged();

    // Update status bar with live stats
    setStatus(QString("Connected: %1 fps  |  %2 frames total")
                  .arg(m_frameRate)
                  .arg(m_traceModel.frameCount()));
}

// ============================================================================
//  Helpers
// ============================================================================

void AppController::setStatus(const QString& text)
{
    if (m_statusText == text) return;
    m_statusText = text;
    emit statusTextChanged();
}
