#pragma once
/**
 * @file AppController.h
 * @brief Central C++↔QML bridge for AutoLens.
 *
 * AppController is the single object that QML talks to for everything:
 *
 *   QML reads properties:
 *     AppController.connected       — is hardware port open?
 *     AppController.measuring       — are frames actively being captured?
 *     AppController.driverName      — "Vector XL" or "Demo"
 *     AppController.channelList     — ["VN1630A CH1 SN:12345", "Demo Channel 1"]
 *     AppController.dbcLoaded       — true once a .dbc is parsed
 *     AppController.dbcInfo         — "vehicle.dbc | 42 msg | 312 sig"
 *     AppController.statusText      — one-line status for the toolbar
 *     AppController.frameCount      — total frames in trace
 *     AppController.frameRate       — frames/s (updated every second)
 *     AppController.traceModel      — bound to the QML TreeView
 *
 *   QML calls methods:
 *     AppController.connectChannels()            — open HW port (connect to bus)
 *     AppController.disconnectChannels()         — close HW port (go off bus)
 *     AppController.startMeasurement()           — begin capturing + displaying frames
 *     AppController.stopMeasurement()            — stop capturing (stay connected)
 *     AppController.applyChannelConfigs(list)    — save per-channel settings from dialog
 *     AppController.getChannelConfigs()          — read per-channel settings (for dialog init)
 *     AppController.preloadChannelDbc(ch, path)  — parse DBC for a channel, return info string
 *     AppController.loadDbc(filePath)            — [legacy] global DBC load
 *     AppController.clearTrace()                 — empty the trace table
 *     AppController.sendFrame(id, data, ext)     — transmit one frame
 *
 * ──────────────────────────────────────────────────────────────────────────
 *  CONNECT vs START — two separate user actions (like real CANoe):
 *
 *  [Connect]  → Open the HW port, go on-bus.  Frames arrive but are NOT
 *               displayed yet (measuring = false, frames are discarded).
 *
 *  [Start]    → Begin capturing.  Frames flow into the trace display.
 *               Requires being Connected first.
 *
 *  [Stop]     → Stop capturing.  Stay connected (port stays open).
 *
 *  [Disconnect] → Go off-bus.  Closes the HW port.  Also stops measuring.
 *
 * ──────────────────────────────────────────────────────────────────────────
 *  Threading
 * ─────────
 *  AppController lives on the UI thread.
 *  VectorCANDriver's async thread emits messageReceived(CANMessage) which
 *  Qt delivers to onFrameReceived() via a queued connection (automatic for
 *  cross-thread).  AppController accumulates frames in m_pending and a
 *  50 ms QTimer flushes them into TraceModel in a single batch, keeping
 *  the UI smooth even at high bus loads.
 */

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QTimer>
#include <QThread>
#include <QElapsedTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QSettings>  // WHY: persistent key-value store; Qt6::Core, no extra deps
#include <array>

#include "hardware/CANInterface.h"
#include "dbc/DBCParser.h"
#include "trace/TraceModel.h"

// ============================================================================
//  Per-Channel Configuration
//
//  Stores all user settings for one logical CAN channel.
//  Up to MAX_CHANNELS slots are kept in AppController.
//  Settings come from the CAN Config dialog and are preserved across
//  connect/disconnect cycles.
// ============================================================================

struct CANChannelUserConfig
{
    bool    enabled         = false;
    QString alias;                        ///< User label, e.g. "Engine_Bus"
    int     hwChannelIndex  = -1;         ///< Index into m_channelInfos (-1 = auto/none)
    bool    fdEnabled       = false;      ///< CAN FD mode
    int     bitrate         = 500000;     ///< Nominal bitrate in bit/s (default 500 kbit/s)
    int     dataBitrate     = 2000000;    ///< FD data-phase bitrate in bit/s (default 2 Mbit/s)
    QString dbcFilePath;                  ///< Filesystem path to the DBC file for this channel
    QString dbcInfo;                      ///< Pre-computed summary: "file.dbc | 42 msg | 312 sig"

    // Convert to QVariantMap for QML (JSON-like object readable in JS)
    QVariantMap toVariantMap() const {
        return {
            { "enabled",        enabled        },
            { "alias",          alias          },
            { "hwChannelIndex", hwChannelIndex },
            { "fdEnabled",      fdEnabled      },
            { "bitrate",        bitrate        },
            { "dataBitrate",    dataBitrate    },
            { "dbcFilePath",    dbcFilePath    },
            { "dbcInfo",        dbcInfo        }
        };
    }

    // Populate from QVariantMap (used by applyChannelConfigs)
    static CANChannelUserConfig fromVariantMap(const QVariantMap& m) {
        CANChannelUserConfig cfg;
        cfg.enabled        = m.value("enabled",        false).toBool();
        cfg.alias          = m.value("alias",          "").toString();
        cfg.hwChannelIndex = m.value("hwChannelIndex", -1).toInt();
        cfg.fdEnabled      = m.value("fdEnabled",      false).toBool();
        cfg.bitrate        = m.value("bitrate",        500000).toInt();
        cfg.dataBitrate    = m.value("dataBitrate",    2000000).toInt();
        cfg.dbcFilePath    = m.value("dbcFilePath",    "").toString();
        cfg.dbcInfo        = m.value("dbcInfo",        "").toString();
        return cfg;
    }
};

// ============================================================================
//  AppController
// ============================================================================

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

    Q_PROPERTY(bool        connected    READ connected    NOTIFY connectedChanged)
    Q_PROPERTY(bool        measuring    READ measuring    NOTIFY measuringChanged)
    Q_PROPERTY(bool        paused       READ paused       NOTIFY pausedChanged)
    Q_PROPERTY(QString     driverName   READ driverName   NOTIFY driverNameChanged)
    Q_PROPERTY(QStringList channelList  READ channelList  NOTIFY channelListChanged)

    Q_PROPERTY(bool    dbcLoaded  READ dbcLoaded  NOTIFY dbcLoadedChanged)
    Q_PROPERTY(QString dbcInfo    READ dbcInfo    NOTIFY dbcInfoChanged)

    Q_PROPERTY(QString statusText  READ statusText  NOTIFY statusTextChanged)
    Q_PROPERTY(int     frameCount  READ frameCount  NOTIFY frameCountChanged)
    Q_PROPERTY(int     frameRate   READ frameRate   NOTIFY frameRateChanged)

    /** The model that QML's TreeView binds to. CONSTANT = pointer never changes. */
    Q_PROPERTY(TraceModel* traceModel READ traceModel CONSTANT)

public:
    static constexpr int MAX_CHANNELS = 4; ///< Maximum configurable CAN channels

    explicit AppController(QObject* parent = nullptr);
    ~AppController() override;

    // --- Property getters ---
    bool        connected()   const { return m_connected; }
    bool        measuring()   const { return m_measuring; }
    bool        paused()      const { return m_paused; }
    QString     driverName()  const;
    QStringList channelList() const { return m_channelList; }
    bool        dbcLoaded()   const { return !m_dbcDb.isEmpty(); }
    QString     dbcInfo()     const { return m_dbcInfo; }
    QString     statusText()  const { return m_statusText; }
    int         frameCount()  const { return m_traceModel.frameCount(); }
    int         frameRate()   const { return m_frameRate; }
    TraceModel* traceModel()        { return &m_traceModel; }

public slots:
    // -----------------------------------------------------------------------
    //  Hardware Connection
    // -----------------------------------------------------------------------

    /** Re-scan hardware for available CAN channels (runs in background thread). */
    void refreshChannels();

    /**
     * @brief Open the CAN port(s) based on the current channel configs.
     *
     * Uses the first enabled channel config.  If no channel is configured,
     * defaults to the first available HW channel with 500 kbit/s.
     *
     * Sets connected = true. Does NOT start measurement — call startMeasurement()
     * separately after connecting.
     */
    void connectChannels();

    /**
     * @brief Close the CAN port(s) and go off-bus.
     * Also stops measurement if active.
     */
    void disconnectChannels();

    // -----------------------------------------------------------------------
    //  Measurement Control (separate from hardware connection)
    // -----------------------------------------------------------------------

    /**
     * @brief Begin capturing and displaying CAN frames.
     *
     * Requires being connected first.  Sets measuring = true.
     * Starts the 50 ms flush timer so frames appear in the trace view.
     */
    void startMeasurement();

    /**
     * @brief Stop capturing frames (stays connected — port stays open).
     * Sets measuring = false.  Frames arriving after this are discarded.
     */
    void stopMeasurement();

    /**
     * @brief Toggle pause state.
     *
     * While paused, incoming frames are still queued in m_pending but not
     * flushed into TraceModel. On resume the backlog is flushed immediately.
     */
    void pauseMeasurement();

    // -----------------------------------------------------------------------
    //  Channel Configuration
    // -----------------------------------------------------------------------

    /**
     * @brief Return current per-channel configs as a QVariantList.
     *
     * Returns a list of 4 QVariantMap objects (one per channel slot).
     * QML reads this to populate the CAN Config dialog.
     */
    Q_INVOKABLE QVariantList getChannelConfigs() const;

    /**
     * @brief Apply per-channel configs from the CAN Config dialog.
     *
     * @param configs  QVariantList of 4 QVariantMap objects.
     *                 Keys: "enabled", "alias", "hwChannelIndex", "fdEnabled",
     *                       "bitrate", "dataBitrate", "dbcFilePath", "dbcInfo"
     *
     * Merges all configured DBC files into the global decode database.
     * Emits dbcLoadedChanged, dbcInfoChanged.
     */
    Q_INVOKABLE void applyChannelConfigs(const QVariantList& configs);

    /**
     * @brief Parse a DBC file for a specific channel and return an info string.
     *
     * Called from the CAN Config dialog when the user picks a DBC file for a
     * channel.  Parses the file, stores it in m_channelDbs[ch], updates the
     * channel config's dbcInfo field, and returns the info string so the dialog
     * can display it immediately.
     *
     * @param ch        Channel slot index (0–3)
     * @param filePath  "file:///C:/..." or "C:/..." path to the .dbc file
     * @return  Info string like "vehicle.dbc | 42 msg | 312 sig", or "" on error
     */
    Q_INVOKABLE QString preloadChannelDbc(int ch, const QString& filePath);

    // -----------------------------------------------------------------------
    //  DBC / Trace
    // -----------------------------------------------------------------------

    /**
     * @brief [Legacy] Parse a DBC file globally and enable signal decoding.
     *
     * This is the old single-DBC load path.  The new per-channel flow uses
     * preloadChannelDbc() + applyChannelConfigs() instead.
     */
    void loadDbc(const QString& filePath);

    /** Remove all rows from the trace table. */
    void clearTrace();

    /**
     * @brief Export the current trace to a CSV text file.
     * @param filePath  Destination file path (may have "file:///" prefix from QML).
     */
    Q_INVOKABLE void saveTrace(const QString& filePath);

    /**
     * @brief Transmit one CAN frame.
     * @param id       CAN arbitration ID
     * @param hexData  Hex string of bytes, e.g. "AA BB CC 00"
     * @param extended true for 29-bit extended ID
     */
    Q_INVOKABLE void sendFrame(quint32 id, const QString& hexData, bool extended = false);

    // -----------------------------------------------------------------------
    //  Persistent Settings  (QSettings — HKCU\Software\AutoLens\AutoLens on Win)
    //
    //  WHY put window/theme persistence here instead of QML?
    //  QML has no built-in persistent storage.  Qt.labs.settings exists but
    //  is a tech-preview module.  Routing through AppController keeps all
    //  persistent I/O in one place and makes it testable in C++.
    // -----------------------------------------------------------------------

    /**
     * @brief Save the main window's normal (non-maximized) geometry and
     *        whether it was maximized when closed.
     *
     * Called from QML's onClosing handler so the window reopens at the
     * same position next time.
     *
     * @param x,y       Top-left corner of the *normal* (restored) window.
     * @param w,h       Size of the *normal* window.
     * @param maximized Whether the window was maximized at close time.
     */
    Q_INVOKABLE void saveWindowState(int x, int y, int w, int h, bool maximized);

    /**
     * @brief Load the previously saved window geometry.
     *
     * Returns a QVariantMap with keys:
     *   "hasGeometry" — bool, false on first run (nothing saved yet)
     *   "x", "y"      — int, top-left corner
     *   "w", "h"      — int, window size
     *   "maximized"   — bool
     */
    Q_INVOKABLE QVariantMap loadWindowState();

    /**
     * @brief Persist the day/night theme choice.
     * Called whenever the user toggles the theme button.
     * @param isDayTheme  true = light/day, false = dark/night
     */
    Q_INVOKABLE void saveTheme(bool isDayTheme);

    /**
     * @brief Retrieve the last saved theme preference.
     * @return true (light/day) if nothing was saved yet (sensible default).
     */
    Q_INVOKABLE bool loadTheme();

signals:
    void connectedChanged();
    void measuringChanged();
    void pausedChanged();
    void driverNameChanged();
    void channelListChanged();
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
    // --- Settings persistence ---
    //
    // loadSettings() is called in the constructor BEFORE rebuildMergedDbc()
    // so that saved channel configs (incl. DBC paths) are in place when
    // the initial DBC merge happens.
    //
    // saveSettings() is called inside applyChannelConfigs() so every time
    // the user clicks Apply in the CAN Config dialog, changes are
    // automatically written to disk — no explicit Save button needed.

    void loadSettings();  ///< Restore channel configs from QSettings
    void saveSettings();  ///< Flush channel configs to QSettings

    // --- Helpers ---
    void setStatus(const QString& text);
    TraceEntry buildEntry(const CANManager::CANMessage& msg) const;

    /** Strip "file:///" or "file://" prefix from QML FileDialog URLs. */
    static QString stripFileUrl(const QString& path);

    /**
     * @brief Called (on the UI thread) when the async init thread completes.
     *
     * WHY a separate method instead of inline lambda: QMetaObject::invokeMethod
     * needs to marshal the call across threads.  Passing channel data through
     * a lambda capture is safe since QList is implicitly shared (copy-on-write).
     */
    void applyDriverInitResult(bool ok,
                               const QList<CANManager::CANChannelInfo>& channels);

    /** Rebuild m_dbcDb by merging all enabled channels' DBC databases. */
    void rebuildMergedDbc();

    // --- Driver ---
    CANManager::ICANDriver*            m_driver     = nullptr;
    QThread*                           m_initThread = nullptr;
    QList<CANManager::CANChannelInfo>  m_channelInfos;
    QStringList                        m_channelList;

    // --- State ---
    bool    m_connected  = false;
    bool    m_measuring  = false;
    bool    m_paused     = false;
    QString m_statusText;

    // --- Per-channel configuration (from CAN Config dialog) ---
    std::array<CANChannelUserConfig, MAX_CHANNELS>    m_channelConfigs;
    std::array<DBCManager::DBCDatabase, MAX_CHANNELS> m_channelDbs;

    // --- Merged DBC (all enabled channels merged into one decode database) ---
    DBCManager::DBCDatabase m_dbcDb;
    QString                 m_dbcInfo;

    // --- Trace model ---
    TraceModel m_traceModel;

    // --- Batching ---
    QVector<CANManager::CANMessage> m_pending;
    QTimer   m_flushTimer;   ///< 50 ms → flushPendingFrames()
    QTimer   m_rateTimer;    ///< 1000 ms → updateFrameRate()
    QElapsedTimer m_measureStart;

    // --- Stats ---
    int m_frameRate          = 0;
    int m_framesSinceLastSec = 0;
};
