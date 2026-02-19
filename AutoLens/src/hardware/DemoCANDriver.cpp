/**
 * @file DemoCANDriver.cpp
 * @brief Synthetic CAN traffic — implementation.
 *
 * The timer fires every 10 ms (m_tick increments each call):
 *
 *   m_tick % 1  ==  0  → every tick (10 ms)  → 0x0C4  Engine
 *   m_tick % 2  ==  0  → every 20 ms         → 0x153  Chassis
 *   m_tick % 10 ==  0  → every 100 ms        → 0x1A0  Body
 *   m_tick % 50 ==  0  → every 500 ms        → 0x6B2  Gateway
 *   m_tick % 500 == 0  → every 5 s           → 0x7DF  OBD-II
 *
 * Signal values vary sinusoidally so the decoded view shows movement.
 */

#include "DemoCANDriver.h"
#include <QDebug>
#include <cmath>
#include <cstring>

namespace CANManager {

// ============================================================================
//  Ctor / Dtor
// ============================================================================

DemoCANDriver::DemoCANDriver(QObject* parent) : ICANDriver(parent) {}

DemoCANDriver::~DemoCANDriver()
{
    shutdown();
}

// ============================================================================
//  Lifecycle
// ============================================================================

bool DemoCANDriver::initialize()
{
    qDebug() << "[DemoDriver] Initialized (no hardware required)";
    return true;
}

void DemoCANDriver::shutdown()
{
    closeChannel();
}

// ============================================================================
//  Channel Detection — returns one fake "Demo" channel
// ============================================================================

QList<CANChannelInfo> DemoCANDriver::detectChannels()
{
    CANChannelInfo ch;
    ch.name        = QStringLiteral("Demo Channel 1");
    ch.hwTypeName  = QStringLiteral("Simulated");
    ch.channelMask = 1;
    ch.supportsFD  = false;
    return {ch};
}

// ============================================================================
//  Open / Close
// ============================================================================

CANResult DemoCANDriver::openChannel(const CANChannelInfo& /*channel*/,
                                      const CANBusConfig&  /*config*/)
{
    if (m_open)
        return CANResult::Failure("Already open");

    m_open = true;
    m_tick = 0;
    m_elapsed.start();

    // QTimer ticks every 10 ms on the UI event loop.
    // Using a QTimer here (instead of a thread) keeps things simple:
    // all signal emissions happen on the UI thread, so AppController's
    // onFrameReceived() slot is called with a direct connection.
    m_timer = new QTimer(this);
    m_timer->setInterval(10);                          // 10 ms
    m_timer->setTimerType(Qt::PreciseTimer);           // requests OS high-res timer
    connect(m_timer, &QTimer::timeout, this, &DemoCANDriver::onTick);
    m_timer->start();

    qDebug() << "[DemoDriver] Channel opened — synthetic traffic started";
    emit channelOpened();
    return CANResult::Success();
}

void DemoCANDriver::closeChannel()
{
    if (!m_open) return;
    if (m_timer) {
        m_timer->stop();
        delete m_timer;
        m_timer = nullptr;
    }
    m_open = false;
    qDebug() << "[DemoDriver] Channel closed";
    emit channelClosed();
}

// ============================================================================
//  Transmit — just acknowledge, log to debug
// ============================================================================

CANResult DemoCANDriver::transmit(const CANMessage& msg)
{
    qDebug() << "[DemoDriver] TX 0x" << Qt::hex << msg.id;
    // Optionally loop back the frame so the trace window shows TX frames too
    CANMessage echo = msg;
    echo.isTxConfirm = true;
    echo.timestamp   = static_cast<uint64_t>(m_elapsed.nsecsElapsed());
    emit messageReceived(echo);
    return CANResult::Success();
}

CANResult DemoCANDriver::receive(CANMessage& /*msg*/, int /*timeoutMs*/)
{
    // Demo driver is purely timer-driven; blocking receive is not used.
    return CANResult::Failure("Demo driver does not support blocking receive");
}

CANResult DemoCANDriver::flushReceiveQueue()
{
    return CANResult::Success();
}

// ============================================================================
//  Timer tick — generate synthetic frames
// ============================================================================

void DemoCANDriver::onTick()
{
    ++m_tick;

    // Time base in seconds (for sinusoidal variation)
    double t = m_elapsed.elapsed() / 1000.0;

    // -----------------------------------------------------------------------
    //  0x0C4 — Engine data (10 ms, every tick)
    //  Byte 0-1 : EngineRPM  = 0…8000 rpm   factor=0.25  offset=0
    //             Raw = RPM / 0.25  → at 2000 rpm raw = 8000
    //  Byte 2   : ThrottlePos = 0…100 %      factor=0.5   offset=0
    //  Byte 3   : CoolantTemp = -40…215 °C   factor=1     offset=-40
    //  Byte 4-5 : (reserved, 0)
    //  Byte 6-7 : (reserved, 0)
    // -----------------------------------------------------------------------
    {
        double rpm     = 800.0 + 1200.0 * (0.5 + 0.5 * std::sin(t * 0.5));
        double throttle= 10.0  + 40.0  * (0.5 + 0.5 * std::sin(t * 0.3));
        double coolant = 85.0  + 5.0   * std::sin(t * 0.1);

        uint16_t rawRpm  = static_cast<uint16_t>(rpm / 0.25);
        uint8_t  rawTps  = static_cast<uint8_t>(throttle / 0.5);
        uint8_t  rawCool = static_cast<uint8_t>(coolant + 40.0);

        uint8_t data[8] = {
            static_cast<uint8_t>(rawRpm & 0xFF),
            static_cast<uint8_t>((rawRpm >> 8) & 0xFF),
            rawTps, rawCool, 0, 0, 0, 0
        };
        emitFrame(0x0C4, data, 8);
    }

    // -----------------------------------------------------------------------
    //  0x153 — Chassis (20 ms)
    //  Byte 0-1 : VehicleSpeed  = 0…250 km/h  factor=0.01  offset=0
    //  Byte 2   : BrakePressure = 0…255 bar    factor=1     offset=0
    //  Byte 3   : SteeringAngle = -180…180 deg factor=0.1   offset=0 (signed)
    // -----------------------------------------------------------------------
    if (m_tick % 2 == 0) {
        double speed   = 60.0 + 30.0 * std::sin(t * 0.2);
        double brake   = (speed < 50.0) ? 20.0 : 5.0;
        double steering= 0.0  + 15.0  * std::sin(t * 0.7);

        uint16_t rawSpd  = static_cast<uint16_t>(speed / 0.01);
        uint8_t  rawBrk  = static_cast<uint8_t>(brake);
        int16_t  rawSteer= static_cast<int16_t>(steering / 0.1);

        uint8_t data[8] = {
            static_cast<uint8_t>(rawSpd & 0xFF),
            static_cast<uint8_t>((rawSpd >> 8) & 0xFF),
            rawBrk,
            static_cast<uint8_t>(rawSteer & 0xFF),
            static_cast<uint8_t>((rawSteer >> 8) & 0xFF),
            0, 0, 0
        };
        emitFrame(0x153, data, 8);
    }

    // -----------------------------------------------------------------------
    //  0x1A0 — Body electronics (100 ms)
    //  Byte 0 : FuelLevel  = 0…100 %   factor=0.4   offset=0
    //  Byte 1 : Odometer   = 0…65535 km (lower 8 bits, wraps)
    //  Byte 2 : AmbientTemp= -40…80°C  factor=0.5   offset=-40
    // -----------------------------------------------------------------------
    if (m_tick % 10 == 0) {
        double fuel    = 65.0 - (m_tick / 10000.0);  // slowly draining
        double odo     = static_cast<double>(m_tick / 10) * 0.002778; // ~10m per 100ms
        double ambient = 22.0 + 3.0 * std::sin(t * 0.05);

        uint8_t rawFuel= static_cast<uint8_t>(std::clamp(fuel, 0.0, 100.0) / 0.4);
        uint8_t rawOdo = static_cast<uint8_t>(static_cast<int>(odo) & 0xFF);
        uint8_t rawAmb = static_cast<uint8_t>((ambient + 40.0) / 0.5);

        uint8_t data[8] = { rawFuel, rawOdo, rawAmb, 0, 0, 0, 0, 0 };
        emitFrame(0x1A0, data, 8);
    }

    // -----------------------------------------------------------------------
    //  0x6B2 — Gateway / network management (500 ms)
    //  Byte 0 : IgnitionState  (0=Off 1=Acc 2=On 3=Start)
    //  Byte 1-2: BatteryVoltage = 0…25.5V  factor=0.1  offset=0
    // -----------------------------------------------------------------------
    if (m_tick % 50 == 0) {
        double voltage = 13.8 + 0.2 * std::sin(t * 2.0);
        uint16_t rawVolt = static_cast<uint16_t>(voltage / 0.1);

        uint8_t data[8] = {
            0x02,   // ignition ON
            static_cast<uint8_t>(rawVolt & 0xFF),
            static_cast<uint8_t>((rawVolt >> 8) & 0xFF),
            0, 0, 0, 0, 0
        };
        emitFrame(0x6B2, data, 8);
    }

    // -----------------------------------------------------------------------
    //  0x7DF — OBD-II functional request (keep-alive, 5 s)
    //  Standard OBD2 Mode 01 PID 0x00 (supported PIDs query)
    // -----------------------------------------------------------------------
    if (m_tick % 500 == 0) {
        uint8_t data[8] = { 0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
        emitFrame(0x7DF, data, 8);
    }
}

// ============================================================================
//  Helper — set timestamp and emit
// ============================================================================

void DemoCANDriver::emitFrame(uint32_t id, const uint8_t* data, uint8_t dlc,
                               bool isExtended)
{
    CANMessage msg;
    msg.id         = id;
    msg.dlc        = dlc;
    msg.isExtended = isExtended;
    msg.channel    = 1;
    msg.timestamp  = static_cast<uint64_t>(m_elapsed.nsecsElapsed());
    memcpy(msg.data, data, dlc);
    emit messageReceived(msg);
}

} // namespace CANManager
