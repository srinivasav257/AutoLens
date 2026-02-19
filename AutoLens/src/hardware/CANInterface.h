#pragma once
/**
 * @file CANInterface.h
 * @brief Abstract CAN bus interface and common data types.
 *
 * Provides a driver-agnostic abstraction for CAN bus communication.
 * Concrete drivers implement this interface:
 *   VectorCANDriver — talks to Vector VN hardware via vxlapi64.dll
 *   DemoCANDriver   — generates synthetic traffic (no hardware needed)
 *
 * Key types:
 *   CANMessage      — one CAN/CAN-FD frame (id, data, timestamp, flags)
 *   CANChannelInfo  — describes one detected hardware channel
 *   CANBusConfig    — bitrate / FD settings for opening a channel
 *   CANResult       — success/failure return value
 *   ICANDriver      — abstract QObject base class; signals + virtual API
 *
 * Threading contract
 * ──────────────────
 *   ICANDriver objects are created on the UI thread.
 *   Concrete drivers MAY spin up internal threads for receive polling.
 *   The messageReceived() signal is emitted from the internal thread;
 *   Qt's queued connection (automatic for cross-thread) delivers it
 *   safely to the UI thread. See AppController::onFrameReceived().
 */

#include <QObject>
#include <QString>
#include <QList>
#include <cstdint>

namespace CANManager {

// ============================================================================
//  CAN DLC ↔ Data-Length helpers (supports CAN FD extended DLCs)
// ============================================================================

/**
 * @brief Convert a DLC code to the actual byte count.
 *
 * Classic CAN: DLC 0–8 maps 1:1.
 * CAN FD:  DLC 9=12, 10=16, 11=20, 12=24, 13=32, 14=48, 15=64 bytes.
 */
inline int dlcToLength(uint8_t dlc)
{
    static constexpr int table[] = {0,1,2,3,4,5,6,7,8,12,16,20,24,32,48,64};
    return (dlc <= 15) ? table[dlc] : 64;
}

/**
 * @brief Return the smallest DLC whose byte count is ≥ byteCount.
 */
inline uint8_t lengthToDlc(int byteCount)
{
    if (byteCount <= 8)  return static_cast<uint8_t>(byteCount);
    if (byteCount <= 12) return 9;
    if (byteCount <= 16) return 10;
    if (byteCount <= 20) return 11;
    if (byteCount <= 24) return 12;
    if (byteCount <= 32) return 13;
    if (byteCount <= 48) return 14;
    return 15;
}

// ============================================================================
//  CANMessage — one CAN / CAN-FD frame
// ============================================================================

/**
 * @brief A single CAN or CAN-FD frame.
 *
 * Passed by value through queued signals (must be Q_DECLARE_METATYPE'd).
 * See main.cpp for the registration call.
 */
struct CANMessage
{
    uint32_t id          = 0;       ///< Arbitration ID (11-bit or 29-bit)
    uint8_t  data[64]    = {};      ///< Payload (up to 8 classic / 64 FD)
    uint8_t  dlc         = 0;       ///< Data length code
    bool     isExtended  = false;   ///< 29-bit extended-ID frame
    bool     isFD        = false;   ///< CAN FD frame (EDL set)
    bool     isBRS       = false;   ///< Bit-rate switch (FD only)
    bool     isRemote    = false;   ///< Remote Transmission Request
    bool     isError     = false;   ///< Error frame
    bool     isTxConfirm = false;   ///< TX echo (our own transmitted frame)
    uint8_t  channel     = 1;       ///< Hardware channel number (1-based)
    uint64_t timestamp   = 0;       ///< Hardware timestamp in nanoseconds

    /** Actual payload byte count — respects FD DLC table. */
    int dataLength() const
    {
        return isFD ? dlcToLength(dlc) : qMin(static_cast<int>(dlc), 8);
    }
};

// ============================================================================
//  CANChannelInfo — one detected hardware channel
// ============================================================================

/**
 * @brief Describes a hardware CAN channel returned by detectChannels().
 */
struct CANChannelInfo
{
    QString  name;                  ///< "Channel 1 (VN1630)"
    QString  hwTypeName;            ///< "VN1630"
    int      hwType       = 0;
    int      hwIndex      = 0;
    int      hwChannel    = 0;
    int      channelIndex = 0;
    uint64_t channelMask  = 0;
    uint32_t serialNumber = 0;
    bool     supportsFD   = false;
    bool     isOnBus      = false;
    QString  transceiverName;

    /** Display string for a combo-box entry, e.g. "Channel 1 (VN1630) [S/N: 12345]" */
    QString displayString() const
    {
        return serialNumber > 0
            ? QString("%1  [S/N: %2]").arg(name).arg(serialNumber)
            : name;
    }
};

// ============================================================================
//  CANBusConfig — how to open a channel
// ============================================================================

struct CANBusConfig
{
    int  bitrate       = 500000;    ///< Nominal bitrate in bps (default 500 kbit/s)
    bool fdEnabled     = false;     ///< Enable CAN FD mode
    int  fdDataBitrate = 2000000;   ///< FD data-phase bitrate in bps
    bool listenOnly    = false;     ///< Silent / listen-only (no ACKs transmitted)
};

// ============================================================================
//  CANResult — operation outcome
// ============================================================================

struct CANResult
{
    bool    success = false;
    QString errorMessage;

    static CANResult Success() { return {true, {}}; }
    static CANResult Failure(const QString& msg) { return {false, msg}; }
};

// ============================================================================
//  ICANDriver — abstract driver interface
// ============================================================================

/**
 * @brief Abstract base class for all CAN drivers.
 *
 * Subclass this to add a new hardware backend.
 * Currently implemented:
 *   VectorCANDriver — Vector VN series via vxlapi64.dll
 *   DemoCANDriver   — fake traffic, always available
 *
 * Lifecycle
 * ─────────
 *   1. Construct on UI thread.
 *   2. Call initialize() → load library / verify HW.
 *   3. Call detectChannels() → list available channels.
 *   4. Call openChannel(info, config) → go on-bus.
 *   5. Connect messageReceived signal → receive frames.
 *   6. Call startAsyncReceive() (driver-specific, see VectorCANDriver).
 *   7. Call closeChannel() then shutdown() when done.
 */
class ICANDriver : public QObject
{
    Q_OBJECT

public:
    explicit ICANDriver(QObject* parent = nullptr) : QObject(parent) {}
    ~ICANDriver() override = default;

    // --- Driver lifecycle ---
    virtual bool    initialize()  = 0;
    virtual void    shutdown()    = 0;
    virtual bool    isAvailable() const = 0;
    virtual QString driverName()  const = 0;

    // --- Hardware detection ---
    virtual QList<CANChannelInfo> detectChannels() = 0;

    // --- Channel management ---
    virtual CANResult openChannel(const CANChannelInfo& channel,
                                  const CANBusConfig& config) = 0;
    virtual void      closeChannel() = 0;
    virtual bool      isOpen() const = 0;

    // --- Data operations ---
    virtual CANResult transmit(const CANMessage& msg) = 0;
    virtual CANResult receive(CANMessage& msg, int timeoutMs = 1000) = 0;
    virtual CANResult flushReceiveQueue() = 0;
    virtual QString   lastError() const = 0;

signals:
    /** Emitted from the receive thread — connected via queued connection. */
    void messageReceived(const CANManager::CANMessage& msg);
    void errorOccurred(const QString& error);
    void channelOpened();
    void channelClosed();
};

} // namespace CANManager
