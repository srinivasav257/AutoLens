/**
 * @file AppController.cpp
 * @brief AppController implementation.
 *
 * Key architectural decisions in this file:
 *
 *  1. CONNECT vs START are two separate states:
 *       connected  = HW port is open, bus is physically active
 *       measuring  = frames are flowing into the trace display
 *     This matches how real CANoe works: you "connect" hardware first,
 *     then press "Start" to begin recording.
 *
 *  2. 50 ms batch flushing keeps the UI smooth at high frame rates:
 *     Frames arrive via onFrameReceived() → m_pending vector.
 *     Every 50 ms, flushPendingFrames() moves the whole batch to TraceModel
 *     in a single beginInsertRows/endInsertRows call.
 *
 *  3. Per-channel DBC: each of the 4 channel slots can have its own DBC
 *     file. All enabled channels' DBCs are merged into m_dbcDb at
 *     connect time. If two channels use the same message ID, last one wins.
 *
 *  4. 3-second watchdog on Vector driver init prevents UI freeze on machines
 *     without Vector hardware or kernel service installed.
 */

#include "AppController.h"

#include "hardware/VectorCANDriver.h"
#include "hardware/DemoCANDriver.h"
#include "trace/TraceExporter.h"
#include "trace/TraceImporter.h"

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
//  Helper: strip "file:///" URL prefix added by QML FileDialog
// ============================================================================

/*static*/ QString AppController::stripFileUrl(const QString& path)
{
    if (path.startsWith(QStringLiteral("file:///")))
        return path.mid(8);   // Windows: "C:/..."
    if (path.startsWith(QStringLiteral("file://")))
        return path.mid(7);   // Linux/Mac: "/home/..."
    return path;
}

// ============================================================================
//  Constructor / Destructor
// ============================================================================

AppController::AppController(QObject* parent)
    : QObject(parent)
{
    // -----------------------------------------------------------------------
    //  Initialise default channel configs (4 slots, all disabled)
    //
    //  WHY pre-set alias names: the dialog shows "CH1" … "CH4" even before
    //  the user has configured anything, so it's clear which slot is which.
    // -----------------------------------------------------------------------
    const char* defaultAliases[MAX_CHANNELS] = { "CH1", "CH2", "CH3", "CH4" };
    for (int i = 0; i < MAX_CHANNELS; ++i)
        m_channelConfigs[i].alias = QString::fromLatin1(defaultAliases[i]);

    // -----------------------------------------------------------------------
    //  Restore persisted channel configs from the previous session.
    //
    //  WHY here (before the driver is selected): we need saved DBC paths and
    //  channel settings in place so that rebuildMergedDbc() below can parse
    //  them immediately, making the DBC badge appear on startup without any
    //  user interaction.
    // -----------------------------------------------------------------------
    loadSettings();
    m_traceModel.setDisplayMode(
        m_inPlaceDisplayMode ? TraceModel::DisplayMode::InPlace
                             : TraceModel::DisplayMode::Append);

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
    //  Qt::AutoConnection becomes QueuedConnection for VectorCANDriver
    //  (cross-thread) and DirectConnection for DemoCANDriver (same thread).
    //  Either way, onFrameReceived() always executes on the UI thread.
    //
    //  WHY onDriverError instead of directly re-emitting errorOccurred:
    //  onDriverError intercepts fatal hardware-removal errors (HW_NOT_PRESENT)
    //  and auto-disconnects before forwarding to QML.  Without this, the
    //  receive thread would flood the error toast with errors every 100 ms.
    // -----------------------------------------------------------------------
    connect(m_driver, &ICANDriver::messageReceived,
            this,     &AppController::onFrameReceived);

    connect(m_driver, &ICANDriver::errorOccurred,
            this,     &AppController::onDriverError);

    // -----------------------------------------------------------------------
    //  Batch-flush timer (50 ms = 20 Hz UI refresh)
    //
    //  Frames accumulate in m_pending between ticks.  One insert per tick
    //  keeps beginInsertRows/endInsertRows calls rare — critical for smooth
    //  scrolling at 1000+ fps bus load.
    // -----------------------------------------------------------------------
    m_flushTimer.setInterval(50);
    m_flushTimer.setTimerType(Qt::CoarseTimer);  // save CPU, ±5% jitter OK
    connect(&m_flushTimer, &QTimer::timeout, this, &AppController::flushPendingFrames);

    // Frame-rate counter — updated once per second
    m_rateTimer.setInterval(1000);
    connect(&m_rateTimer, &QTimer::timeout, this, &AppController::updateFrameRate);

    // -----------------------------------------------------------------------
    //  Port health monitoring timer (2-second interval)
    //
    //  WHY 2 seconds: fast enough to detect unplugged hardware quickly (good
    //  UX), but rare enough not to waste CPU on constant polling.
    //
    //  The timer does NOT start here — it starts in applyDriverInitResult()
    //  once the initial hardware detection is complete.
    // -----------------------------------------------------------------------
    m_portCheckTimer.setInterval(2000);
    m_portCheckTimer.setTimerType(Qt::CoarseTimer);
    connect(&m_portCheckTimer, &QTimer::timeout, this, &AppController::checkPortHealth);

    // -----------------------------------------------------------------------
    //  Set the initial splash-screen status message.
    //
    //  startInitSequence() is NO LONGER called here via a timer.
    //
    //  WHY: The constructor runs inside main() BEFORE the QML engine even
    //  starts.  A singleShot(50 ms) timer starts counting immediately, but
    //  QML compilation + object creation takes 100–300 ms.  By the time
    //  app.exec() starts the 50 ms is already expired, so startInitSequence()
    //  fired on the first event loop tick — before the splash Window ever
    //  received a paint pass.
    //
    //  Instead, startup code (main.cpp) calls startInitSequence() only after
    //  the bootstrap splash window has painted at least one frame.
    // -----------------------------------------------------------------------
    setInitStatus("Preparing AutoLens...");
}

AppController::~AppController()
{
    disconnectChannels();
}

// ============================================================================
//  Property Accessors
// ============================================================================

QString AppController::driverName() const
{
    return m_driver ? m_driver->driverName() : QStringLiteral("None");
}

// ============================================================================
//  Hardware Detection (background thread with watchdog)
// ============================================================================

void AppController::refreshChannels()
{
    if (m_initThread && m_initThread->isRunning()) {
        qDebug() << "[AppController] refreshChannels: init already in progress, skipping";
        return;
    }

    setStatus("Initializing driver...");

    // -----------------------------------------------------------------------
    //  Cancellation flag — shared between background thread and watchdog.
    //  shared_ptr ensures the atomic lives long enough for whichever
    //  lambda runs last; atomic<bool> avoids a data race on the flag itself.
    // -----------------------------------------------------------------------
    auto cancelled = std::make_shared<std::atomic<bool>>(false);

    // -----------------------------------------------------------------------
    //  3-second watchdog.
    //
    //  WHY we DO NOT terminate() + delete() the stuck driver:
    //  initialize() holds m_mutex the whole time xlOpenDriver() blocks.
    //  terminate() kills the thread without releasing the mutex → any
    //  subsequent shutdown() → ~VectorCANDriver() → QMutexLocker would
    //  deadlock forever.
    //
    //  Safe solution: abandon the driver object (intentional one-time leak)
    //  and replace it with Demo driver. The zombie thread dies with the
    //  process; the OS cleans up its resources.
    // -----------------------------------------------------------------------
    auto* watchdog = new QTimer(this);
    watchdog->setSingleShot(true);
    watchdog->setInterval(3000);

    m_initThread = QThread::create([this, cancelled]() {
        bool ok = m_driver->initialize();

        if (cancelled->load()) return;   // watchdog already fired

        QList<CANManager::CANChannelInfo> channels;
        if (ok) channels = m_driver->detectChannels();

        if (cancelled->load()) return;

        // Marshal result back to UI thread (safe: QList is copy-on-write)
        QMetaObject::invokeMethod(this, [this, ok, channels, cancelled]() {
            if (!cancelled->load())
                applyDriverInitResult(ok, channels);
        }, Qt::QueuedConnection);
    });
    m_initThread->setObjectName(QStringLiteral("AutoLens_DriverInit"));

    connect(watchdog, &QTimer::timeout, this, [this, watchdog, cancelled]() {
        watchdog->deleteLater();
        if (!m_initThread || !m_initThread->isRunning()) return;

        qWarning() << "[AppController] Vector driver init timed out — falling back to Demo";
        cancelled->store(true);

        // Abandon stuck driver (no terminate/delete — see comment above)
        disconnect(m_driver, nullptr, this, nullptr);
        m_driver->setParent(nullptr);   // detach from AppController
        m_initThread = nullptr;

        // Create Demo driver and re-wire signals
        // WHY onDriverError (not direct errorOccurred): consistent with the
        // initial driver connection so hardware-removal logic is always active.
        m_driver = new DemoCANDriver(this);
        connect(m_driver, &ICANDriver::messageReceived,
                this,     &AppController::onFrameReceived);
        connect(m_driver, &ICANDriver::errorOccurred,
                this,     &AppController::onDriverError);

        m_driver->initialize();
        applyDriverInitResult(true, m_driver->detectChannels());

        // Override the status that applyDriverInitResult just set above, so both
        // the splash (initStatus) and the toolbar (statusText) show the timeout reason.
        setInitStatus(QString("Vector HW unavailable (timeout) — using Demo driver | %1 channel(s)")
                          .arg(m_channelList.size()));
        emit driverNameChanged();
    });

    connect(m_initThread, &QThread::finished, watchdog, [watchdog]() {
        watchdog->stop();
        watchdog->deleteLater();
    });

    connect(m_initThread, &QThread::finished, this, [this]() {
        if (m_initThread) { m_initThread->deleteLater(); m_initThread = nullptr; }
    });

    m_initThread->start();
    watchdog->start();
}

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

    // WHY setInitStatus (not setStatus): setInitStatus updates BOTH m_initStatus
    // (the property the splash screen binds to) AND m_statusText (toolbar).
    // Using plain setStatus here leaves m_initStatus stuck at "Detecting CAN hardware..."
    // so the splash fades out showing that stale message instead of the actual result.
    // With setInitStatus, the splash briefly shows the detection outcome before fading.
    if (m_channelList.isEmpty())
        setInitStatus("No CAN channels found — connect hardware or use Demo");
    else
        setInitStatus(QString("%1 | %2 channel(s) available").arg(driverName()).arg(m_channelList.size()));

    // -----------------------------------------------------------------------
    //  Mark startup as complete on the FIRST call only.
    //
    //  WHY guard with !m_initComplete:
    //  refreshChannels() can be called again by the user (e.g. after plugging
    //  in hardware).  We only want to show the splash once — on app startup.
    //  Subsequent calls update the channel list but do NOT re-trigger the splash.
    // -----------------------------------------------------------------------
    if (!m_initComplete) {
        m_initComplete = true;
        emit initCompleteChanged();

        // Start the 2-second port health monitor NOW that we know the
        // initial hardware state. Doing it here (not in the ctor) ensures we
        // don't fire health checks before init finishes.
        m_portCheckTimer.start();

        qDebug() << "[AppController] Startup complete — port health monitor active";

        // TEMP DEBUG: auto-start measurement 3 seconds after init to capture
        // the full trace flow in automated debug output (remove after diagnosis).
        QTimer::singleShot(3000, this, &AppController::startMeasurement);
    }
}

// ============================================================================
//  Startup Sequence — triggered once by startup code (main.cpp)
// ============================================================================

void AppController::startInitSequence()
{
    // -----------------------------------------------------------------------
    //  Guard: startup should call this only once.
    //  If something triggers it a second time (debug, hot reload, etc.)
    //  we silently ignore it — the init sequence must run exactly once.
    // -----------------------------------------------------------------------
    if (m_initComplete) {
        qDebug() << "[AppController] startInitSequence: already complete, skipping";
        return;
    }
    if (m_initThread && m_initThread->isRunning()) {
        qDebug() << "[AppController] startInitSequence: already in progress, skipping";
        return;
    }

    // -----------------------------------------------------------------------
    //  Step 1: Parse DBC files in a background thread.
    //
    //  WHY background: even a moderate-sized DBC (500 messages, 4000 signals)
    //  can take 100–500 ms to parse on spinning disk.  Running on the UI thread
    //  would freeze the splash animations during that window.
    //
    //  Thread-safety rationale:
    //  We snapshot the channel configs (file paths + enabled flags) into a
    //  plain struct before launching the thread.  The background thread never
    //  touches AppController members — it only reads the snapshot and creates
    //  its own local DBCDatabase objects.  Results are marshalled back to the
    //  UI thread via QMetaObject::invokeMethod (Qt::QueuedConnection).
    // -----------------------------------------------------------------------
    setInitStatus("Loading DBC files...");

    // Snapshot what we need — avoids sharing AppController members across threads
    struct DbcTask { int idx; QString path; };
    QVector<DbcTask> tasks;
    for (int i = 0; i < MAX_CHANNELS; ++i) {
        if (m_channelConfigs[i].enabled && !m_channelConfigs[i].dbcFilePath.isEmpty())
            tasks.append({ i, m_channelConfigs[i].dbcFilePath });
    }

    auto* dbcThread = QThread::create([this, tasks]() {
        // Parse each DBC file — pure I/O + parsing, zero AppController access
        QVector<QPair<int, DBCManager::DBCDatabase>> results;
        for (const auto& task : tasks) {
            DBCManager::DBCParser parser;
            auto db = parser.parseFile(task.path);
            if (!db.isEmpty())
                results.append({ task.idx, std::move(db) });
        }

        // Marshal parsed results back to the UI thread
        QMetaObject::invokeMethod(this, [this, results]() {

            // Store databases — safe here because we're back on the UI thread
            for (const auto& [idx, db] : results)
                m_channelDbs[idx] = db;

            // Merge all channel DBCs into the single decode DB (fast — no I/O)
            rebuildMergedDbc();

            // ── Step 2: Hardware detection ────────────────────────────────
            // refreshChannels() spawns its OWN background thread internally
            // and calls applyDriverInitResult() when done, which sets initComplete.
            setInitStatus("Detecting CAN hardware...");
            refreshChannels();

        }, Qt::QueuedConnection);
    });

    dbcThread->setObjectName(QStringLiteral("AutoLens_DbcLoad"));
    connect(dbcThread, &QThread::finished, dbcThread, &QThread::deleteLater);
    dbcThread->start();
}

// ============================================================================
//  Port Health Monitor — called every 2 seconds by m_portCheckTimer
// ============================================================================

void AppController::checkPortHealth()
{
    // Guard: skip if already running a concurrent check
    if (m_portChecking) return;

    // ── Case A: Not connected — silently refresh the available port list ──────
    //
    // WHY: If the user opens the CAN Config dialog after plugging in (or out)
    // a Vector device, the port dropdown should reflect reality.  With a 2-second
    // refresh the list is always fresh without requiring a manual "Refresh" click.
    if (!m_connected) {
        // Demo driver always has the same virtual channels — no need to re-scan
        if (qobject_cast<DemoCANDriver*>(m_driver)) return;

        // Skip if init or a manual refreshChannels() is already in progress
        if (m_initThread && m_initThread->isRunning()) return;

        m_portChecking = true;

        // Background thread: detectChannels() does a lightweight IPC call into
        // the Vector kernel driver — fast but still worth doing off the UI thread.
        auto* t = QThread::create([this]() {
            auto channels = m_driver->detectChannels();

            QMetaObject::invokeMethod(this, [this, channels]() {
                m_portChecking = false;

                // Compare to current list — only emit if something changed
                bool changed = (channels.size() != m_channelInfos.size());
                if (!changed) {
                    for (int i = 0; i < channels.size(); ++i) {
                        if (channels[i].name         != m_channelInfos[i].name ||
                            channels[i].serialNumber != m_channelInfos[i].serialNumber) {
                            changed = true;
                            break;
                        }
                    }
                }

                if (changed) {
                    m_channelInfos = channels;
                    m_channelList.clear();
                    for (const auto& ch : m_channelInfos)
                        m_channelList.append(ch.displayString());
                    emit channelListChanged();

                    setStatus(m_channelList.isEmpty()
                        ? "No CAN hardware found — connect a device"
                        : QString("%1 | %2 channel(s) available")
                              .arg(driverName()).arg(m_channelList.size()));

                    qDebug() << "[AppController] Port list updated by health check:"
                             << m_channelList.size() << "channel(s)";
                }
            }, Qt::QueuedConnection);
        });

        t->setObjectName(QStringLiteral("AutoLens_PortRefresh"));
        connect(t, &QThread::finished, t, &QThread::deleteLater);
        t->start();
        return;
    }

    // ── Case B: Connected with Vector HW — check port is still physically open ──
    //
    // WHY isOpen() is sufficient: When hardware is physically removed, the
    // Vector driver stops delivering events.  Our receive thread then gets
    // XL_ERR_HW_NOT_PRESENT from xlReceive(), which flows through makeError()
    // → onDriverError() which calls disconnectChannels().  By the time this
    // health check fires (2 s later), m_connected is already false — so we
    // only land here for the edge case where the error path didn't fire.
    if (auto* vdrv = qobject_cast<CANManager::VectorCANDriver*>(m_driver)) {
        if (!vdrv->isOpen()) {
            qWarning() << "[AppController] Health check: port closed unexpectedly — cleaning up";
            setStatus("CAN hardware port lost — disconnected");
            emit errorOccurred("CAN hardware was disconnected while in use");

            // Force-clean the state (port already gone — don't call driver methods)
            if (m_measuring) {
                m_flushTimer.stop();
                m_rateTimer.stop();
                m_pending.clear();
                m_measuring = false;
                m_paused    = false;
                emit measuringChanged();
                emit pausedChanged();
            }
            m_connected = false;
            emit connectedChanged();
        }
    }
    // Demo driver is always available — nothing to check
}

// ============================================================================
//  Driver Error Handler
// ============================================================================

void AppController::onDriverError(const QString& message)
{
    // -----------------------------------------------------------------------
    //  Detect fatal hardware-removal errors while connected.
    //
    //  WHY this matters: when a Vector device is physically unplugged while
    //  the async receive thread is running, xlReceive() starts returning
    //  XL_ERR_HW_NOT_PRESENT on every iteration (~every 100 ms).  Without
    //  this handler the error toast would be spammed and the app would appear
    //  frozen.
    //
    //  Solution: on the FIRST fatal error, call disconnectChannels() which
    //  sets m_asyncRunning = false and waits for the receive thread to exit.
    //  The thread then stops its loop → no more errors.
    // -----------------------------------------------------------------------
    if (m_connected && !qobject_cast<DemoCANDriver*>(m_driver)) {
        const bool isFatalHwError =
            message.contains("HW_NOT_PRESENT") ||
            message.contains("HW_NOT_READY")   ||
            message.contains("CANNOT_OPEN_DRIVER");

        if (isFatalHwError) {
            qWarning() << "[AppController] Fatal HW error — auto-disconnecting:" << message;
            setStatus("CAN hardware removed — port closed");
            disconnectChannels();  // safe re-entry guard: checks m_connected
        }
    }

    // Always forward to QML for the toast notification
    emit errorOccurred(message);
}

// ============================================================================
//  Hardware Connection (Connect / Disconnect)
// ============================================================================

void AppController::connectChannels()
{
    if (m_connected) {
        disconnectChannels();
        return;
    }

    if (!m_driver->initialize()) {
        setStatus("Driver init failed: " + m_driver->lastError());
        return;
    }

    // -----------------------------------------------------------------------
    //  Find first enabled channel config to use for connection.
    //
    //  WHY search from index 0: channels are numbered 1-4 in the UI, so
    //  CH1 (index 0) is the natural default.  If none are explicitly enabled,
    //  fall back to the first available HW channel with default settings.
    // -----------------------------------------------------------------------
    CANBusConfig busConfig;
    busConfig.listenOnly = true;   // Safe default: don't ACK or disturb the bus
    int hwIdx = 0;                 // Default: first available HW channel

    bool anyEnabled = false;
    for (int i = 0; i < MAX_CHANNELS; ++i) {
        if (m_channelConfigs[i].enabled) {
            anyEnabled = true;
            busConfig.fdEnabled      = m_channelConfigs[i].fdEnabled;
            busConfig.bitrate        = m_channelConfigs[i].bitrate;
            busConfig.fdDataBitrate  = m_channelConfigs[i].dataBitrate;
            if (m_channelConfigs[i].hwChannelIndex >= 0)
                hwIdx = m_channelConfigs[i].hwChannelIndex;
            break;
        }
    }

    // If no channel is configured yet, announce this so user knows to use CAN Config
    if (!anyEnabled) {
        setStatus(QString("Using defaults: %1 | 500 kbit/s | listen-only")
                      .arg(driverName()));
    }

    // -----------------------------------------------------------------------
    //  Refresh channel list if needed (e.g. first time or HW was plugged in)
    // -----------------------------------------------------------------------
    if (m_channelInfos.isEmpty()) {
        // Synchronous init only for Demo driver (instant); Vector was async
        if (auto* demo = qobject_cast<DemoCANDriver*>(m_driver)) {
            demo->initialize();
            m_channelInfos = demo->detectChannels();
            m_channelList.clear();
            for (const auto& ch : m_channelInfos)
                m_channelList.append(ch.displayString());
            emit channelListChanged();
        }
        if (m_channelInfos.isEmpty()) {
            emit errorOccurred("No CAN channels available — try Refresh in CAN Config");
            setStatus("No channels available");
            return;
        }
    }

    // Clamp to valid range (guard against stale hwIndex after HW changes)
    hwIdx = qBound(0, hwIdx, m_channelInfos.size() - 1);
    const auto& ch = m_channelInfos[hwIdx];

    // -----------------------------------------------------------------------
    //  Merge all configured DBC files into the decode database
    //  before opening the channel, so decoding works from the first frame.
    // -----------------------------------------------------------------------
    rebuildMergedDbc();

    // Feed merged DBC to Demo driver so it generates realistic traffic
    if (auto* demoDrv = qobject_cast<DemoCANDriver*>(m_driver))
        demoDrv->setSimulationDatabase(m_dbcDb);

    // Open the hardware channel
    auto result = m_driver->openChannel(ch, busConfig);
    if (!result.success) {
        setStatus("Connect failed: " + result.errorMessage);
        emit errorOccurred(result.errorMessage);
        return;
    }

    m_connected = true;
    emit connectedChanged();

    // Start async receive for Vector HW (Demo driver uses its own timer)
    if (auto* vdrv = qobject_cast<CANManager::VectorCANDriver*>(m_driver))
        vdrv->startAsyncReceive();

    const QString bitrateStr = busConfig.fdEnabled
        ? QString("%1k / %2k FD").arg(busConfig.bitrate/1000).arg(busConfig.fdDataBitrate/1000)
        : QString("%1k").arg(busConfig.bitrate/1000);

    setStatus(QString("Connected: %1 | %2 | listen-only | press Start to measure")
                  .arg(ch.name)
                  .arg(bitrateStr));
}

void AppController::disconnectChannels()
{
    if (!m_connected) return;

    // Stop measuring first (cleans up timers, sets m_measuring=false)
    if (m_measuring) stopMeasurement();

    // Stop Vector async receive thread
    if (auto* vdrv = qobject_cast<CANManager::VectorCANDriver*>(m_driver))
        vdrv->stopAsyncReceive();

    m_driver->closeChannel();

    m_connected = false;
    m_paused    = false;
    emit connectedChanged();
    emit pausedChanged();

    setStatus("Disconnected");
}

// ============================================================================
//  Measurement Control (Start / Stop / Pause)
// ============================================================================

void AppController::startMeasurement()
{
    // Toggle: if already measuring, stop
    if (m_measuring) {
        stopMeasurement();
        return;
    }

    // -----------------------------------------------------------------------
    //  Auto-connect if not yet connected.
    //  WHY: lets the user press Start directly without a separate Connect
    //  step — common workflow for "just show me the bus traffic quickly".
    // -----------------------------------------------------------------------
    if (!m_connected) {
        connectChannels();
        if (!m_connected) return;   // connection failed
    }

    m_measuring = true;
    m_paused    = false;
    m_measureStart.start();
    m_pending.clear();    // discard any stale frames from before Start
    m_framesSinceLastSec = 0;

    m_flushTimer.start();
    m_rateTimer.start();

    emit measuringChanged();
    emit pausedChanged();

    qDebug() << "[startMeasurement] measuring=true, flushTimer active=" << m_flushTimer.isActive();
    setStatus("Measuring — capturing CAN frames...");
}

void AppController::stopMeasurement()
{
    if (!m_measuring) return;

    m_flushTimer.stop();
    m_rateTimer.stop();
    m_pending.clear();

    m_measuring = false;
    m_paused    = false;

    emit measuringChanged();
    emit pausedChanged();
    emit frameRateChanged();

    setStatus(QString("Stopped — %1 frames captured").arg(m_traceModel.frameCount()));
}

void AppController::pauseMeasurement()
{
    if (!m_measuring) return;

    m_paused = !m_paused;
    emit pausedChanged();

    if (!m_paused) {
        // On resume: flush any queued-while-paused frames immediately
        flushPendingFrames();
        setStatus("Measurement resumed");
    } else {
        setStatus("Measurement paused — frames queuing");
    }
}

void AppController::setInPlaceDisplayMode(bool enabled)
{
    if (m_inPlaceDisplayMode == enabled) return;

    const int oldCount = m_traceModel.frameCount();
    m_inPlaceDisplayMode = enabled;
    m_traceModel.setDisplayMode(
        enabled ? TraceModel::DisplayMode::InPlace
                : TraceModel::DisplayMode::Append);
    if (m_traceModel.frameCount() != oldCount)
        emit frameCountChanged();

    emit inPlaceDisplayModeChanged();
    saveSettings();

    setStatus(enabled
                  ? "Display mode: In-Place (latest value per frame)"
                  : "Display mode: Append (every frame as new row)");
}

void AppController::toggleDisplayMode()
{
    setInPlaceDisplayMode(!m_inPlaceDisplayMode);
}

// ============================================================================
//  Per-Channel Configuration
// ============================================================================

QVariantList AppController::getChannelConfigs() const
{
    QVariantList list;
    list.reserve(MAX_CHANNELS);
    for (int i = 0; i < MAX_CHANNELS; ++i)
        list.append(m_channelConfigs[i].toVariantMap());
    return list;
}

void AppController::applyChannelConfigs(const QVariantList& configs)
{
    // -----------------------------------------------------------------------
    //  Store the per-channel configs from the dialog.
    //  We accept 1-4 entries; missing entries keep their current values.
    // -----------------------------------------------------------------------
    const int count = qMin(static_cast<int>(configs.size()), MAX_CHANNELS);
    for (int i = 0; i < count; ++i) {
        const QVariantMap m = configs[i].toMap();
        m_channelConfigs[i] = CANChannelUserConfig::fromVariantMap(m);
    }

    // Merge all configured DBCs into the decode database
    rebuildMergedDbc();

    // If connected, re-feed merged DBC to Demo driver so simulation updates
    if (auto* demoDrv = qobject_cast<DemoCANDriver*>(m_driver))
        if (m_connected) demoDrv->setSimulationDatabase(m_dbcDb);

    // -----------------------------------------------------------------------
    //  Auto-save: persist configs immediately so they survive app restart.
    //
    //  WHY here: applyChannelConfigs() is the single entry-point for all
    //  CAN Config dialog changes.  Saving here means the user never needs
    //  a dedicated "Save" button — it works exactly like real CANoe does.
    // -----------------------------------------------------------------------
    saveSettings();

    setStatus("Channel configuration saved");
    qDebug() << "[AppController] Channel configs applied. DBC:" << m_dbcInfo;
}

QString AppController::preloadChannelDbc(int ch, const QString& filePath)
{
    // -----------------------------------------------------------------------
    //  Called from the CAN Config dialog when the user picks a DBC file
    //  for a specific channel. We parse it immediately and return an info
    //  string so the dialog can display it without waiting for Apply.
    // -----------------------------------------------------------------------
    if (ch < 0 || ch >= MAX_CHANNELS) return {};

    const QString path = stripFileUrl(filePath);
    QFileInfo fi(path);

    if (!fi.exists()) {
        emit errorOccurred("DBC file not found: " + path);
        return {};
    }

    DBCParser parser;
    m_channelDbs[ch] = parser.parseFile(path);

    if (parser.hasErrors()) {
        qWarning() << "[AppController] DBC parse warnings for CH" << (ch+1) << ":";
        for (const auto& e : parser.errors())
            qWarning() << "  Line" << e.line << ":" << e.message;
    }

    // Build the summary string shown in the dialog: "vehicle.dbc | 42 msg | 312 sig"
    const QString info = QString("%1  |  %2 msg  |  %3 sig")
                             .arg(fi.fileName())
                             .arg(m_channelDbs[ch].messages.size())
                             .arg(m_channelDbs[ch].totalSignalCount());

    // Store into channel config so applyChannelConfigs() can retrieve it
    m_channelConfigs[ch].dbcFilePath = path;
    m_channelConfigs[ch].dbcInfo     = info;

    qDebug() << "[AppController] CH" << (ch+1) << "DBC preloaded:" << info;
    return info;
}

// ============================================================================
//  Rebuild merged DBC from all enabled channels
// ============================================================================

void AppController::rebuildMergedDbc()
{
    // -----------------------------------------------------------------------
    //  Merge all enabled channels' DBC databases into one global m_dbcDb.
    //
    //  WHY merge: the trace receives frames from all channels mixed together.
    //  A single lookup database is faster than per-channel branching in the
    //  hot path (buildEntry() called for every received frame).
    //
    //  WHY also check dbcFilePath: if a channel is enabled but no DBC was
    //  pre-loaded yet (e.g. first-time connect), try loading from the stored
    //  file path. This handles the case where applyChannelConfigs() is called
    //  before preloadChannelDbc().
    // -----------------------------------------------------------------------
    m_dbcDb = DBCDatabase();
    m_dbcInfo.clear();

    QStringList infoParts;
    int totalMsg = 0, totalSig = 0;

    for (int i = 0; i < MAX_CHANNELS; ++i) {
        if (!m_channelConfigs[i].enabled) continue;
        if (m_channelConfigs[i].dbcFilePath.isEmpty()) continue;

        // Lazy-load: parse DBC if not already loaded for this channel
        if (m_channelDbs[i].isEmpty() && !m_channelConfigs[i].dbcFilePath.isEmpty()) {
            DBCParser parser;
            m_channelDbs[i] = parser.parseFile(m_channelConfigs[i].dbcFilePath);
        }

        if (m_channelDbs[i].isEmpty()) continue;

        // Merge into m_dbcDb (append all messages from this channel's DBC,
        // then rebuild the internal ID→index hash so messageById() works)
        for (const auto& msg : m_channelDbs[i].messages)
            m_dbcDb.messages.push_back(msg);

        totalMsg += m_channelDbs[i].messages.size();
        totalSig += m_channelDbs[i].totalSignalCount();

        const QFileInfo fi(m_channelConfigs[i].dbcFilePath);
        infoParts.append(QString("CH%1: %2").arg(i+1).arg(fi.fileName()));
    }

    if (!m_dbcDb.isEmpty()) {
        // Rebuild the internal ID hash so messageById() works correctly
        // after all channel DBCs have been appended into messages[]
        m_dbcDb.buildIndex();

        m_dbcInfo = infoParts.join(" | ") +
                    QString("  [%1 msg, %2 sig total]").arg(totalMsg).arg(totalSig);
        emit dbcLoadedChanged();
        emit dbcInfoChanged();
        qDebug() << "[AppController] Merged DBC:" << m_dbcInfo;
    }
}

// ============================================================================
//  Legacy DBC Load (global, no channel assignment)
// ============================================================================

void AppController::loadDbc(const QString& filePath)
{
    const QString path = stripFileUrl(filePath);
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

    if (auto* demoDrv = qobject_cast<DemoCANDriver*>(m_driver))
        demoDrv->setSimulationDatabase(m_dbcDb);

    m_dbcInfo = QString("%1  |  %2 msg  |  %3 sig")
                    .arg(fi.fileName())
                    .arg(m_dbcDb.messages.size())
                    .arg(m_dbcDb.totalSignalCount());

    emit dbcLoadedChanged();
    emit dbcInfoChanged();
    setStatus("DBC loaded: " + m_dbcInfo);
    qDebug() << "[AppController] DBC loaded (global):" << m_dbcInfo;
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

bool AppController::importTraceLog(const QString& filePath, bool append)
{
    const QString path = stripFileUrl(filePath);
    const QFileInfo fi(path);

    if (!fi.exists()) {
        const QString err = QString("Trace file not found: %1").arg(path);
        setStatus(err);
        emit errorOccurred(err);
        return false;
    }

    QVector<CANMessage> importedFrames;
    const QString importErr = TraceImporter::load(path, importedFrames);
    if (!importErr.isEmpty()) {
        setStatus("Import failed: " + importErr);
        emit errorOccurred(importErr);
        return false;
    }

    if (importedFrames.isEmpty()) {
        const QString err =
            QString("No CAN frames found in %1").arg(fi.fileName());
        setStatus(err);
        emit errorOccurred(err);
        return false;
    }

    if (m_measuring)
        stopMeasurement();

    m_pending.clear();
    m_framesSinceLastSec = 0;
    if (m_frameRate != 0) {
        m_frameRate = 0;
        emit frameRateChanged();
    }

    if (!append)
        m_traceModel.clear();

    QVector<TraceEntry> entries;
    entries.reserve(importedFrames.size());
    for (const auto& frame : importedFrames)
        entries.append(buildEntry(frame));

    m_traceModel.addEntries(entries);
    emit frameCountChanged();

    setStatus(QString("Offline trace %1: %2 (%3 frames)")
                  .arg(append ? "appended" : "loaded")
                  .arg(fi.fileName())
                  .arg(importedFrames.size()));

    return true;
}

void AppController::saveTrace(const QString& filePath)
{
    const QString path = stripFileUrl(filePath);
    const QFileInfo fi(path);

    // ── Dispatch to the correct exporter based on file extension ──────────────
    //
    //  WHY extension-based dispatch: the QML FileDialog passes back the full
    //  file path including the extension the user chose from the filter list.
    //  Checking the suffix here keeps all format logic in one place and lets
    //  the same QML button work for CSV, ASC, and BLF without any QML changes.
    //
    const QString ext = fi.suffix().toLower();
    QString err;

    if (ext == "asc")
    {
        // ── Vector ASC (ASCII Log) ─────────────────────────────────────────
        // Human-readable text format.  Opens in CANalyzer or any text editor.
        err = TraceExporter::saveAsAsc(path, m_traceModel.frames());
    }
    else if (ext == "blf")
    {
        // ── Vector BLF (Binary Log File) ──────────────────────────────────
        // Compact binary format.  Preferred for large traces and automated
        // test toolchains.  Opens in CANalyzer / CANoe / python-can.
        err = TraceExporter::saveAsBLF(path, m_traceModel.frames());
    }
    else
    {
        // ── CSV (default, and fallback for unknown extensions) ─────────────
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            err = QString("Cannot open for writing: %1").arg(path);
        } else {
            QTextStream out(&file);
            out << "Time(ms),Name,ID,Chn,EventType,Dir,DLC,Data\n";

            const int rows = m_traceModel.frameCount();
            for (int r = 0; r < rows; ++r) {
                auto cell = [&](int col) -> QString {
                    return m_traceModel.data(
                        m_traceModel.index(r, col), Qt::DisplayRole).toString();
                };
                auto quoted = [](const QString& s) -> QString {
                    if (s.contains(',') || s.contains('"'))
                        return "\"" + QString(s).replace("\"", "\"\"") + "\"";
                    return s;
                };
                out << cell(0) << "," << cell(1) << "," << cell(2) << ","
                    << cell(3) << "," << cell(4) << "," << cell(5) << ","
                    << cell(6) << "," << quoted(cell(7)) << "\n";
            }
            file.close();
        }
    }

    // ── Report result ──────────────────────────────────────────────────────────
    if (!err.isEmpty()) {
        setStatus("Save failed: " + err);
        emit errorOccurred(err);
    } else {
        setStatus(QString("Trace saved: %1  (%2 frames)  [%3]")
                      .arg(fi.fileName())
                      .arg(m_traceModel.frameCount())
                      .arg(ext.toUpper()));
    }
}

void AppController::sendFrame(quint32 id, const QString& hexData, bool extended)
{
    if (!m_connected) {
        emit errorOccurred("Not connected — cannot send");
        return;
    }

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
//  Frame Reception
// ============================================================================

void AppController::onFrameReceived(const CANMessage& msg)
{
    // -----------------------------------------------------------------------
    //  Discard frames when not measuring.
    //
    //  WHY check here rather than in flushPendingFrames(): we don't want
    //  m_pending to grow unboundedly when connected-but-not-measuring.
    //  Dropping here keeps memory usage O(batch size) not O(time connected).
    // -----------------------------------------------------------------------
    if (!m_measuring || m_paused) return;

    if (msg.isTxConfirm) return;   // skip TX echoes (optional — could expose as setting)

    m_pending.append(msg);
    ++m_framesSinceLastSec;
}

// ============================================================================
//  50 ms flush timer — batch insert into TraceModel
// ============================================================================

void AppController::flushPendingFrames()
{
    if (m_pending.isEmpty()) return;

    // While paused, m_pending accumulates but we don't flush until resume.
    // pauseMeasurement() calls flushPendingFrames() manually on resume.
    if (m_paused) return;

    QVector<CANMessage> batch = std::move(m_pending);
    m_pending.clear();

    qDebug() << "[Flush] batch=" << batch.size()
             << "measuring=" << m_measuring
             << "mode=" << (m_inPlaceDisplayMode ? "InPlace" : "Append")
             << "frames_before=" << m_traceModel.frameCount();

    QVector<TraceEntry> entries;
    entries.reserve(batch.size());
    for (const auto& msg : batch)
        entries.append(buildEntry(msg));

    m_traceModel.addEntries(entries);
    emit frameCountChanged();

    qDebug() << "[Flush] frames_after=" << m_traceModel.frameCount();
}

// ============================================================================
//  Build one TraceEntry from a raw CANMessage
// ============================================================================

TraceEntry AppController::buildEntry(const CANMessage& msg) const
{
    TraceEntry e;
    e.msg = msg;

    // Col 0: Relative timestamp (hardware ns → display ms with 6 decimal places)
    const double relMs = static_cast<double>(msg.timestamp) / 1.0e6;
    e.timeStr = QString::number(relMs, 'f', 6);

    // Col 2: CAN ID — CANoe format "0C4h" (std) / "18DB33F1h" (ext)
    if (msg.isExtended)
        e.idStr = QString("%1h").arg(msg.id, 8, 16, QChar('0')).toUpper();
    else
        e.idStr = QString("%1h").arg(msg.id, 3, 16, QChar('0')).toUpper();

    // Col 3: Channel number
    e.chnStr = QString::number(msg.channel);

    // Col 4: Event type (priority: Error > Remote > FD variants > CAN)
    if (msg.isError)
        e.eventTypeStr = QStringLiteral("Error Frame");
    else if (msg.isRemote)
        e.eventTypeStr = QStringLiteral("Remote Frame");
    else if (msg.isFD)
        e.eventTypeStr = msg.isBRS ? QStringLiteral("CAN FD BRS") : QStringLiteral("CAN FD");
    else
        e.eventTypeStr = QStringLiteral("CAN");

    // Col 5: Direction
    e.dirStr = msg.isTxConfirm ? QStringLiteral("Tx") : QStringLiteral("Rx");

    // Col 6: DLC (FD: show actual byte count to avoid DLC code confusion)
    e.dlcStr = (msg.isFD && msg.dlc > 8)
        ? QString::number(msg.dataLength())
        : QString::number(msg.dlc);

    // Col 7: Data bytes (hex dump, space-separated, uppercase)
    {
        const int len = msg.dataLength();
        QString dataStr;
        dataStr.reserve(len * 3);
        for (int i = 0; i < len; ++i) {
            if (i > 0) dataStr += ' ';
            dataStr += QString("%1").arg(msg.data[i], 2, 16, QChar('0')).toUpper();
        }
        e.dataStr = dataStr;
    }

    // DBC decode → Col 1 name + signal child rows
    if (!m_dbcDb.isEmpty()) {
        const DBCMessage* dbcMsg = m_dbcDb.messageById(msg.id);
        if (dbcMsg) {
            e.nameStr = dbcMsg->name;

            const int dataLen = msg.dataLength();
            e.decodedSignals.reserve(dbcMsg->signalList.size());

            // Evaluate mux selector first (muxIndicator == "M")
            bool    hasMuxSelector = false;
            int64_t activeMuxRaw   = -1;
            for (const auto& sig : dbcMsg->signalList) {
                if (sig.muxIndicator == QStringLiteral("M")) {
                    hasMuxSelector = true;
                    activeMuxRaw   = sig.rawValue(msg.data, dataLen);
                    break;
                }
            }

            for (const auto& sig : dbcMsg->signalList) {
                const bool isMuxSel = (sig.muxIndicator == QStringLiteral("M"));
                const bool isMuxed  = !sig.muxIndicator.isEmpty() && !isMuxSel;

                // Skip muxed signals not belonging to the active mux branch
                if (isMuxed && hasMuxSelector && sig.muxValue >= 0
                    && sig.muxValue != activeMuxRaw)
                    continue;

                const int64_t rawValue     = sig.rawValue(msg.data, dataLen);
                const double  physicalVal  = sig.decode(msg.data, dataLen);

                QString valueText = QString::number(physicalVal, 'g', 8);
                if (!sig.unit.isEmpty()) valueText += " " + sig.unit;
                if (sig.valueDescriptions.contains(rawValue))
                    valueText += QString(" (%1)").arg(sig.valueDescriptions.value(rawValue));

                SignalRow sr;
                sr.name     = sig.name;
                sr.valueStr = valueText;
                sr.rawStr   = QString("0x%1").arg(rawValue, 0, 16, QChar('0')).toUpper();
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

    setStatus(QString("Measuring: %1 fps  |  %2 frames total")
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

void AppController::setInitStatus(const QString& text)
{
    // Update both the splash-screen property and the toolbar status so that
    // after the splash fades the toolbar already shows the latest state.
    if (m_initStatus != text) {
        m_initStatus = text;
        emit initStatusChanged();
    }
    setStatus(text);
}

// ============================================================================
//  Settings Persistence (QSettings)
//
//  QSettings with no arguments uses:
//    Organisation = QCoreApplication::organizationName() = "AutoLens"
//    Application  = QCoreApplication::applicationName()  = "AutoLens"
//  Storage location:
//    Windows : HKEY_CURRENT_USER\Software\AutoLens\AutoLens
//    Linux   : ~/.config/AutoLens/AutoLens.conf
//    macOS   : ~/Library/Preferences/com.autolens.AutoLens.plist
// ============================================================================

void AppController::loadSettings()
{
    // -----------------------------------------------------------------------
    //  Restore per-channel configs saved in the previous session.
    //
    //  WHY check settings.contains("alias") before restoring:
    //  On a first run there is nothing in the registry yet.  Without the
    //  guard, every field would default to its fallback value, which is fine
    //  — but we also need the guard to distinguish "saved empty string" from
    //  "never saved".  Checking for "alias" (always written when saving) is
    //  a reliable sentinel for "this channel was configured before".
    // -----------------------------------------------------------------------
    QSettings settings;
    settings.beginGroup(QStringLiteral("Channels"));

    for (int i = 0; i < MAX_CHANNELS; ++i) {
        settings.beginGroup(QString("channel%1").arg(i));

        if (settings.contains(QStringLiteral("alias"))) {
            // Something was previously saved for this slot — restore it.
            m_channelConfigs[i].enabled        = settings.value("enabled",        false).toBool();
            m_channelConfigs[i].alias          = settings.value("alias",          QString("CH%1").arg(i+1)).toString();
            m_channelConfigs[i].hwChannelIndex = settings.value("hwChannelIndex", -1).toInt();
            m_channelConfigs[i].fdEnabled      = settings.value("fdEnabled",      false).toBool();
            m_channelConfigs[i].bitrate        = settings.value("bitrate",        500000).toInt();
            m_channelConfigs[i].dataBitrate    = settings.value("dataBitrate",    2000000).toInt();
            m_channelConfigs[i].dbcFilePath    = settings.value("dbcFilePath",    "").toString();
            m_channelConfigs[i].dbcInfo        = settings.value("dbcInfo",        "").toString();
        }
        // else: slot stays at constructor defaults (alias already set above)

        settings.endGroup();
    }

    settings.endGroup();

    // Trace display mode (false=append, true=in-place)
    m_inPlaceDisplayMode = settings.value("Trace/inPlaceDisplayMode", false).toBool();
    qDebug() << "[AppController] Settings loaded from persistent store";
}

void AppController::saveSettings()
{
    // -----------------------------------------------------------------------
    //  Persist all 4 channel configs.
    //
    //  WHY settings.sync() at the end:
    //  QSettings batches writes for performance.  sync() flushes them to
    //  disk/registry immediately.  Without it, a hard crash right after
    //  Apply could silently discard the changes.
    // -----------------------------------------------------------------------
    QSettings settings;
    settings.beginGroup(QStringLiteral("Channels"));

    for (int i = 0; i < MAX_CHANNELS; ++i) {
        settings.beginGroup(QString("channel%1").arg(i));
        settings.setValue(QStringLiteral("enabled"),        m_channelConfigs[i].enabled);
        settings.setValue(QStringLiteral("alias"),          m_channelConfigs[i].alias);
        settings.setValue(QStringLiteral("hwChannelIndex"), m_channelConfigs[i].hwChannelIndex);
        settings.setValue(QStringLiteral("fdEnabled"),      m_channelConfigs[i].fdEnabled);
        settings.setValue(QStringLiteral("bitrate"),        m_channelConfigs[i].bitrate);
        settings.setValue(QStringLiteral("dataBitrate"),    m_channelConfigs[i].dataBitrate);
        settings.setValue(QStringLiteral("dbcFilePath"),    m_channelConfigs[i].dbcFilePath);
        settings.setValue(QStringLiteral("dbcInfo"),        m_channelConfigs[i].dbcInfo);
        settings.endGroup();
    }

    settings.endGroup();
    settings.setValue("Trace/inPlaceDisplayMode", m_inPlaceDisplayMode);
    settings.sync();  // flush to disk right now
    qDebug() << "[AppController] Settings saved to persistent store";
}

// -----------------------------------------------------------------------
//  Window geometry — saved on close, restored on startup.
//
//  WHY save *normal* geometry only (not maximized size)?
//  A maximized Qt window on Windows reports inflated x/y (e.g. -8,-8) and
//  an oversized w/h.  If we blindly restore those, the next session starts
//  with an off-screen window.  QML therefore tracks the normal geometry
//  separately (see Main.qml) and passes it here alongside the maximized flag.
// -----------------------------------------------------------------------

void AppController::saveWindowState(int x, int y, int w, int h, bool maximized)
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("Window"));
    settings.setValue(QStringLiteral("x"),         x);
    settings.setValue(QStringLiteral("y"),         y);
    settings.setValue(QStringLiteral("width"),     w);
    settings.setValue(QStringLiteral("height"),    h);
    settings.setValue(QStringLiteral("maximized"), maximized);
    settings.endGroup();
    settings.sync();
    qDebug() << "[AppController] Window state saved:" << x << y << w << h
             << (maximized ? "(maximized)" : "(normal)");
}

QVariantMap AppController::loadWindowState()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("Window"));

    QVariantMap result;
    // hasGeometry: false on first run — QML will keep its hard-coded defaults
    result[QStringLiteral("hasGeometry")] = settings.contains(QStringLiteral("width"));
    result[QStringLiteral("x")]           = settings.value(QStringLiteral("x"),         100).toInt();
    result[QStringLiteral("y")]           = settings.value(QStringLiteral("y"),         100).toInt();
    result[QStringLiteral("w")]           = settings.value(QStringLiteral("width"),    1280).toInt();
    result[QStringLiteral("h")]           = settings.value(QStringLiteral("height"),    760).toInt();
    result[QStringLiteral("maximized")]   = settings.value(QStringLiteral("maximized"), false).toBool();

    settings.endGroup();
    return result;
}

// -----------------------------------------------------------------------
//  Theme preference — saved every time the user toggles day/night.
// -----------------------------------------------------------------------

void AppController::saveTheme(bool isDayTheme)
{
    QSettings settings;
    // Store directly at top level under "theme/isDayTheme" — no group needed
    // for a single boolean value.
    settings.setValue(QStringLiteral("theme/isDayTheme"), isDayTheme);
    settings.sync();
    qDebug() << "[AppController] Theme saved:" << (isDayTheme ? "day" : "night");
}

bool AppController::loadTheme()
{
    QSettings settings;
    // Default: true (light/day theme) — matches the QML property initialiser.
    return settings.value(QStringLiteral("theme/isDayTheme"), true).toBool();
}
