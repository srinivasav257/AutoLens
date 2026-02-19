#pragma once
/**
 * @file DemoCANDriver.h
 * @brief Synthetic CAN traffic generator — no hardware required.
 *
 * DemoCANDriver implements ICANDriver using QTimer to emit realistic-looking
 * CAN frames at configurable rates.  It is selected automatically by
 * AppController when Vector hardware is not found.
 *
 * This lets you develop and test the UI on any machine, even the dev laptop
 * without a test bench.  When you later run the exe on the remote PC with
 * Vector hardware, AppController will pick VectorCANDriver instead.
 *
 * Simulated traffic
 * ─────────────────
 *   0x0C4  10 ms   Engine: RPM + throttle + coolant temp
 *   0x153  20 ms   Chassis: vehicle speed + brake pressure
 *   0x1A0 100 ms   Body: fuel level + odometer
 *   0x6B2 500 ms   Gateway: ignition state + battery voltage
 *   0x7DF   5  s   OBD-II: keep-alive request frame
 *
 * Learning notes
 * ──────────────
 *  • QTimer on the UI thread fires the slot → no extra thread needed.
 *    Each timer tick produces one or more frames and emits messageReceived().
 *  • Because the emit happens on the UI thread (same as AppController),
 *    Qt uses a direct connection — still safe, no queuing needed here.
 *  • Timestamps are in nanoseconds to match the Vector XL API convention.
 */

#include "CANInterface.h"
#include <QTimer>
#include <QElapsedTimer>

namespace CANManager {

class DemoCANDriver : public ICANDriver
{
    Q_OBJECT

public:
    explicit DemoCANDriver(QObject* parent = nullptr);
    ~DemoCANDriver() override;

    // --- ICANDriver interface ---
    bool    initialize()  override;
    void    shutdown()    override;

    /** Always returns true — demo mode needs no external dependencies. */
    bool    isAvailable() const override { return true; }
    QString driverName()  const override { return QStringLiteral("Demo (simulated traffic)"); }

    QList<CANChannelInfo> detectChannels() override;

    CANResult openChannel(const CANChannelInfo& channel,
                          const CANBusConfig& config) override;
    void      closeChannel() override;
    bool      isOpen() const override { return m_open; }

    CANResult transmit(const CANMessage& msg) override;

    /** Not used in demo mode (timer-driven, no blocking receive loop). */
    CANResult receive(CANMessage& msg, int timeoutMs = 1000) override;
    CANResult flushReceiveQueue() override;
    QString   lastError() const override { return m_lastError; }

private slots:
    /** Called by QTimer every 10 ms — the "heartbeat" of the simulation. */
    void onTick();

private:
    // Build and emit one simulated CAN frame
    void emitFrame(uint32_t id, const uint8_t* data, uint8_t dlc,
                   bool isExtended = false);

    bool          m_open      = false;
    QString       m_lastError;
    QTimer*       m_timer     = nullptr;
    QElapsedTimer m_elapsed;        ///< Measures time since openChannel() call
    int           m_tick      = 0;  ///< Tick counter used to derive sub-rates
};

} // namespace CANManager
