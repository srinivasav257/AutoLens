#pragma once
/**
 * @file DBCParser.h
 * @brief DBC file parser for CAN database definitions.
 *
 * Parses Vector DBC files to extract:
 * - Message definitions (ID, name, length, sender)
 * - Signal definitions (start bit, length, byte order, factor, offset, limits, unit)
 * - Value tables and enumerations
 * - Nodes and comments
 */

#include <QString>
#include <QVector>
#include <QMap>
#include <QHash>
#include <QVariant>
#include <cstdint>

namespace DBCManager {

//=============================================================================
// Enumerations
//=============================================================================

/**
 * @brief Byte order of a signal within the CAN message
 */
enum class ByteOrder {
    LittleEndian = 0,   ///< Intel byte order (LSB first)
    BigEndian    = 1    ///< Motorola byte order (MSB first)
};

/**
 * @brief Value type of the signal
 */
enum class ValueType {
    Unsigned = 0,
    Signed   = 1,
    Float32  = 2,
    Float64  = 3
};

//=============================================================================
// DBCSignal — Individual signal within a CAN message
//=============================================================================

/**
 * @brief Represents a single signal extracted from a DBC file.
 */
struct DBCSignal
{
    QString   name;                 ///< Signal name
    uint32_t  startBit     = 0;     ///< Start bit position
    uint32_t  bitLength    = 0;     ///< Length in bits
    ByteOrder byteOrder    = ByteOrder::LittleEndian;
    ValueType valueType    = ValueType::Unsigned;
    double    factor       = 1.0;   ///< Scaling factor
    double    offset       = 0.0;   ///< Offset value
    double    minimum      = 0.0;   ///< Physical minimum value
    double    maximum      = 0.0;   ///< Physical maximum value
    QString   unit;                 ///< Unit string (e.g., "km/h", "degC")
    QStringList receivers;          ///< Receiving node names
    QString   comment;              ///< Signal comment
    double    initialValue = 0.0;   ///< Initial/default value

    /// Value descriptions (e.g., 0="Off", 1="On")
    QMap<int64_t, QString> valueDescriptions;

    /// Multiplexer indicator: "" = normal, "M" = multiplexer, "m<N>" = multiplexed
    QString   muxIndicator;
    int       muxValue = -1;        ///< Mux switch value (if multiplexed signal)

    /**
     * @brief Decode raw bits to physical value
     */
    double decode(const uint8_t* data, int dataLength) const;

    /**
     * @brief Encode physical value into raw bits
     */
    void encode(double physicalValue, uint8_t* data, int dataLength) const;

    /**
     * @brief Get the raw integer value from data (before factor/offset)
     */
    int64_t rawValue(const uint8_t* data, int dataLength) const;

    /**
     * @brief Set raw integer value into data
     */
    void setRawValue(int64_t raw, uint8_t* data, int dataLength) const;

    /**
     * @brief Convert physical value → raw integer
     */
    int64_t physicalToRaw(double physical) const;

    /**
     * @brief Convert raw integer → physical value
     */
    double rawToPhysical(int64_t raw) const;

    /**
     * @brief Get display string for a value (uses valueDescriptions if available)
     */
    QString valueToString(double physicalValue) const;
};

//=============================================================================
// DBCMessage — CAN message definition
//=============================================================================

/**
 * @brief Represents a CAN message definition from a DBC file.
 */
struct DBCMessage
{
    uint32_t  id       = 0;         ///< CAN arbitration ID
    QString   name;                 ///< Message name
    uint32_t  dlc      = 8;         ///< Data length code (bytes)
    QString   sender;               ///< Transmitting node name
    QString   comment;              ///< Message comment
    bool      isExtended = false;   ///< 29-bit extended ID

    /// Signals in this message (ordered by start bit)
    QVector<DBCSignal> signalList;

    /**
     * @brief Find a signal by name
     * @return Pointer to signal, or nullptr
     */
    const DBCSignal* signal(const QString& name) const;
    DBCSignal* signal(const QString& name);

    /**
     * @brief Get signal names
     */
    QStringList signalNames() const;

    /**
     * @brief Decode all signals from raw data
     * @return Map of signal name → physical value
     */
    QMap<QString, double> decodeAll(const uint8_t* data, int dataLength) const;

    /**
     * @brief Encode signal values into raw data
     * @param signalValues Map of signal name → physical value
     * @param data Output buffer (must be at least dlc bytes)
     */
    void encodeAll(const QMap<QString, double>& signalValues, uint8_t* data, int dataLength) const;

    /**
     * @brief Display string: "0xID MsgName"
     */
    QString displayString() const;
};

//=============================================================================
// DBCNode — Network node definition
//=============================================================================

struct DBCNode
{
    QString name;
    QString comment;
};

//=============================================================================
// DBCDatabase — Complete parsed DBC database
//=============================================================================

/**
 * @brief Complete DBC database — all messages, signals, nodes, etc.
 */
struct DBCDatabase
{
    QString version;                            ///< DBC version string
    QString filename;                           ///< Source file path
    QVector<DBCNode> nodes;                     ///< Network nodes
    QVector<DBCMessage> messages;               ///< All messages
    QMap<QString, QMap<int64_t, QString>> valueTables;  ///< Named value tables (VAL_TABLE_)

    /**
     * @brief Rebuild the internal ID→index hash after messages are modified.
     * Called automatically by DBCParser after parsing.
     */
    void buildIndex();

    /**
     * @brief Incrementally add the last message to the ID index.
     * Call after appending a message so that messageById() works
     * during the rest of the parse (comments, value descriptions, etc.).
     */
    void indexLastMessage();

    /**
     * @brief Find message by ID (O(1) hash lookup)
     */
    const DBCMessage* messageById(uint32_t id) const;
    DBCMessage* messageById(uint32_t id);

    /**
     * @brief Find message by name
     */
    const DBCMessage* messageByName(const QString& name) const;
    DBCMessage* messageByName(const QString& name);

    /**
     * @brief Get all message names
     */
    QStringList messageNames() const;

    /**
     * @brief Get all message IDs
     */
    QVector<uint32_t> messageIds() const;

    /**
     * @brief Get sorted message display list ("0xID - Name")
     */
    QStringList messageDisplayList() const;

    /**
     * @brief Total signal count across all messages
     */
    int totalSignalCount() const;

    /**
     * @brief Check if database is empty
     */
    bool isEmpty() const { return messages.isEmpty(); }

private:
    /// Hash of (id & 0x7FFFFFFF) → index into messages vector
    QHash<uint32_t, int> m_idIndex;
    /// Hash of message name → index into messages vector
    QHash<QString, int> m_nameIndex;
};

//=============================================================================
// DBCParseError
//=============================================================================

struct DBCParseError
{
    int line = 0;
    QString message;
};

//=============================================================================
// DBCParser — DBC file parser
//=============================================================================

/**
 * @brief Parser for Vector DBC (*.dbc) files.
 *
 * Supports:
 * - VERSION, NS_, BS_, BU_ (nodes)
 * - BO_ (messages), SG_ (signals)
 * - CM_ (comments for messages and signals)
 * - BA_DEF_, BA_ (attribute definitions and values)
 * - VAL_TABLE_, VAL_ (value descriptions)
 * - SIG_VALTYPE_ (signal value types: float/double)
 * - Multiplexed signals (M, m<N>)
 *
 * Usage:
 * @code
 * DBCParser parser;
 * DBCDatabase db = parser.parseFile("vehicle.dbc");
 * if (parser.hasErrors())
 *     qWarning() << parser.errors();
 * @endcode
 */
class DBCParser
{
public:
    DBCParser() = default;

    /**
     * @brief Parse a DBC file
     * @param filePath Path to the .dbc file
     * @return Parsed database (may be partial if errors occurred)
     */
    DBCDatabase parseFile(const QString& filePath);

    /**
     * @brief Parse DBC content from a string
     */
    DBCDatabase parseString(const QString& content);

    /**
     * @brief Check for parse errors
     */
    bool hasErrors() const { return !m_errors.isEmpty(); }

    /**
     * @brief Get parse errors
     */
    QVector<DBCParseError> errors() const { return m_errors; }

private:
    void parse(const QString& content, DBCDatabase& db);
    void parseVersion(const QString& line, DBCDatabase& db);
    void parseNodes(const QString& line, DBCDatabase& db);
    void parseMessage(const QStringList& lines, int& index, DBCDatabase& db);
    void parseSignal(const QString& line, DBCMessage& msg);
    void parseComment(const QStringList& lines, int& index, DBCDatabase& db);
    void parseValueDescriptions(const QStringList& lines, int& index, DBCDatabase& db);
    void parseValueTable(const QStringList& lines, int& index, DBCDatabase& db);
    void parseSignalValueType(const QString& line, DBCDatabase& db);
    void parseAttributeDefinition(const QString& line, DBCDatabase& db);
    void parseAttributeValue(const QString& line, DBCDatabase& db);

    void addError(int line, const QString& msg);

    QVector<DBCParseError> m_errors;
};

} // namespace DBCManager
