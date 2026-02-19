#pragma once
/**
 * @file VectorCANDriver.h
 * @brief Vector XL Library CAN driver for AutoLens.
 *
 * Wraps the Vector XL Driver Library (vxlapi64.dll / vxlapi.dll) using
 * runtime DLL loading so the application starts on any machine — even one
 * without Vector software installed.  Only when a channel is actually
 * opened does the DLL need to be present.
 *
 * Supported hardware (any Vector VN / CANboard device):
 *   VN1610, VN1630, VN1640, VN1670, VN5610, VN7600, VN8900, …
 *
 * Features:
 *   • Runtime DLL loading via QLibrary (no link-time dependency)
 *   • Channel enumeration (all CAN-capable channels on all devices)
 *   • Classic CAN (HS) and CAN FD
 *   • Async receive thread → emits messageReceived() signal
 *   • Mutex-protected transmit so the UI can call transmit() safely
 *
 * Usage (see also AppController):
 * @code
 *   auto* drv = new VectorCANDriver;
 *   if (drv->initialize()) {
 *       auto channels = drv->detectChannels();
 *       CANBusConfig cfg;  cfg.bitrate = 500000;
 *       drv->openChannel(channels[0], cfg);
 *       connect(drv, &ICANDriver::messageReceived, this, &MyClass::onFrame);
 *       drv->startAsyncReceive();
 *   }
 * @endcode
 */

#include "CANInterface.h"

#include <QLibrary>
#include <QMutex>
#include <QThread>
#include <atomic>

// vxlapi.h uses HANDLE from the Windows API
#include <windows.h>

// DYNAMIC_XLDRIVER_DLL  → vxlapi.h exposes function-pointer typedefs instead
//                         of extern function declarations, allowing us to
//                         resolve them manually via QLibrary::resolve().
// DO_NOT_DEFINE_EXTERN_DECLARATION → suppress the extern "C" block entirely.
#define DYNAMIC_XLDRIVER_DLL
#define DO_NOT_DEFINE_EXTERN_DECLARATION
#include "vxlapi.h"

namespace CANManager {

class VectorCANDriver : public ICANDriver
{
    Q_OBJECT

public:
    explicit VectorCANDriver(QObject* parent = nullptr);
    ~VectorCANDriver() override;

    // --- ICANDriver interface ---
    bool    initialize()  override;
    void    shutdown()    override;
    bool    isAvailable() const override;
    QString driverName()  const override { return QStringLiteral("Vector XL"); }

    QList<CANChannelInfo> detectChannels() override;

    CANResult openChannel(const CANChannelInfo& channel,
                          const CANBusConfig& config) override;
    void      closeChannel() override;
    bool      isOpen() const override;

    CANResult transmit(const CANMessage& msg) override;
    CANResult receive(CANMessage& msg, int timeoutMs = 1000) override;
    CANResult flushReceiveQueue() override;
    QString   lastError() const override;

    // --- Vector-specific extras ---

    /** Start a background thread that calls receive() in a loop and emits
     *  messageReceived() for every incoming frame.  Call after openChannel(). */
    void startAsyncReceive();

    /** Stop the async receive thread.  Called automatically by closeChannel(). */
    void stopAsyncReceive();

    bool isAsyncReceiving() const { return m_asyncRunning.load(); }

    /** XL Library DLL version string, e.g. "20.30.14". */
    QString xlDllVersion() const;

    /** Human-readable name for a Vector hardware type code. */
    static QString hwTypeName(int hwType);

    /** Application name shown in Vector Hardware Config tool. */
    void    setAppName(const QString& name) { m_appName = name; }
    QString appName() const                 { return m_appName; }

private:
    // DLL lifecycle
    bool loadLibrary();
    void unloadLibrary();
    bool resolveFunctions();

    // Transmit helpers (split by classic / FD)
    CANResult transmitClassic(const CANMessage& msg);
    CANResult transmitFD(const CANMessage& msg);

    // Receive helpers
    CANResult receiveClassic(CANMessage& msg, int timeoutMs);
    CANResult receiveFD(CANMessage& msg, int timeoutMs);

    // Error helpers
    QString   xlStatusToString(XLstatus status) const;
    void      setError(const QString& msg);
    CANResult makeError(const QString& context, XLstatus status);

    // State
    QLibrary       m_xlLib;
    bool           m_driverOpen      = false;
    XLportHandle   m_portHandle      = XL_INVALID_PORTHANDLE;
    XLaccess       m_channelMask     = 0;
    XLaccess       m_permissionMask  = 0;
    XLhandle       m_notifyEvent     = nullptr;
    bool           m_isFD            = false;
    QString        m_lastError;
    QString        m_appName         = QStringLiteral("AutoLens");
    mutable QMutex m_mutex;
    mutable int    m_availableCached = -1;  // -1 = unchecked

    // Async receive thread
    QThread*          m_rxThread    = nullptr;
    std::atomic<bool> m_asyncRunning{false};

    // ---------------------------------------------------------------------------
    //  XL Library function pointers
    //  Each resolved by QLibrary::resolve() from vxlapi64.dll at runtime.
    //  The typedefs come from vxlapi.h (via DYNAMIC_XLDRIVER_DLL define above).
    // ---------------------------------------------------------------------------
    XLOPENDRIVER                m_xlOpenDriver              = nullptr;
    XLCLOSEDRIVER               m_xlCloseDriver             = nullptr;
    XLGETDRIVERCONFIG           m_xlGetDriverConfig         = nullptr;
    XLGETAPPLCONFIG             m_xlGetApplConfig           = nullptr;
    XLSETAPPLCONFIG             m_xlSetApplConfig           = nullptr;
    XLGETCHANNELINDEX           m_xlGetChannelIndex         = nullptr;
    XLGETCHANNELMASK            m_xlGetChannelMask          = nullptr;
    XLOPENPORT                  m_xlOpenPort                = nullptr;
    XLCLOSEPORT                 m_xlClosePort               = nullptr;
    XLACTIVATECHANNEL           m_xlActivateChannel         = nullptr;
    XLDEACTIVATECHANNEL         m_xlDeactivateChannel       = nullptr;
    XLCANSETCHANNELBITRATE      m_xlCanSetChannelBitrate    = nullptr;
    XLCANSETCHANNELOUTPUT       m_xlCanSetChannelOutput     = nullptr;
    XLCANSETCHANNELMODE         m_xlCanSetChannelMode       = nullptr;
    XLCANFDSETCONFIGURATION     m_xlCanFdSetConfiguration   = nullptr;
    XLCANTRANSMIT               m_xlCanTransmit             = nullptr;
    XLCANTRANSMITEX             m_xlCanTransmitEx           = nullptr;
    XLRECEIVE                   m_xlReceive                 = nullptr;
    XLCANRECEIVE                m_xlCanReceive              = nullptr;
    XLSETNOTIFICATION           m_xlSetNotification         = nullptr;
    XLFLUSHRECEIVEQUEUE         m_xlFlushReceiveQueue       = nullptr;
    XLGETERRORSTRING            m_xlGetErrorString          = nullptr;
    XLGETEVENTSTRING            m_xlGetEventString          = nullptr;
};

} // namespace CANManager
