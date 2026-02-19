/**
 * @file VectorCANDriver.cpp
 * @brief Vector XL Library CAN driver — full implementation.
 *
 * Runtime-loads vxlapi64.dll (or vxlapi.dll for 32-bit) and provides
 * complete CAN HS + FD communication through Vector VN hardware.
 *
 * Learning notes
 * ──────────────
 *  • QLibrary::resolve()  — looks up a symbol by name from a loaded DLL.
 *    We cast the result to the matching function-pointer typedef from vxlapi.h.
 *  • XLportHandle         — opaque handle returned by xlOpenPort(); required
 *    for all subsequent API calls to identify our "session".
 *  • XLaccess (channelMask) — bitmask where each bit represents one physical
 *    channel. OR-ing masks opens multiple channels in a single port.
 *  • WaitForSingleObject() — Windows API that blocks until a Win32 event
 *    (m_notifyEvent) is signalled by the driver, indicating data is ready.
 *    More efficient than busy-polling.
 */

#include "VectorCANDriver.h"

#include <QDebug>
#include <QCoreApplication>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#endif

namespace CANManager {

// ============================================================================
//  Hardware-type name table
// ============================================================================

QString VectorCANDriver::hwTypeName(int hwType)
{
    switch (hwType) {
    case XL_HWTYPE_VIRTUAL:     return QStringLiteral("Virtual");
    case XL_HWTYPE_CANCARDX:    return QStringLiteral("CANcardX");
    case XL_HWTYPE_CANCASEXL:   return QStringLiteral("CANcaseXL");
    case XL_HWTYPE_CANBOARDXL:  return QStringLiteral("CANboardXL");
    case XL_HWTYPE_VN1610:      return QStringLiteral("VN1610");
    case XL_HWTYPE_VN1630:      return QStringLiteral("VN1630");
    case XL_HWTYPE_VN1640:      return QStringLiteral("VN1640");
    case XL_HWTYPE_VN8900:      return QStringLiteral("VN8900");
    case XL_HWTYPE_VN7600:      return QStringLiteral("VN7600");
    case XL_HWTYPE_VN5610:      return QStringLiteral("VN5610");
    case XL_HWTYPE_VN5620:      return QStringLiteral("VN5620");
    case XL_HWTYPE_VN7610:      return QStringLiteral("VN7610");
    case XL_HWTYPE_VN7572:      return QStringLiteral("VN7572");
    case XL_HWTYPE_VN1530:      return QStringLiteral("VN1530");
    case XL_HWTYPE_VN1531:      return QStringLiteral("VN1531");
    case XL_HWTYPE_VN1670:      return QStringLiteral("VN1670");
    case XL_HWTYPE_VN5610A:     return QStringLiteral("VN5610A");
    case XL_HWTYPE_VN7640:      return QStringLiteral("VN7640");
    case XL_HWTYPE_VN4610:      return QStringLiteral("VN4610");
    default:
        return QString("HW_0x%1").arg(hwType, 2, 16, QChar('0'));
    }
}

// ============================================================================
//  Constructor / Destructor
// ============================================================================

VectorCANDriver::VectorCANDriver(QObject* parent) : ICANDriver(parent) {}

VectorCANDriver::~VectorCANDriver()
{
    shutdown();
}

// ============================================================================
//  DLL Loading
// ============================================================================

bool VectorCANDriver::loadLibrary()
{
    if (m_xlLib.isLoaded())
        return true;

    // Try 64-bit DLL first (matches a 64-bit build), then 32-bit fallback.
    // QLibrary searches: app directory, system PATH, Windows System32.
    static const QStringList candidates = {
        QStringLiteral("vxlapi64"),
        QStringLiteral("vxlapi")
    };

    for (const auto& name : candidates) {
        m_xlLib.setFileName(name);
        if (m_xlLib.load()) {
            qDebug() << "[VectorCAN] Loaded DLL:" << m_xlLib.fileName();
            return true;
        }
    }

    setError(QString("vxlapi64.dll not found — is the Vector driver installed? (%1)")
                 .arg(m_xlLib.errorString()));
    return false;
}

void VectorCANDriver::unloadLibrary()
{
    if (m_xlLib.isLoaded()) {
        m_xlLib.unload();
        qDebug() << "[VectorCAN] Library unloaded";
    }
    // Null out all function pointers so we don't accidentally call freed code
    m_xlOpenDriver = nullptr;  m_xlCloseDriver = nullptr;
    m_xlGetDriverConfig = nullptr; m_xlGetApplConfig = nullptr;
    m_xlSetApplConfig = nullptr;   m_xlGetChannelIndex = nullptr;
    m_xlGetChannelMask = nullptr;  m_xlOpenPort = nullptr;
    m_xlClosePort = nullptr;       m_xlActivateChannel = nullptr;
    m_xlDeactivateChannel = nullptr; m_xlCanSetChannelBitrate = nullptr;
    m_xlCanSetChannelOutput = nullptr; m_xlCanSetChannelMode = nullptr;
    m_xlCanFdSetConfiguration = nullptr; m_xlCanTransmit = nullptr;
    m_xlCanTransmitEx = nullptr;   m_xlReceive = nullptr;
    m_xlCanReceive = nullptr;      m_xlSetNotification = nullptr;
    m_xlFlushReceiveQueue = nullptr; m_xlGetErrorString = nullptr;
    m_xlGetEventString = nullptr;
}

bool VectorCANDriver::resolveFunctions()
{
    // RESOLVE_XL: mandatory function — return false if missing.
    // RESOLVE_XL_OPTIONAL: nice-to-have — warn but continue if absent.
#define RESOLVE_XL(fn, T) \
    m_##fn = reinterpret_cast<T>(m_xlLib.resolve(#fn)); \
    if (!m_##fn) { qWarning() << "[VectorCAN] Missing:" << #fn; return false; }

#define RESOLVE_XL_OPTIONAL(fn, T) \
    m_##fn = reinterpret_cast<T>(m_xlLib.resolve(#fn)); \
    if (!m_##fn) { qDebug() << "[VectorCAN] Optional not found:" << #fn; }

    RESOLVE_XL(xlOpenDriver,           XLOPENDRIVER)
    RESOLVE_XL(xlCloseDriver,          XLCLOSEDRIVER)
    RESOLVE_XL(xlGetDriverConfig,      XLGETDRIVERCONFIG)
    RESOLVE_XL(xlOpenPort,             XLOPENPORT)
    RESOLVE_XL(xlClosePort,            XLCLOSEPORT)
    RESOLVE_XL(xlActivateChannel,      XLACTIVATECHANNEL)
    RESOLVE_XL(xlDeactivateChannel,    XLDEACTIVATECHANNEL)
    RESOLVE_XL(xlCanSetChannelBitrate, XLCANSETCHANNELBITRATE)
    RESOLVE_XL(xlCanSetChannelOutput,  XLCANSETCHANNELOUTPUT)
    RESOLVE_XL(xlSetNotification,      XLSETNOTIFICATION)
    RESOLVE_XL(xlFlushReceiveQueue,    XLFLUSHRECEIVEQUEUE)
    RESOLVE_XL(xlCanTransmit,          XLCANTRANSMIT)
    RESOLVE_XL(xlReceive,              XLRECEIVE)

    RESOLVE_XL_OPTIONAL(xlGetApplConfig,        XLGETAPPLCONFIG)
    RESOLVE_XL_OPTIONAL(xlSetApplConfig,        XLSETAPPLCONFIG)
    RESOLVE_XL_OPTIONAL(xlGetChannelIndex,      XLGETCHANNELINDEX)
    RESOLVE_XL_OPTIONAL(xlGetChannelMask,       XLGETCHANNELMASK)
    RESOLVE_XL_OPTIONAL(xlCanSetChannelMode,    XLCANSETCHANNELMODE)
    RESOLVE_XL_OPTIONAL(xlCanFdSetConfiguration,XLCANFDSETCONFIGURATION)
    RESOLVE_XL_OPTIONAL(xlCanTransmitEx,        XLCANTRANSMITEX)
    RESOLVE_XL_OPTIONAL(xlCanReceive,           XLCANRECEIVE)
    RESOLVE_XL_OPTIONAL(xlGetErrorString,       XLGETERRORSTRING)
    RESOLVE_XL_OPTIONAL(xlGetEventString,       XLGETEVENTSTRING)

#undef RESOLVE_XL
#undef RESOLVE_XL_OPTIONAL
    return true;
}

// ============================================================================
//  Driver Lifecycle
// ============================================================================

bool VectorCANDriver::initialize()
{
    QMutexLocker lock(&m_mutex);
    if (m_driverOpen) return true;

    if (!loadLibrary())    return false;
    if (!resolveFunctions()) { unloadLibrary(); return false; }

    XLstatus s = m_xlOpenDriver();
    if (s != XL_SUCCESS) {
        setError(QString("xlOpenDriver failed: %1").arg(xlStatusToString(s)));
        unloadLibrary();
        return false;
    }

    m_driverOpen = true;
    qDebug() << "[VectorCAN] Initialized. DLL version:" << xlDllVersion();
    return true;
}

void VectorCANDriver::shutdown()
{
    stopAsyncReceive();

    QMutexLocker lock(&m_mutex);
    if (m_portHandle != XL_INVALID_PORTHANDLE) {
        lock.unlock();
        closeChannel();
        lock.relock();
    }
    if (m_driverOpen && m_xlCloseDriver) {
        m_xlCloseDriver();
        m_driverOpen = false;
    }
    unloadLibrary();
}

bool VectorCANDriver::isAvailable() const
{
    if (m_availableCached >= 0)
        return m_availableCached == 1;

    QLibrary test;
    for (const auto& name : {QStringLiteral("vxlapi64"), QStringLiteral("vxlapi")}) {
        test.setFileName(name);
        if (test.load()) { test.unload(); m_availableCached = 1; return true; }
    }
    m_availableCached = 0;
    return false;
}

QString VectorCANDriver::xlDllVersion() const
{
    QMutexLocker lock(&m_mutex);
    if (!m_driverOpen || !m_xlGetDriverConfig) return {};
    XLdriverConfig cfg; memset(&cfg, 0, sizeof(cfg));
    if (m_xlGetDriverConfig(&cfg) != XL_SUCCESS) return {};
    unsigned v = cfg.dllVersion;
    return QString("%1.%2.%3").arg((v>>24)&0xFF).arg((v>>16)&0xFF).arg(v&0xFFFF);
}

// ============================================================================
//  Hardware Detection
// ============================================================================

QList<CANChannelInfo> VectorCANDriver::detectChannels()
{
    QMutexLocker lock(&m_mutex);
    QList<CANChannelInfo> result;

    if (!m_driverOpen) { setError("Driver not initialized"); return result; }

    XLdriverConfig cfg; memset(&cfg, 0, sizeof(cfg));
    XLstatus s = m_xlGetDriverConfig(&cfg);
    if (s != XL_SUCCESS) {
        setError(QString("xlGetDriverConfig: %1").arg(xlStatusToString(s)));
        return result;
    }

    qDebug() << "[VectorCAN]" << cfg.channelCount << "total channels";

    for (unsigned i = 0; i < cfg.channelCount && i < XL_CONFIG_MAX_CHANNELS; ++i) {
        const auto& ch = cfg.channel[i];
        if (!(ch.channelBusCapabilities & XL_BUS_COMPATIBLE_CAN))
            continue;   // skip non-CAN channels (LIN, Ethernet, …)

        CANChannelInfo info;
        info.name            = QString::fromLatin1(ch.name);
        info.hwTypeName      = hwTypeName(ch.hwType);
        info.hwType          = ch.hwType;
        info.hwIndex         = ch.hwIndex;
        info.hwChannel       = ch.hwChannel;
        info.channelIndex    = ch.channelIndex;
        info.channelMask     = ch.channelMask;
        info.serialNumber    = ch.serialNumber;
        info.isOnBus         = ch.isOnBus != 0;
        info.transceiverName = QString::fromLatin1(ch.transceiverName);
        info.supportsFD      = (ch.channelCapabilities & XL_CHANNEL_FLAG_CANFD_ISO_SUPPORT) ||
                               (ch.channelCapabilities & XL_CHANNEL_FLAG_CANFD_BOSCH_SUPPORT);

        qDebug() << "[VectorCAN]  Ch" << i << info.name
                 << "S/N:" << info.serialNumber << "FD:" << info.supportsFD;
        result.append(info);
    }
    return result;
}

// ============================================================================
//  Channel Open / Close
// ============================================================================

CANResult VectorCANDriver::openChannel(const CANChannelInfo& channel,
                                        const CANBusConfig& config)
{
    QMutexLocker lock(&m_mutex);
    if (!m_driverOpen)
        return CANResult::Failure("Driver not initialized");
    if (m_portHandle != XL_INVALID_PORTHANDLE)
        return CANResult::Failure("Channel already open — close first");

    m_isFD           = config.fdEnabled && channel.supportsFD;
    m_channelMask    = channel.channelMask;
    m_permissionMask = channel.channelMask;  // request init (TX) access

    unsigned ifVer = m_isFD ? XL_INTERFACE_VERSION_V4 : XL_INTERFACE_VERSION;

    QByteArray appNameBytes = m_appName.toUtf8();
    XLstatus s = m_xlOpenPort(&m_portHandle, appNameBytes.data(),
                               m_channelMask, &m_permissionMask,
                               256, ifVer, XL_BUS_TYPE_CAN);
    if (s != XL_SUCCESS) {
        m_portHandle = XL_INVALID_PORTHANDLE;
        return makeError("xlOpenPort", s);
    }

    // Configure bitrate if we have init access
    if (m_permissionMask & m_channelMask) {
        if (m_isFD && m_xlCanFdSetConfiguration) {
            XLcanFdConf fd; memset(&fd, 0, sizeof(fd));
            fd.arbitrationBitRate = static_cast<unsigned>(config.bitrate);
            fd.dataBitRate        = static_cast<unsigned>(config.fdDataBitrate);
            s = m_xlCanFdSetConfiguration(m_portHandle, m_channelMask, &fd);
            if (s != XL_SUCCESS) { m_isFD = false; /* fall through to classic */ }
        }
        if (!m_isFD) {
            m_xlCanSetChannelBitrate(m_portHandle, m_channelMask,
                                     static_cast<unsigned long>(config.bitrate));
        }
        int outMode = config.listenOnly ? XL_OUTPUT_MODE_SILENT : XL_OUTPUT_MODE_NORMAL;
        m_xlCanSetChannelOutput(m_portHandle, m_channelMask, outMode);
    } else {
        qWarning() << "[VectorCAN] No init access — listen-only (another app owns it)";
    }

    // Win32 event for efficient blocking receive (avoids busy-wait)
    m_xlSetNotification(m_portHandle, &m_notifyEvent, 1);

    s = m_xlActivateChannel(m_portHandle, m_channelMask,
                             XL_BUS_TYPE_CAN, XL_ACTIVATE_RESET_CLOCK);
    if (s != XL_SUCCESS) {
        m_xlClosePort(m_portHandle);
        m_portHandle  = XL_INVALID_PORTHANDLE;
        m_notifyEvent = nullptr;
        return makeError("xlActivateChannel", s);
    }

    m_xlFlushReceiveQueue(m_portHandle);

    qDebug() << "[VectorCAN] Channel open. FD:" << m_isFD << "Bitrate:" << config.bitrate;
    lock.unlock();
    emit channelOpened();
    return CANResult::Success();
}

void VectorCANDriver::closeChannel()
{
    stopAsyncReceive();
    QMutexLocker lock(&m_mutex);
    if (m_portHandle == XL_INVALID_PORTHANDLE) return;

    if (m_xlDeactivateChannel) m_xlDeactivateChannel(m_portHandle, m_channelMask);
    if (m_xlClosePort)         m_xlClosePort(m_portHandle);

    m_portHandle = XL_INVALID_PORTHANDLE;
    m_channelMask = m_permissionMask = 0;
    m_notifyEvent = nullptr;
    m_isFD = false;
    lock.unlock();
    emit channelClosed();
}

bool VectorCANDriver::isOpen() const
{
    QMutexLocker lock(&m_mutex);
    return m_portHandle != XL_INVALID_PORTHANDLE;
}

// ============================================================================
//  Transmit
// ============================================================================

CANResult VectorCANDriver::transmit(const CANMessage& msg)
{
    QMutexLocker lock(&m_mutex);
    if (m_portHandle == XL_INVALID_PORTHANDLE)
        return CANResult::Failure("Channel not open");
    if (!(m_permissionMask & m_channelMask))
        return CANResult::Failure("No TX access (listen-only)");
    return (msg.isFD && m_isFD) ? transmitFD(msg) : transmitClassic(msg);
}

CANResult VectorCANDriver::transmitClassic(const CANMessage& msg)
{
    XLevent ev; memset(&ev, 0, sizeof(ev));
    ev.tag = XL_TRANSMIT_MSG;
    ev.tagData.msg.id  = msg.id;
    ev.tagData.msg.dlc = qMin((unsigned short)msg.dlc, (unsigned short)8);
    if (msg.isExtended) ev.tagData.msg.id |= XL_CAN_EXT_MSG_ID;
    if (msg.isRemote)   ev.tagData.msg.flags |= XL_CAN_MSG_FLAG_REMOTE_FRAME;
    memcpy(ev.tagData.msg.data, msg.data, ev.tagData.msg.dlc);

    unsigned cnt = 1;
    XLstatus s = m_xlCanTransmit(m_portHandle, m_channelMask, &cnt, &ev);
    return (s == XL_SUCCESS) ? CANResult::Success() : makeError("xlCanTransmit", s);
}

CANResult VectorCANDriver::transmitFD(const CANMessage& msg)
{
    if (!m_xlCanTransmitEx)
        return CANResult::Failure("FD transmit not available");

    XLcanTxEvent tx; memset(&tx, 0, sizeof(tx));
    tx.tag = XL_CAN_EV_TAG_TX_MSG;
    tx.tagData.canMsg.canId  = msg.id | (msg.isExtended ? XL_CAN_EXT_MSG_ID : 0);
    tx.tagData.canMsg.msgFlags = XL_CAN_TXMSG_FLAG_EDL;
    if (msg.isBRS)   tx.tagData.canMsg.msgFlags |= XL_CAN_TXMSG_FLAG_BRS;
    if (msg.isRemote)tx.tagData.canMsg.msgFlags |= XL_CAN_TXMSG_FLAG_RTR;
    tx.tagData.canMsg.dlc = msg.dlc;
    memcpy(tx.tagData.canMsg.data, msg.data, dlcToLength(msg.dlc));

    unsigned sent = 0;
    XLstatus s = m_xlCanTransmitEx(m_portHandle, m_channelMask, 1, &sent, &tx);
    if (s != XL_SUCCESS)  return makeError("xlCanTransmitEx", s);
    if (sent == 0)        return CANResult::Failure("TX queue full");
    return CANResult::Success();
}

// ============================================================================
//  Receive
// ============================================================================

CANResult VectorCANDriver::receive(CANMessage& msg, int timeoutMs)
{
    QMutexLocker lock(&m_mutex);
    if (m_portHandle == XL_INVALID_PORTHANDLE)
        return CANResult::Failure("Channel not open");
    return (m_isFD && m_xlCanReceive) ? receiveFD(msg, timeoutMs)
                                      : receiveClassic(msg, timeoutMs);
}

CANResult VectorCANDriver::receiveClassic(CANMessage& msg, int timeoutMs)
{
    if (m_notifyEvent) {
        DWORD ms = (timeoutMs < 0) ? INFINITE : static_cast<DWORD>(timeoutMs);
        DWORD r  = WaitForSingleObject(m_notifyEvent, ms);
        if (r == WAIT_TIMEOUT) return CANResult::Failure("Timeout");
        if (r != WAIT_OBJECT_0) return CANResult::Failure("Wait error");
    }

    XLevent ev; unsigned cnt = 1;
    XLstatus s = m_xlReceive(m_portHandle, &cnt, &ev);
    if (s == XL_ERR_QUEUE_IS_EMPTY) return CANResult::Failure("Empty");
    if (s != XL_SUCCESS)            return makeError("xlReceive", s);
    if (ev.tag != XL_RECEIVE_MSG)   return CANResult::Failure("Not a CAN msg event");

    msg.id         = ev.tagData.msg.id & ~XL_CAN_EXT_MSG_ID;
    msg.isExtended = (ev.tagData.msg.id & XL_CAN_EXT_MSG_ID) != 0;
    msg.dlc        = static_cast<uint8_t>(qMin((unsigned short)ev.tagData.msg.dlc, (unsigned short)8));
    msg.isFD       = false;
    msg.isRemote   = (ev.tagData.msg.flags & XL_CAN_MSG_FLAG_REMOTE_FRAME) != 0;
    msg.isError    = (ev.tagData.msg.flags & XL_CAN_MSG_FLAG_ERROR_FRAME)  != 0;
    msg.isTxConfirm= (ev.tagData.msg.flags & XL_CAN_MSG_FLAG_TX_COMPLETED) != 0;
    msg.timestamp  = ev.timeStamp;
    memcpy(msg.data, ev.tagData.msg.data, msg.dlc);
    return CANResult::Success();
}

CANResult VectorCANDriver::receiveFD(CANMessage& msg, int timeoutMs)
{
    if (m_notifyEvent) {
        DWORD ms = (timeoutMs < 0) ? INFINITE : static_cast<DWORD>(timeoutMs);
        DWORD r  = WaitForSingleObject(m_notifyEvent, ms);
        if (r == WAIT_TIMEOUT)  return CANResult::Failure("Timeout");
        if (r != WAIT_OBJECT_0) return CANResult::Failure("Wait error");
    }

    XLcanRxEvent rx; memset(&rx, 0, sizeof(rx));
    XLstatus s = m_xlCanReceive(m_portHandle, &rx);
    if (s == XL_ERR_QUEUE_IS_EMPTY) return CANResult::Failure("Empty");
    if (s != XL_SUCCESS)            return makeError("xlCanReceive", s);

    if (rx.tag != XL_CAN_EV_TAG_RX_OK && rx.tag != XL_CAN_EV_TAG_TX_OK)
        return CANResult::Failure("Non-data FD event");

    const auto& m_ = rx.tagData.canRxOkMsg;
    msg.id         = m_.canId & ~XL_CAN_EXT_MSG_ID;
    msg.isExtended = (m_.canId & XL_CAN_EXT_MSG_ID) != 0;
    msg.dlc        = m_.dlc;
    msg.isFD       = (m_.msgFlags & XL_CAN_RXMSG_FLAG_EDL) != 0;
    msg.isBRS      = (m_.msgFlags & XL_CAN_RXMSG_FLAG_BRS) != 0;
    msg.isRemote   = (m_.msgFlags & XL_CAN_RXMSG_FLAG_RTR) != 0;
    msg.isError    = (m_.msgFlags & XL_CAN_RXMSG_FLAG_EF)  != 0;
    msg.isTxConfirm= (rx.tag == XL_CAN_EV_TAG_TX_OK);
    msg.timestamp  = rx.timeStampSync;
    memcpy(msg.data, m_.data, msg.isFD ? dlcToLength(msg.dlc) : qMin((int)msg.dlc,8));
    return CANResult::Success();
}

// ============================================================================
//  Async Receive Thread
// ============================================================================

void VectorCANDriver::startAsyncReceive()
{
    if (m_asyncRunning.load()) return;
    if (!isOpen()) { qWarning() << "[VectorCAN] startAsyncReceive: not open"; return; }

    m_asyncRunning = true;
    // QThread::create() wraps a lambda in a QThread — no subclassing needed.
    m_rxThread = QThread::create([this]() {
        while (m_asyncRunning.load()) {
            CANMessage msg;
            auto res = receive(msg, 100);   // 100 ms timeout → re-check flag
            if (res.success && !msg.isError && !msg.isTxConfirm)
                emit messageReceived(msg);  // ← crosses to UI thread (queued)
        }
    });
    m_rxThread->setObjectName(QStringLiteral("AutoLens_CAN_RX"));
    m_rxThread->start(QThread::HighPriority);
}

void VectorCANDriver::stopAsyncReceive()
{
    if (!m_asyncRunning.load()) return;
    m_asyncRunning = false;
    if (m_rxThread) { m_rxThread->wait(3000); delete m_rxThread; m_rxThread = nullptr; }
}

// ============================================================================
//  Misc / Helpers
// ============================================================================

CANResult VectorCANDriver::flushReceiveQueue()
{
    QMutexLocker lock(&m_mutex);
    if (m_portHandle == XL_INVALID_PORTHANDLE)
        return CANResult::Failure("Not open");
    XLstatus s = m_xlFlushReceiveQueue(m_portHandle);
    return (s == XL_SUCCESS) ? CANResult::Success() : makeError("xlFlushReceiveQueue", s);
}

QString VectorCANDriver::lastError() const
{
    QMutexLocker lock(&m_mutex);
    return m_lastError;
}

QString VectorCANDriver::xlStatusToString(XLstatus s) const
{
    if (m_xlGetErrorString) {
        const char* str = m_xlGetErrorString(s);
        if (str) return QString::fromLatin1(str);
    }
    switch (s) {
    case XL_SUCCESS:                return QStringLiteral("XL_SUCCESS");
    case XL_ERR_QUEUE_IS_EMPTY:     return QStringLiteral("QUEUE_EMPTY");
    case XL_ERR_QUEUE_IS_FULL:      return QStringLiteral("QUEUE_FULL");
    case XL_ERR_TX_NOT_POSSIBLE:    return QStringLiteral("TX_NOT_POSSIBLE");
    case XL_ERR_NO_LICENSE:         return QStringLiteral("NO_LICENSE");
    case XL_ERR_WRONG_PARAMETER:    return QStringLiteral("WRONG_PARAMETER");
    case XL_ERR_CANNOT_OPEN_DRIVER: return QStringLiteral("CANNOT_OPEN_DRIVER");
    case XL_ERR_HW_NOT_PRESENT:     return QStringLiteral("HW_NOT_PRESENT");
    case XL_ERR_DLL_NOT_FOUND:      return QStringLiteral("DLL_NOT_FOUND");
    default: return QString("XL_ERR_%1").arg(static_cast<int>(s));
    }
}

void VectorCANDriver::setError(const QString& msg)
{
    m_lastError = msg;
    qWarning() << "[VectorCAN]" << msg;
}

CANResult VectorCANDriver::makeError(const QString& ctx, XLstatus s)
{
    QString msg = QString("%1: %2").arg(ctx, xlStatusToString(s));
    setError(msg);
    emit errorOccurred(msg);
    return CANResult::Failure(msg);
}

} // namespace CANManager
