#pragma once

#include <QString>
#include <QVector>

#include "hardware/CANInterface.h"

/**
 * @brief Offline trace import helpers for ASC and BLF log formats.
 *
 * All methods are static and return an empty string on success.
 */
class TraceImporter
{
public:
    /**
     * @brief Load a trace file based on extension (.asc / .blf).
     * @param filePath     Source file path.
     * @param outMessages  Parsed CAN/CAN-FD frames.
     * @return Empty string on success, otherwise a human-readable error.
     */
    static QString load(const QString& filePath,
                        QVector<CANManager::CANMessage>& outMessages);

private:
    static QString loadAsc(const QString& filePath,
                           QVector<CANManager::CANMessage>& outMessages);
    static QString loadBlf(const QString& filePath,
                           QVector<CANManager::CANMessage>& outMessages);
};
