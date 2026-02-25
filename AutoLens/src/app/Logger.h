#pragma once
/**
 * @file Logger.h
 * @brief Centralized crash-resilient logging system for AutoLens.
 *
 * Provides a singleton logger that:
 *   1. Captures ALL Qt message output (qDebug, qInfo, qWarning, qCritical, qFatal)
 *   2. Writes to rotating log files with timestamps and severity levels
 *   3. Flushes immediately on warnings/errors so crash dumps are complete
 *   4. Keeps the last N log files for post-mortem analysis
 *   5. Records a crash marker file if the app exits abnormally
 *   6. Thread-safe — can be called from any thread (CAN receive, DBC parse, UI)
 *
 * Usage:
 *   In main.cpp, call Logger::install() before any other code.
 *   On shutdown, call Logger::shutdown() to flush and close cleanly.
 *
 * Log files are stored in:
 *   Windows: %LOCALAPPDATA%/AutoLens/logs/
 *   Linux:   ~/.local/share/AutoLens/logs/
 *   macOS:   ~/Library/Application Support/AutoLens/logs/
 *
 * File naming:  autolens_YYYYMMDD_HHmmss.log
 * Crash marker: autolens_crash_marker.txt  (deleted on clean shutdown)
 *
 * Architecture:
 *   Logger installs a custom Qt message handler via qInstallMessageHandler().
 *   All qDebug/qInfo/qWarning/qCritical output across the entire app is
 *   routed through this handler automatically — no code changes needed in
 *   existing modules.
 *
 *   The handler is fully re-entrant: a QMutex protects the file I/O so
 *   messages from the CAN receive thread and UI thread are safely interleaved.
 *
 *   On Windows, a structured exception handler (SetUnhandledExceptionFilter)
 *   captures hard crashes (access violations, stack overflows) and writes a
 *   final log line + crash marker before the process terminates.
 */

#include <QString>
#include <QFile>
#include <QMutex>
#include <QElapsedTimer>
#include <QDateTime>

class Logger
{
public:
    // ── Singleton access ──────────────────────────────────────────────────
    static Logger& instance();

    // ── Lifecycle ─────────────────────────────────────────────────────────

    /**
     * @brief Install the logger as the global Qt message handler.
     *
     * Call this once at the very start of main(), before QGuiApplication.
     * It creates the log directory, opens the log file, writes a session
     * header, creates the crash marker, and sets up the SEH handler.
     *
     * @param appVersion  Application version string for the session header.
     */
    static void install(const QString& appVersion = QString());

    /**
     * @brief Cleanly shut down the logger.
     *
     * Writes a session footer with uptime, removes the crash marker,
     * and closes the log file.  Call this just before app.exec() returns.
     */
    static void shutdown();

    // ── Configuration ─────────────────────────────────────────────────────

    /** Maximum number of log files to keep.  Oldest are deleted on startup. */
    static constexpr int MAX_LOG_FILES = 10;

    /** Maximum size of a single log file in bytes (10 MB). */
    static constexpr qint64 MAX_LOG_SIZE = 10 * 1024 * 1024;

    // ── Direct write (for non-Qt log sources) ─────────────────────────────

    /**
     * @brief Write a raw line to the log file (thread-safe).
     *
     * Use this for structured messages that don't go through qDebug(),
     * such as CAN frame statistics summaries or performance counters.
     */
    void write(const QString& line);

    /**
     * @brief Force-flush all buffered data to disk.
     *
     * Called automatically on Warning/Critical/Fatal.
     * Can be called manually before a risky operation.
     */
    void flush();

    /**
     * @brief Get the path to the current log file.
     */
    QString currentLogPath() const;

    /**
     * @brief Get the path to the log directory.
     */
    QString logDirectory() const;

    /**
     * @brief Check if a crash marker from a previous session exists.
     *
     * Returns true if the app crashed last time (marker file found).
     * The marker is removed by shutdown() on clean exit.
     */
    static bool previousSessionCrashed();

    /**
     * @brief Get the crash marker content (timestamp + last log lines).
     */
    static QString previousCrashInfo();

private:
    Logger();
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    // ── Internal helpers ──────────────────────────────────────────────────
    void openLogFile(const QString& appVersion);
    void closeLogFile();
    void createCrashMarker();
    void removeCrashMarker();
    void pruneOldLogs();
    void rotateIfNeeded();
    QString logDirPath() const;
    QString crashMarkerPath() const;

    static void messageHandler(QtMsgType type,
                                const QMessageLogContext& context,
                                const QString& message);

#ifdef Q_OS_WIN
    static void installCrashHandler();
    // Allow the SEH exception filter (free function in Logger.cpp) to
    // access m_mutex, m_logFile, and createCrashMarker() directly.
    friend struct CrashHandlerAccess;
#endif

    // ── State ─────────────────────────────────────────────────────────────
    QFile         m_logFile;
    QMutex        m_mutex;
    QElapsedTimer m_uptime;
    QDateTime     m_sessionStart;
    QString       m_logDir;
    QString       m_currentLogPath;
    bool          m_installed = false;
    int           m_messageCount = 0;
    int           m_warningCount = 0;
    int           m_errorCount   = 0;

    // Ring buffer of last N messages for crash marker
    static constexpr int CRASH_RING_SIZE = 50;
    QString m_crashRing[CRASH_RING_SIZE];
    int     m_crashRingIdx = 0;
};
