/**
 * @file DemoCANDriver.cpp
 * @brief Synthetic CAN traffic implementation.
 *
 * Demo mode supports two behaviors:
 * 1) Legacy built-in traffic (hardcoded IDs) when no DBC is loaded.
 * 2) DBC-driven traffic profile when AppController provides a parsed DBC.
 *
 * DBC-driven mode emits real message IDs from the loaded file and encodes
 * payloads via DBC signal definitions, so runtime decode can be verified.
 */

#include "DemoCANDriver.h"

#include <QDebug>

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace DBCManager;

namespace CANManager {

namespace {

constexpr int kTickMs = 10;

bool hasFiniteRange(const DBCSignal& sig)
{
    return std::isfinite(sig.minimum)
        && std::isfinite(sig.maximum)
        && (sig.maximum > sig.minimum);
}

double clampToSignalRange(double value, const DBCSignal& sig)
{
    if (!hasFiniteRange(sig))
        return value;
    return std::clamp(value, sig.minimum, sig.maximum);
}

} // namespace

// ============================================================================
//  Ctor / Dtor
// ============================================================================

DemoCANDriver::DemoCANDriver(QObject* parent) : ICANDriver(parent) {}

DemoCANDriver::~DemoCANDriver()
{
    shutdown();
}

// ============================================================================
//  DBC simulation profile
// ============================================================================

void DemoCANDriver::setSimulationDatabase(const DBCDatabase& db)
{
    m_simPlans.clear();
    m_useDbcSimulation = false;

    if (db.messages.isEmpty()) {
        qDebug() << "[DemoDriver] DBC simulation profile cleared (empty DB)";
        return;
    }

    QVector<DBCMessage> candidates;
    candidates.reserve(db.messages.size());

    // Restrict to classic CAN messages with actual signals.
    for (const auto& msg : db.messages) {
        if (msg.dlc == 0 || msg.dlc > 8)
            continue;
        if (msg.signalList.isEmpty())
            continue;
        candidates.append(msg);
    }

    if (candidates.isEmpty()) {
        qDebug() << "[DemoDriver] DBC loaded but no usable classic messages."
                 << "Using built-in simulation.";
        return;
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const DBCMessage& a, const DBCMessage& b) {
                  if (a.id != b.id)
                      return a.id < b.id;
                  return a.name < b.name;
              });

    // Spread message rates from 10 ms to 2 s.
    static const int periods[] = {1, 2, 5, 10, 20, 50, 100, 200};
    constexpr int periodCount = sizeof(periods) / sizeof(periods[0]);
    constexpr int maxPlans = 8;
    const int count = qMin(candidates.size(), maxPlans);

    m_simPlans.reserve(count);
    for (int i = 0; i < count; ++i) {
        SimMessagePlan plan;
        plan.message = candidates[i];
        plan.periodTicks = periods[i % periodCount];
        m_simPlans.append(plan);
    }

    m_useDbcSimulation = !m_simPlans.isEmpty();

    QStringList summary;
    summary.reserve(m_simPlans.size());
    for (const auto& plan : m_simPlans) {
        summary.append(
            QString("0x%1(%2/%3ms)")
                .arg(plan.message.id, plan.message.isExtended ? 8 : 3, 16, QChar('0'))
                .arg(plan.message.name)
                .arg(plan.periodTicks * kTickMs)
                .toUpper()
        );
    }

    qDebug() << "[DemoDriver] DBC simulation profile active:"
             << summary.join(", ");
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
//  Channel Detection
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
                                     const CANBusConfig& /*config*/)
{
    if (m_open)
        return CANResult::Failure("Already open");

    m_open = true;
    m_tick = 0;
    m_elapsed.start();

    m_timer = new QTimer(this);
    m_timer->setInterval(kTickMs);
    m_timer->setTimerType(Qt::PreciseTimer);
    connect(m_timer, &QTimer::timeout, this, &DemoCANDriver::onTick);
    m_timer->start();

    qDebug() << "[DemoDriver] Channel opened - synthetic traffic started";
    emit channelOpened();
    return CANResult::Success();
}

void DemoCANDriver::closeChannel()
{
    if (!m_open)
        return;

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
//  Transmit / Receive
// ============================================================================

CANResult DemoCANDriver::transmit(const CANMessage& msg)
{
    qDebug() << "[DemoDriver] TX 0x" << Qt::hex << msg.id;

    CANMessage echo = msg;
    echo.isTxConfirm = true;
    echo.timestamp   = static_cast<uint64_t>(m_elapsed.nsecsElapsed());
    emit messageReceived(echo);
    return CANResult::Success();
}

CANResult DemoCANDriver::receive(CANMessage& /*msg*/, int /*timeoutMs*/)
{
    return CANResult::Failure("Demo driver does not support blocking receive");
}

CANResult DemoCANDriver::flushReceiveQueue()
{
    return CANResult::Success();
}

// ============================================================================
//  Timer tick
// ============================================================================

void DemoCANDriver::onTick()
{
    ++m_tick;
    const double seconds = m_elapsed.elapsed() / 1000.0;

    // ------------------------------------------------------------------------
    // DBC-driven simulation mode
    // ------------------------------------------------------------------------
    if (m_useDbcSimulation && !m_simPlans.isEmpty()) {
        for (int planIndex = 0; planIndex < m_simPlans.size(); ++planIndex) {
            const auto& plan = m_simPlans[planIndex];
            if (plan.periodTicks <= 0 || (m_tick % plan.periodTicks) != 0)
                continue;

            const int dataLen = std::clamp(static_cast<int>(plan.message.dlc), 0, 8);
            uint8_t data[8] = {};
            QMap<QString, double> signalValues;

            // Mux handling: pick one active mux branch if present.
            const DBCSignal* muxSignal = nullptr;
            QList<int> muxRawValues;
            for (const auto& sig : plan.message.signalList) {
                if (sig.muxIndicator == "M") {
                    muxSignal = &sig;
                } else if (sig.muxValue >= 0 && !muxRawValues.contains(sig.muxValue)) {
                    muxRawValues.append(sig.muxValue);
                }
            }

            int activeMuxRaw = -1;
            if (muxSignal) {
                if (!muxRawValues.isEmpty()) {
                    const int selector = (m_tick / qMax(1, plan.periodTicks) + planIndex)
                                       % muxRawValues.size();
                    activeMuxRaw = muxRawValues[selector];
                } else {
                    activeMuxRaw = 0;
                }

                double muxPhys = muxSignal->rawToPhysical(activeMuxRaw);
                muxPhys = clampToSignalRange(muxPhys, *muxSignal);
                signalValues.insert(muxSignal->name, muxPhys);
            }

            int signalIndex = 0;
            for (const auto& sig : plan.message.signalList) {
                ++signalIndex;

                if (sig.muxIndicator == "M")
                    continue;

                const bool isMuxedSignal =
                    !sig.muxIndicator.isEmpty()
                    && (sig.muxIndicator.startsWith('m') || sig.muxIndicator.startsWith('M'));
                if (isMuxedSignal && activeMuxRaw >= 0 && sig.muxValue != activeMuxRaw)
                    continue;

                double value = 0.0;

                if (!sig.valueDescriptions.isEmpty()) {
                    QList<int64_t> rawKeys = sig.valueDescriptions.keys();
                    std::sort(rawKeys.begin(), rawKeys.end());
                    const int idx = (m_tick / qMax(1, plan.periodTicks)
                                     + planIndex + signalIndex) % rawKeys.size();
                    value = sig.rawToPhysical(rawKeys[idx]);
                } else if (sig.bitLength == 1
                           && sig.valueType != ValueType::Float32
                           && sig.valueType != ValueType::Float64) {
                    const int toggle = (m_tick / (5 + planIndex + signalIndex)) % 2;
                    value = sig.rawToPhysical(toggle);
                } else if (hasFiniteRange(sig)) {
                    const double center = (sig.minimum + sig.maximum) * 0.5;
                    const double amplitude = (sig.maximum - sig.minimum) * 0.35;
                    const double freq = 0.12 + (planIndex * 0.03) + (signalIndex * 0.015);
                    value = center + amplitude * std::sin(seconds * freq + planIndex);
                } else if (std::abs(sig.initialValue) > 1e-9) {
                    value = sig.initialValue;
                } else {
                    value = sig.offset;
                }

                value = clampToSignalRange(value, sig);
                signalValues.insert(sig.name, value);
            }

            plan.message.encodeAll(signalValues, data, dataLen);
            emitFrame(plan.message.id, data, static_cast<uint8_t>(dataLen), plan.message.isExtended);
        }
        return;
    }

    // ------------------------------------------------------------------------
    // Legacy built-in simulation (fallback when no DBC profile is configured)
    // ------------------------------------------------------------------------

    // 0x0C4 - Engine data (10 ms)
    {
        const double rpm      = 800.0 + 1200.0 * (0.5 + 0.5 * std::sin(seconds * 0.5));
        const double throttle = 10.0  +  40.0 * (0.5 + 0.5 * std::sin(seconds * 0.3));
        const double coolant  = 85.0  +   5.0 * std::sin(seconds * 0.1);

        const uint16_t rawRpm  = static_cast<uint16_t>(rpm / 0.25);
        const uint8_t  rawTps  = static_cast<uint8_t>(throttle / 0.5);
        const uint8_t  rawCool = static_cast<uint8_t>(coolant + 40.0);

        const uint8_t data[8] = {
            static_cast<uint8_t>(rawRpm & 0xFF),
            static_cast<uint8_t>((rawRpm >> 8) & 0xFF),
            rawTps, rawCool, 0, 0, 0, 0
        };
        emitFrame(0x0C4, data, 8);
    }

    // 0x153 - Chassis (20 ms)
    if (m_tick % 2 == 0) {
        const double speed    = 60.0 + 30.0 * std::sin(seconds * 0.2);
        const double brake    = (speed < 50.0) ? 20.0 : 5.0;
        const double steering = 15.0 * std::sin(seconds * 0.7);

        const uint16_t rawSpeed = static_cast<uint16_t>(speed / 0.01);
        const uint8_t  rawBrake = static_cast<uint8_t>(brake);
        const int16_t  rawSteer = static_cast<int16_t>(steering / 0.1);

        const uint8_t data[8] = {
            static_cast<uint8_t>(rawSpeed & 0xFF),
            static_cast<uint8_t>((rawSpeed >> 8) & 0xFF),
            rawBrake,
            static_cast<uint8_t>(rawSteer & 0xFF),
            static_cast<uint8_t>((rawSteer >> 8) & 0xFF),
            0, 0, 0
        };
        emitFrame(0x153, data, 8);
    }

    // 0x1A0 - Body (100 ms)
    if (m_tick % 10 == 0) {
        const double fuel    = 65.0 - (m_tick / 10000.0);
        const double odo     = static_cast<double>(m_tick / 10) * 0.002778;
        const double ambient = 22.0 + 3.0 * std::sin(seconds * 0.05);

        const uint8_t rawFuel = static_cast<uint8_t>(std::clamp(fuel, 0.0, 100.0) / 0.4);
        const uint8_t rawOdo  = static_cast<uint8_t>(static_cast<int>(odo) & 0xFF);
        const uint8_t rawAmb  = static_cast<uint8_t>((ambient + 40.0) / 0.5);

        const uint8_t data[8] = {rawFuel, rawOdo, rawAmb, 0, 0, 0, 0, 0};
        emitFrame(0x1A0, data, 8);
    }

    // 0x6B2 - Gateway (500 ms)
    if (m_tick % 50 == 0) {
        const double voltage = 13.8 + 0.2 * std::sin(seconds * 2.0);
        const uint16_t rawVolt = static_cast<uint16_t>(voltage / 0.1);

        const uint8_t data[8] = {
            0x02,
            static_cast<uint8_t>(rawVolt & 0xFF),
            static_cast<uint8_t>((rawVolt >> 8) & 0xFF),
            0, 0, 0, 0, 0
        };
        emitFrame(0x6B2, data, 8);
    }

    // 0x7DF - OBD keep-alive (5 s)
    if (m_tick % 500 == 0) {
        const uint8_t data[8] = {0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        emitFrame(0x7DF, data, 8);
    }
}

// ============================================================================
//  Helper
// ============================================================================

void DemoCANDriver::emitFrame(uint32_t id, const uint8_t* data, uint8_t dlc, bool isExtended)
{
    CANMessage msg;
    msg.id         = id;
    msg.dlc        = dlc;
    msg.isExtended = isExtended;
    msg.channel    = 1;
    msg.timestamp  = static_cast<uint64_t>(m_elapsed.nsecsElapsed());
    std::memcpy(msg.data, data, dlc);
    emit messageReceived(msg);
}

} // namespace CANManager
