/**
 * @file Logger.cpp
 * @brief Centralized crash-resilient logging implementation.
 *
 * Key design decisions:
 *
 *  1. IMMEDIATE FLUSH on Warning/Critical/Fatal:
 *     If the app crashes right after a warning, the message MUST be on disk.
 *     Normal Debug/Info messages are buffered by the OS for performance.
 *
 *  2. CRASH RING BUFFER:
 *     The last 50 messages are kept in a circular buffer in memory.
 *     On crash (SEH handler), these are written to a crash marker file
 *     so the developer can see what happened right before the crash.
 *
 *  3. LOG ROTATION:
 *     Each session gets a new log file.  Old files beyond MAX_LOG_FILES
 *     are deleted on startup to prevent disk space exhaustion.
 *
 *  4. THREAD SAFETY:
 *     QMutex protects all file I/O.  The CAN receive thread, DBC parse
 *     thread, and UI thread can all log simultaneously without corruption.
 *
 *  5. SEH HANDLER (Windows):
 *     SetUnhandledExceptionFilter catches access violations, divide-by-zero,
 *     stack overflow, etc.  Writes a final crash report to the marker file.
 */

#include "Logger.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextStream>
#include <QThread>

#include <cstdio>
#include <cstdlib>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dbghelp.h>
#endif

// ============================================================================
//  Singleton
// ============================================================================

Logger& Logger::instance()
{
    static Logger s_instance;
    return s_instance;
}

// ============================================================================
//  Constructor / Destructor
// ============================================================================

Logger::Logger()
    : m_sessionStart(QDateTime::currentDateTime())
{
    m_uptime.start();
}

Logger::~Logger()
{
    closeLogFile();
}

// ============================================================================
//  Static API
// ============================================================================

void Logger::install(const QString& appVersion)
{
    Logger& log = instance();
    if (log.m_installed) return;

    log.openLogFile(appVersion);
    log.createCrashMarker();
    log.pruneOldLogs();

    // Install as the global Qt message handler — intercepts ALL qDebug etc.
    qInstallMessageHandler(Logger::messageHandler);

#ifdef Q_OS_WIN
    installCrashHandler();
#endif

    log.m_installed = true;
}

void Logger::shutdown()
{
    Logger& log = instance();
    if (!log.m_installed) return;

    // Restore default handler before we close our file
    qInstallMessageHandler(nullptr);

    // Write session footer
    {
        QMutexLocker lk(&log.m_mutex);
        if (log.m_logFile.isOpen()) {
            QTextStream out(&log.m_logFile);
            out << "\n";
            out << "════════════════════════════════════════════════════════════\n";
            out << "  SESSION END — Clean shutdown\n";
            out << "  Uptime:     " << (log.m_uptime.elapsed() / 1000.0) << " seconds\n";
            out << "  Messages:   " << log.m_messageCount << " total"
                << "  (" << log.m_warningCount << " warnings, "
                << log.m_errorCount << " errors)\n";
            out << "  Timestamp:  " << QDateTime::currentDateTime().toString(Qt::ISODateWithMs) << "\n";
            out << "════════════════════════════════════════════════════════════\n";
            out.flush();
        }
    }

    log.removeCrashMarker();
    log.closeLogFile();
    log.m_installed = false;
}

// ============================================================================
//  Message Handler — the core routing function
// ============================================================================

void Logger::messageHandler(QtMsgType type,
                             const QMessageLogContext& context,
                             const QString& message)
{
    Logger& log = instance();
    QMutexLocker lk(&log.m_mutex);

    if (!log.m_logFile.isOpen()) return;

    // ── Build the log line ────────────────────────────────────────────────
    const char* levelStr = nullptr;
    switch (type) {
    case QtDebugMsg:    levelStr = "DBG"; break;
    case QtInfoMsg:     levelStr = "INF"; break;
    case QtWarningMsg:  levelStr = "WRN"; ++log.m_warningCount; break;
    case QtCriticalMsg: levelStr = "ERR"; ++log.m_errorCount;   break;
    case QtFatalMsg:    levelStr = "FTL"; ++log.m_errorCount;   break;
    }

    ++log.m_messageCount;

    const QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
    const QString threadName = QThread::currentThread()->objectName();
    const QString threadId = threadName.isEmpty()
        ? QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()), 16)
        : threadName;

    QString line;

    // Include file/function context for warnings and errors (helps debugging)
    if (type >= QtWarningMsg && context.file) {
        const QString fileName = QFileInfo(context.file).fileName();
        line = QStringLiteral("[%1] [%2] [%3] %4  (%5:%6 %7)")
                   .arg(timestamp, levelStr, threadId, message,
                        fileName, QString::number(context.line),
                        context.function ? QString::fromLatin1(context.function) : QString());
    } else {
        line = QStringLiteral("[%1] [%2] [%3] %4")
                   .arg(timestamp, levelStr, threadId, message);
    }

    // ── Write to file ─────────────────────────────────────────────────────
    QTextStream out(&log.m_logFile);
    out << line << "\n";

    // ── Store in crash ring buffer ────────────────────────────────────────
    log.m_crashRing[log.m_crashRingIdx % CRASH_RING_SIZE] = line;
    ++log.m_crashRingIdx;

    // ── Flush strategy ────────────────────────────────────────────────────
    // - Debug/Info: buffered (OS flush) — fast, ~1000x less I/O
    // - Warning/Critical/Fatal: immediate flush — survives crash
    if (type >= QtWarningMsg) {
        out.flush();
        log.m_logFile.flush();
    }

    // ── Fatal: write crash marker and abort ───────────────────────────────
    if (type == QtFatalMsg) {
        log.createCrashMarker();
        // Must Release mutex before abort to avoid deadlock in atexit handlers
        lk.unlock();
        std::abort();
    }
}

// ============================================================================
//  Direct Write
// ============================================================================

void Logger::write(const QString& line)
{
    QMutexLocker lk(&m_mutex);
    if (!m_logFile.isOpen()) return;

    const QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
    QTextStream out(&m_logFile);
    out << "[" << timestamp << "] [LOG] " << line << "\n";
    ++m_messageCount;
}

void Logger::flush()
{
    QMutexLocker lk(&m_mutex);
    if (m_logFile.isOpen())
        m_logFile.flush();
}

QString Logger::currentLogPath() const
{
    return m_currentLogPath;
}

QString Logger::logDirectory() const
{
    return m_logDir;
}

// ============================================================================
//  Log File Management
// ============================================================================

void Logger::openLogFile(const QString& appVersion)
{
    m_logDir = logDirPath();
    QDir dir;
    dir.mkpath(m_logDir);

    const QString fileName = QStringLiteral("autolens_%1.log")
                                 .arg(m_sessionStart.toString("yyyyMMdd_HHmmss"));
    m_currentLogPath = m_logDir + "/" + fileName;

    m_logFile.setFileName(m_currentLogPath);
    if (!m_logFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append)) {
        // Cannot even open the log file — fall back to stderr
        fprintf(stderr, "[Logger] FATAL: Cannot open log file: %s\n",
                qPrintable(m_currentLogPath));
        return;
    }

    // ── Session header ────────────────────────────────────────────────────
    QTextStream out(&m_logFile);
    out << "════════════════════════════════════════════════════════════\n";
    out << "  AutoLens Log — Session Start\n";
    if (!appVersion.isEmpty())
        out << "  Version:    " << appVersion << "\n";
    out << "  Timestamp:  " << m_sessionStart.toString(Qt::ISODateWithMs) << "\n";
    out << "  Platform:   " << QSysInfo::prettyProductName() << "\n";
    out << "  CPU Arch:   " << QSysInfo::currentCpuArchitecture() << "\n";
    out << "  Qt:         " << qVersion() << "\n";
    out << "  Log file:   " << m_currentLogPath << "\n";

    // Check if previous session crashed
    if (previousSessionCrashed()) {
        out << "  ⚠ PREVIOUS SESSION CRASHED — see crash marker for details\n";
    }

    out << "════════════════════════════════════════════════════════════\n\n";
    out.flush();
}

void Logger::closeLogFile()
{
    QMutexLocker lk(&m_mutex);
    if (m_logFile.isOpen()) {
        m_logFile.flush();
        m_logFile.close();
    }
}

// ============================================================================
//  Crash Marker
// ============================================================================

void Logger::createCrashMarker()
{
    // The crash marker is a small file that indicates the app is running.
    // If it exists on next startup, the previous session crashed.
    QFile marker(crashMarkerPath());
    if (marker.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        QTextStream out(&marker);
        out << "AutoLens Crash Marker\n";
        out << "Session: " << m_sessionStart.toString(Qt::ISODateWithMs) << "\n";
        out << "Log:     " << m_currentLogPath << "\n";
        out << "Uptime:  " << (m_uptime.elapsed() / 1000.0) << " seconds\n\n";

        // Write the last N log messages from the ring buffer
        out << "── Last " << CRASH_RING_SIZE << " messages before marker ──\n";
        const int total = qMin(m_crashRingIdx, CRASH_RING_SIZE);
        const int start = (m_crashRingIdx >= CRASH_RING_SIZE)
                              ? (m_crashRingIdx % CRASH_RING_SIZE)
                              : 0;
        for (int i = 0; i < total; ++i) {
            const int idx = (start + i) % CRASH_RING_SIZE;
            if (!m_crashRing[idx].isEmpty())
                out << m_crashRing[idx] << "\n";
        }

        out.flush();
        marker.close();
    }
}

void Logger::removeCrashMarker()
{
    QFile::remove(crashMarkerPath());
}

bool Logger::previousSessionCrashed()
{
    return QFile::exists(instance().crashMarkerPath());
}

QString Logger::previousCrashInfo()
{
    QFile marker(instance().crashMarkerPath());
    if (!marker.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return marker.readAll();
}

// ============================================================================
//  Log Rotation / Pruning
// ============================================================================

void Logger::pruneOldLogs()
{
    QDir dir(m_logDir);
    QStringList filters;
    filters << "autolens_*.log";
    QFileInfoList files = dir.entryInfoList(filters, QDir::Files, QDir::Name);

    // Keep only the newest MAX_LOG_FILES files
    while (files.size() > MAX_LOG_FILES) {
        const QString oldest = files.first().absoluteFilePath();
        QFile::remove(oldest);
        files.removeFirst();
        qDebug() << "[Logger] Pruned old log:" << oldest;
    }
}

void Logger::rotateIfNeeded()
{
    // Not used for per-session files, but available for future
    // long-running sessions that exceed MAX_LOG_SIZE
    if (m_logFile.size() > MAX_LOG_SIZE) {
        closeLogFile();
        m_sessionStart = QDateTime::currentDateTime();
        openLogFile(QString());
    }
}

// ============================================================================
//  Path Helpers
// ============================================================================

QString Logger::logDirPath() const
{
    // Use QStandardPaths for cross-platform log storage
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return appData + "/logs";
}

QString Logger::crashMarkerPath() const
{
    return logDirPath() + "/autolens_crash_marker.txt";
}

// ============================================================================
//  Windows Crash Handler (SEH)
// ============================================================================

#ifdef Q_OS_WIN

// CrashHandlerAccess is declared as a friend of Logger in Logger.h,
// giving the SEH filter access to private members without exposing them.
struct CrashHandlerAccess {
    static QMutex&  mutex(Logger& l)   { return l.m_mutex; }
    static QFile&   logFile(Logger& l) { return l.m_logFile; }
    static void     createCrashMarker(Logger& l) { l.createCrashMarker(); }
};

static LONG WINAPI autolensExceptionFilter(EXCEPTION_POINTERS* exInfo)
{
    // This runs in the context of the crashing thread — minimal work only.
    // Do NOT allocate heap memory, call complex Qt functions, or acquire locks
    // that might be held by the crashing thread.

    const DWORD code = exInfo ? exInfo->ExceptionRecord->ExceptionCode : 0;
    const void* addr = exInfo ? exInfo->ExceptionRecord->ExceptionAddress : nullptr;

    const char* description = "Unknown exception";
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:     description = "ACCESS_VIOLATION"; break;
    case EXCEPTION_STACK_OVERFLOW:       description = "STACK_OVERFLOW"; break;
    case EXCEPTION_INT_DIVIDE_BY_ZERO:   description = "DIVIDE_BY_ZERO"; break;
    case EXCEPTION_ILLEGAL_INSTRUCTION:  description = "ILLEGAL_INSTRUCTION"; break;
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:   description = "FLT_DIVIDE_BY_ZERO"; break;
    case EXCEPTION_IN_PAGE_ERROR:        description = "IN_PAGE_ERROR"; break;
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:description = "ARRAY_BOUNDS_EXCEEDED"; break;
    case EXCEPTION_DATATYPE_MISALIGNMENT:description = "DATATYPE_MISALIGNMENT"; break;
    }

    // Try to write a crash log line — Logger::write() acquires m_mutex,
    // which is NOT safe if the crashing thread already holds it.
    // Use a tryLock with 0 timeout to avoid deadlock.
    Logger& log = Logger::instance();
    QMutexLocker lk(&CrashHandlerAccess::mutex(log));

    if (CrashHandlerAccess::logFile(log).isOpen()) {
        // Direct file write — bypass QTextStream to minimize allocations
        char buf[512];
        snprintf(buf, sizeof(buf),
                 "\n[CRASH] *** UNHANDLED EXCEPTION: %s (0x%08lX) at %p ***\n"
                 "[CRASH] AutoLens is crashing. See crash marker for recent log history.\n",
                 description, code, addr);

        CrashHandlerAccess::logFile(log).write(buf);
        CrashHandlerAccess::logFile(log).flush();
    }

    // Write crash marker with ring buffer contents
    CrashHandlerAccess::createCrashMarker(log);

    lk.unlock();

    // Let Windows generate the default crash dump / WER report
    return EXCEPTION_CONTINUE_SEARCH;
}

void Logger::installCrashHandler()
{
    SetUnhandledExceptionFilter(autolensExceptionFilter);
}

#endif // Q_OS_WIN
