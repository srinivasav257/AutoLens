/**
 * @file DBCParser.cpp
 * @brief Implementation of DBC file parser and signal encode/decode.
 */

#include "DBCParser.h"
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QDebug>
#include <cmath>
#include <cstring>
#include <algorithm>

namespace DBCManager {

//=============================================================================
// Bit-level helpers
//=============================================================================

/**
 * @brief Extract raw bits from CAN data (Little Endian / Intel byte order)
 */
static uint64_t extractBitsLE(const uint8_t* data, int dataLen, uint32_t startBit, uint32_t bitLen)
{
    uint64_t result = 0;
    for (uint32_t i = 0; i < bitLen; ++i) {
        uint32_t bitPos = startBit + i;
        uint32_t byteIdx = bitPos / 8;
        uint32_t bitIdx  = bitPos % 8;
        if (static_cast<int>(byteIdx) < dataLen) {
            if (data[byteIdx] & (1u << bitIdx))
                result |= (1ULL << i);
        }
    }
    return result;
}

/**
 * @brief Extract raw bits from CAN data (Big Endian / Motorola byte order)
 *
 * In Motorola byte order the start bit is the MSB position.
 * The DBC convention encodes Motorola start bit as:
 *   byte_number * 8 + (7 - bit_in_byte)
 * We iterate from MSB to LSB across byte boundaries.
 */
static uint64_t extractBitsBE(const uint8_t* data, int dataLen, uint32_t startBit, uint32_t bitLen)
{
    uint64_t result = 0;

    // startBit follows DBC Motorola convention
    uint32_t pos = startBit;
    for (uint32_t i = 0; i < bitLen; ++i) {
        uint32_t byteIdx = pos / 8;
        uint32_t bitIdx  = pos % 8;
        if (static_cast<int>(byteIdx) < dataLen) {
            if (data[byteIdx] & (1u << bitIdx))
                result |= (1ULL << (bitLen - 1 - i));
        }
        // Navigate to next bit in Motorola layout
        if (bitIdx == 0)
            pos += 15;  // jump to next byte, MSB
        else
            pos -= 1;
    }
    return result;
}

/**
 * @brief Place raw bits into CAN data (Little Endian)
 */
static void placeBitsLE(uint8_t* data, int dataLen, uint32_t startBit, uint32_t bitLen, uint64_t value)
{
    for (uint32_t i = 0; i < bitLen; ++i) {
        uint32_t bitPos = startBit + i;
        uint32_t byteIdx = bitPos / 8;
        uint32_t bitIdx  = bitPos % 8;
        if (static_cast<int>(byteIdx) < dataLen) {
            if (value & (1ULL << i))
                data[byteIdx] |= (1u << bitIdx);
            else
                data[byteIdx] &= ~(1u << bitIdx);
        }
    }
}

/**
 * @brief Place raw bits into CAN data (Big Endian / Motorola)
 */
static void placeBitsBE(uint8_t* data, int dataLen, uint32_t startBit, uint32_t bitLen, uint64_t value)
{
    uint32_t pos = startBit;
    for (uint32_t i = 0; i < bitLen; ++i) {
        uint32_t byteIdx = pos / 8;
        uint32_t bitIdx  = pos % 8;
        if (static_cast<int>(byteIdx) < dataLen) {
            if (value & (1ULL << (bitLen - 1 - i)))
                data[byteIdx] |= (1u << bitIdx);
            else
                data[byteIdx] &= ~(1u << bitIdx);
        }
        if (bitIdx == 0)
            pos += 15;
        else
            pos -= 1;
    }
}

//=============================================================================
// DBCSignal implementation
//=============================================================================

int64_t DBCSignal::rawValue(const uint8_t* data, int dataLength) const
{
    uint64_t raw;
    if (byteOrder == ByteOrder::LittleEndian)
        raw = extractBitsLE(data, dataLength, startBit, bitLength);
    else
        raw = extractBitsBE(data, dataLength, startBit, bitLength);

    // Sign extension for signed types
    if (valueType == ValueType::Signed && bitLength < 64) {
        uint64_t signBit = 1ULL << (bitLength - 1);
        if (raw & signBit) {
            raw |= ~((1ULL << bitLength) - 1);  // sign-extend
        }
    }

    return static_cast<int64_t>(raw);
}

void DBCSignal::setRawValue(int64_t raw, uint8_t* data, int dataLength) const
{
    uint64_t uraw = static_cast<uint64_t>(raw);
    if (bitLength < 64)
        uraw &= (1ULL << bitLength) - 1;  // mask to bit length

    if (byteOrder == ByteOrder::LittleEndian)
        placeBitsLE(data, dataLength, startBit, bitLength, uraw);
    else
        placeBitsBE(data, dataLength, startBit, bitLength, uraw);
}

double DBCSignal::rawToPhysical(int64_t raw) const
{
    return static_cast<double>(raw) * factor + offset;
}

int64_t DBCSignal::physicalToRaw(double physical) const
{
    if (std::abs(factor) < 1e-15)
        return 0;
    return static_cast<int64_t>(std::round((physical - offset) / factor));
}

double DBCSignal::decode(const uint8_t* data, int dataLength) const
{
    // Float32 / Float64 signals: extract raw bits and reinterpret as IEEE754
    if (valueType == ValueType::Float32 && bitLength == 32) {
        uint64_t raw;
        if (byteOrder == ByteOrder::LittleEndian)
            raw = extractBitsLE(data, dataLength, startBit, 32);
        else
            raw = extractBitsBE(data, dataLength, startBit, 32);
        uint32_t u32 = static_cast<uint32_t>(raw);
        float f;
        std::memcpy(&f, &u32, sizeof(f));
        return static_cast<double>(f) * factor + offset;
    }
    if (valueType == ValueType::Float64 && bitLength == 64) {
        uint64_t raw;
        if (byteOrder == ByteOrder::LittleEndian)
            raw = extractBitsLE(data, dataLength, startBit, 64);
        else
            raw = extractBitsBE(data, dataLength, startBit, 64);
        double d;
        std::memcpy(&d, &raw, sizeof(d));
        return d * factor + offset;
    }

    // Integer-based signals
    int64_t raw = rawValue(data, dataLength);
    return rawToPhysical(raw);
}

void DBCSignal::encode(double physicalValue, uint8_t* data, int dataLength) const
{
    // Float32 / Float64 signals: convert physical back through factor/offset, then
    // reinterpret as the raw IEEE754 bit pattern.
    if (valueType == ValueType::Float32 && bitLength == 32) {
        float f = static_cast<float>((physicalValue - offset) / factor);
        uint32_t u32;
        std::memcpy(&u32, &f, sizeof(u32));
        uint64_t raw64 = u32;
        if (byteOrder == ByteOrder::LittleEndian)
            placeBitsLE(data, dataLength, startBit, 32, raw64);
        else
            placeBitsBE(data, dataLength, startBit, 32, raw64);
        return;
    }
    if (valueType == ValueType::Float64 && bitLength == 64) {
        double d = (physicalValue - offset) / factor;
        uint64_t raw64;
        std::memcpy(&raw64, &d, sizeof(raw64));
        if (byteOrder == ByteOrder::LittleEndian)
            placeBitsLE(data, dataLength, startBit, 64, raw64);
        else
            placeBitsBE(data, dataLength, startBit, 64, raw64);
        return;
    }

    // Integer-based signals
    int64_t raw = physicalToRaw(physicalValue);
    setRawValue(raw, data, dataLength);
}

QString DBCSignal::valueToString(double physicalValue) const
{
    int64_t raw = physicalToRaw(physicalValue);
    if (valueDescriptions.contains(raw))
        return valueDescriptions[raw];
    if (!unit.isEmpty())
        return QString::number(physicalValue, 'g', 6) + " " + unit;
    return QString::number(physicalValue, 'g', 6);
}

//=============================================================================
// DBCMessage implementation
//=============================================================================

const DBCSignal* DBCMessage::signal(const QString& name) const
{
    for (const auto& sig : signalList) {
        if (sig.name == name)
            return &sig;
    }
    return nullptr;
}

DBCSignal* DBCMessage::signal(const QString& name)
{
    for (auto& sig : signalList) {
        if (sig.name == name)
            return &sig;
    }
    return nullptr;
}

QStringList DBCMessage::signalNames() const
{
    QStringList names;
    names.reserve(signalList.size());
    for (const auto& sig : signalList)
        names.append(sig.name);
    return names;
}

QMap<QString, double> DBCMessage::decodeAll(const uint8_t* data, int dataLength) const
{
    QMap<QString, double> result;
    for (const auto& sig : signalList) {
        result[sig.name] = sig.decode(data, dataLength);
    }
    return result;
}

void DBCMessage::encodeAll(const QMap<QString, double>& signalValues, uint8_t* data, int dataLength) const
{
    for (const auto& sig : signalList) {
        if (signalValues.contains(sig.name)) {
            sig.encode(signalValues[sig.name], data, dataLength);
        }
    }
}

QString DBCMessage::displayString() const
{
    QString idStr = QString("%1").arg(id, 3, 16, QChar('0')).toUpper();
    return QString("0x%1 - %2").arg(idStr, name);
}

//=============================================================================
// DBCDatabase implementation
//=============================================================================

void DBCDatabase::buildIndex()
{
    m_idIndex.clear();
    m_idIndex.reserve(messages.size());
    m_nameIndex.clear();
    m_nameIndex.reserve(messages.size());
    for (int i = 0; i < messages.size(); ++i) {
        uint32_t key = messages[i].id & 0x7FFFFFFF;
        m_idIndex.insert(key, i);
        if (!messages[i].name.isEmpty())
            m_nameIndex.insert(messages[i].name, i);
    }
}

void DBCDatabase::indexLastMessage()
{
    if (messages.isEmpty())
        return;
    int idx = messages.size() - 1;
    uint32_t key = messages[idx].id & 0x7FFFFFFF;
    m_idIndex.insert(key, idx);
    if (!messages[idx].name.isEmpty())
        m_nameIndex.insert(messages[idx].name, idx);
}

const DBCMessage* DBCDatabase::messageById(uint32_t id) const
{
    uint32_t key = id & 0x7FFFFFFF;
    auto it = m_idIndex.find(key);
    if (it != m_idIndex.end()) {
        int idx = it.value();
        if (idx >= 0 && idx < messages.size())
            return &messages[idx];
    }
    return nullptr;
}

DBCMessage* DBCDatabase::messageById(uint32_t id)
{
    uint32_t key = id & 0x7FFFFFFF;
    auto it = m_idIndex.find(key);
    if (it != m_idIndex.end()) {
        int idx = it.value();
        if (idx >= 0 && idx < messages.size())
            return &messages[idx];
    }
    return nullptr;
}

const DBCMessage* DBCDatabase::messageByName(const QString& name) const
{
    auto it = m_nameIndex.find(name);
    if (it != m_nameIndex.end()) {
        int idx = it.value();
        if (idx >= 0 && idx < messages.size())
            return &messages[idx];
    }
    return nullptr;
}

DBCMessage* DBCDatabase::messageByName(const QString& name)
{
    auto it = m_nameIndex.find(name);
    if (it != m_nameIndex.end()) {
        int idx = it.value();
        if (idx >= 0 && idx < messages.size())
            return &messages[idx];
    }
    return nullptr;
}

QStringList DBCDatabase::messageNames() const
{
    QStringList names;
    names.reserve(messages.size());
    for (const auto& msg : messages)
        names.append(msg.name);
    return names;
}

QVector<uint32_t> DBCDatabase::messageIds() const
{
    QVector<uint32_t> ids;
    ids.reserve(messages.size());
    for (const auto& msg : messages)
        ids.append(msg.id);
    return ids;
}

QStringList DBCDatabase::messageDisplayList() const
{
    QStringList list;
    list.reserve(messages.size());
    for (const auto& msg : messages) {
        QString idStr = QString("0x%1").arg(msg.id, 3, 16, QChar('0')).toUpper();
        list.append(QString("%1 - %2").arg(idStr, msg.name));
    }
    // Sort by ID
    std::sort(list.begin(), list.end());
    return list;
}

int DBCDatabase::totalSignalCount() const
{
    int count = 0;
    for (const auto& msg : messages)
        count += msg.signalList.size();
    return count;
}

//=============================================================================
// DBCParser implementation
//=============================================================================

DBCDatabase DBCParser::parseFile(const QString& filePath)
{
    m_errors.clear();

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        addError(0, QString("Cannot open file: %1").arg(filePath));
        return {};
    }

    QTextStream stream(&file);
    QString content = stream.readAll();
    file.close();

    DBCDatabase db;
    db.filename = filePath;
    parse(content, db);
    return db;
}

DBCDatabase DBCParser::parseString(const QString& content)
{
    m_errors.clear();
    DBCDatabase db;
    parse(content, db);
    return db;
}

void DBCParser::addError(int line, const QString& msg)
{
    m_errors.append({line, msg});
    qWarning() << "[DBCParser] Line" << line << ":" << msg;
}

void DBCParser::parse(const QString& content, DBCDatabase& db)
{
    // Split into lines
    QStringList lines = content.split('\n');

    for (int i = 0; i < lines.size(); ++i) {
        QString line = lines[i].trimmed();

        if (line.isEmpty() || line.startsWith("//"))
            continue;

        // VERSION
        if (line.startsWith("VERSION")) {
            parseVersion(line, db);
        }
        // NS_ (new symbols) — skip
        else if (line.startsWith("NS_")) {
            // Skip until next top-level keyword
            while (i + 1 < lines.size()) {
                QString nextLine = lines[i + 1].trimmed();
                if (nextLine.isEmpty() || nextLine.startsWith("BS_") ||
                    nextLine.startsWith("BU_") || nextLine.startsWith("BO_") ||
                    nextLine.startsWith("CM_") || nextLine.startsWith("BA_") ||
                    nextLine.startsWith("VAL_") || nextLine.startsWith("SIG_"))
                    break;
                ++i;
            }
        }
        // BS_ (bus speed) — skip
        else if (line.startsWith("BS_")) {
            // nothing to parse
        }
        // BU_ (nodes)
        else if (line.startsWith("BU_")) {
            parseNodes(line, db);
        }
        // BO_ (message definition)
        else if (line.startsWith("BO_ ")) {
            parseMessage(lines, i, db);
        }
        // CM_ (comment)
        else if (line.startsWith("CM_ ")) {
            parseComment(lines, i, db);
        }
        // VAL_TABLE_
        else if (line.startsWith("VAL_TABLE_ ")) {
            parseValueTable(lines, i, db);
        }
        // VAL_ (value descriptions for signals)
        else if (line.startsWith("VAL_ ")) {
            parseValueDescriptions(lines, i, db);
        }
        // SIG_VALTYPE_
        else if (line.startsWith("SIG_VALTYPE_ ")) {
            parseSignalValueType(line, db);
        }
        // BA_DEF_
        else if (line.startsWith("BA_DEF_ ") || line.startsWith("BA_DEF_DEF_ ")) {
            parseAttributeDefinition(line, db);
        }
        // BA_
        else if (line.startsWith("BA_ ")) {
            parseAttributeValue(line, db);
        }
    }

    // Build the ID->index hash for O(1) lookups
    db.buildIndex();
}

void DBCParser::parseVersion(const QString& line, DBCDatabase& db)
{
    // VERSION "1.0"
    static QRegularExpression re(R"re(VERSION\s+"([^"]*)")re");
    auto match = re.match(line);
    if (match.hasMatch()) {
        db.version = match.captured(1);
    }
}

void DBCParser::parseNodes(const QString& line, DBCDatabase& db)
{
    // BU_ : Node1 Node2 Node3
    QString rest = line.mid(4).trimmed(); // after "BU_ "
    if (rest.startsWith(':'))
        rest = rest.mid(1).trimmed();

    QStringList nodeNames = rest.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    for (const QString& name : nodeNames) {
        DBCNode node;
        node.name = name;
        db.nodes.append(node);
    }
}

void DBCParser::parseMessage(const QStringList& lines, int& index, DBCDatabase& db)
{
    // BO_ <CAN-ID> <MessageName>: <MessageLength> <SendingNode>
    // e.g.: BO_ 2024 OBD2: 8 Vector__XXX
    QString line = lines[index].trimmed();

    static QRegularExpression re(R"(BO_\s+(\d+)\s+(\w+)\s*:\s*(\d+)\s+(\w+))");
    auto match = re.match(line);
    if (!match.hasMatch()) {
        addError(index + 1, "Invalid message definition: " + line);
        return;
    }

    DBCMessage msg;
    uint32_t rawId = match.captured(1).toUInt();

    // In DBC, if bit 31 is set, it's an extended (29-bit) ID
    if (rawId & 0x80000000u) {
        msg.isExtended = true;
        msg.id = rawId & 0x1FFFFFFFu;
    } else {
        msg.isExtended = false;
        msg.id = rawId & 0x7FFu;  // standard 11-bit
    }

    msg.name   = match.captured(2);
    msg.dlc    = match.captured(3).toUInt();
    msg.sender = match.captured(4);

    // Parse signal lines that follow (indented with SG_)
    while (index + 1 < lines.size()) {
        QString nextLine = lines[index + 1].trimmed();
        if (nextLine.startsWith("SG_ ")) {
            ++index;
            parseSignal(nextLine, msg);
        } else {
            break;
        }
    }

    db.messages.append(msg);
    db.indexLastMessage();
}

void DBCParser::parseSignal(const QString& line, DBCMessage& msg)
{
    // SG_ <SignalName> [<MuxIndicator>] : <StartBit>|<Length>@<ByteOrder><ValueType>
    //     (<Factor>,<Offset>) [<Min>|<Max>] "<Unit>" <ReceivingNodes>
    //
    // Example:
    // SG_ EngineSpeed : 24|16@1+ (0.25,0) [0|16383.75] "rpm" Vector__XXX
    // SG_ MuxSignal M : 0|4@1+ (1,0) [0|15] "" Vector__XXX
    // SG_ MuxedSig m2 : 8|8@1+ (1,0) [0|255] "" Vector__XXX

    static QRegularExpression re(
        R"(SG_\s+(\w+)\s*)"                           // Signal name
        R"(([Mm]\d*\s+)?)"                              // Optional mux indicator
        R"(:\s*(\d+)\|(\d+)@([01])([+-]))"             // start|len@byteorder±
        R"(\s*\(\s*([^,]+)\s*,\s*([^)]+)\s*\))"        // (factor,offset)
        R"(\s*\[\s*([^|]+)\|([^\]]+)\s*\])"            // [min|max]
        R"re(\s*"([^"]*)")re"                             // "unit"
        R"(\s*(.*))"                                     // receivers
    );

    auto match = re.match(line);
    if (!match.hasMatch()) {
        // Could be a different format, skip silently
        return;
    }

    DBCSignal sig;
    sig.name = match.captured(1);

    // Mux indicator
    QString muxStr = match.captured(2).trimmed();
    if (!muxStr.isEmpty()) {
        sig.muxIndicator = muxStr;
        if (muxStr == "M") {
            // This is the multiplexer signal
        } else if (muxStr.startsWith('m') || muxStr.startsWith('M')) {
            bool ok;
            sig.muxValue = muxStr.mid(1).toInt(&ok);
        }
    }

    sig.startBit  = match.captured(3).toUInt();
    sig.bitLength = match.captured(4).toUInt();

    sig.byteOrder = (match.captured(5) == "0") ? ByteOrder::BigEndian : ByteOrder::LittleEndian;
    sig.valueType = (match.captured(6) == "-") ? ValueType::Signed : ValueType::Unsigned;

    sig.factor  = match.captured(7).toDouble();
    sig.offset  = match.captured(8).toDouble();
    sig.minimum = match.captured(9).toDouble();
    sig.maximum = match.captured(10).toDouble();
    sig.unit    = match.captured(11);

    // Receivers
    QString receiversStr = match.captured(12).trimmed();
    if (!receiversStr.isEmpty()) {
        sig.receivers = receiversStr.split(QRegularExpression("[,\\s]+"), Qt::SkipEmptyParts);
    }

    msg.signalList.append(sig);
}

void DBCParser::parseComment(const QStringList& lines, int& index, DBCDatabase& db)
{
    // CM_ <type> <id> "comment";
    // Comments can span multiple lines ending with ";
    QString fullLine = lines[index].trimmed();

    // Accumulate multi-line comment
    while (!fullLine.endsWith(';') && index + 1 < lines.size()) {
        ++index;
        fullLine += "\n" + lines[index];
    }

    // Remove trailing semicolon
    if (fullLine.endsWith(';'))
        fullLine.chop(1);

    // CM_ SG_ <msgId> <sigName> "comment"
    static QRegularExpression reSig(R"re(CM_\s+SG_\s+(\d+)\s+(\w+)\s+"(.*)")re",
                                     QRegularExpression::DotMatchesEverythingOption);
    auto matchSig = reSig.match(fullLine);
    if (matchSig.hasMatch()) {
        uint32_t msgId = matchSig.captured(1).toUInt();
        QString sigName = matchSig.captured(2);
        QString comment = matchSig.captured(3);
        // Remove closing quote if present
        if (comment.endsWith('"'))
            comment.chop(1);

        // Handle extended ID bit
        uint32_t lookupId = (msgId & 0x80000000u) ? (msgId & 0x1FFFFFFFu) : (msgId & 0x7FFu);
        auto* msg = db.messageById(lookupId);
        if (msg) {
            auto* sig = msg->signal(sigName);
            if (sig)
                sig->comment = comment;
        }
        return;
    }

    // CM_ BO_ <msgId> "comment"
    static QRegularExpression reMsg(R"re(CM_\s+BO_\s+(\d+)\s+"(.*)")re",
                                     QRegularExpression::DotMatchesEverythingOption);
    auto matchMsg = reMsg.match(fullLine);
    if (matchMsg.hasMatch()) {
        uint32_t msgId = matchMsg.captured(1).toUInt();
        QString comment = matchMsg.captured(2);
        if (comment.endsWith('"'))
            comment.chop(1);

        uint32_t lookupId = (msgId & 0x80000000u) ? (msgId & 0x1FFFFFFFu) : (msgId & 0x7FFu);
        auto* msg = db.messageById(lookupId);
        if (msg)
            msg->comment = comment;
        return;
    }

    // CM_ BU_ <nodeName> "comment"
    static QRegularExpression reNode(R"re(CM_\s+BU_\s+(\w+)\s+"(.*)")re",
                                      QRegularExpression::DotMatchesEverythingOption);
    auto matchNode = reNode.match(fullLine);
    if (matchNode.hasMatch()) {
        QString nodeName = matchNode.captured(1);
        QString comment = matchNode.captured(2);
        if (comment.endsWith('"'))
            comment.chop(1);
        for (auto& node : db.nodes) {
            if (node.name == nodeName) {
                node.comment = comment;
                break;
            }
        }
        return;
    }
}

void DBCParser::parseValueDescriptions(const QStringList& lines, int& index, DBCDatabase& db)
{
    // VAL_ <msgId> <sigName> <value> "desc" <value> "desc" ... ;
    QString fullLine = lines[index].trimmed();
    while (!fullLine.endsWith(';') && index + 1 < lines.size()) {
        ++index;
        fullLine += " " + lines[index].trimmed();
    }
    if (fullLine.endsWith(';'))
        fullLine.chop(1);

    static QRegularExpression reHead(R"(VAL_\s+(\d+)\s+(\w+)\s+(.*))");
    auto match = reHead.match(fullLine);
    if (!match.hasMatch())
        return;

    uint32_t msgId = match.captured(1).toUInt();
    QString sigName = match.captured(2);
    QString rest = match.captured(3).trimmed();

    uint32_t lookupId = (msgId & 0x80000000u) ? (msgId & 0x1FFFFFFFu) : (msgId & 0x7FFu);
    auto* msg = db.messageById(lookupId);
    if (!msg)
        return;
    auto* sig = msg->signal(sigName);
    if (!sig)
        return;

    // Parse value-description pairs: <integer> "string" ...
    static QRegularExpression rePair(R"re((-?\d+)\s+"([^"]*)")re");
    auto it = rePair.globalMatch(rest);
    while (it.hasNext()) {
        auto m = it.next();
        int64_t val = m.captured(1).toLongLong();
        QString desc = m.captured(2);
        sig->valueDescriptions[val] = desc;
    }
}

void DBCParser::parseValueTable(const QStringList& lines, int& index, DBCDatabase& db)
{
    // VAL_TABLE_ <name> <value> "desc" ... ;
    QString fullLine = lines[index].trimmed();
    while (!fullLine.endsWith(';') && index + 1 < lines.size()) {
        ++index;
        fullLine += " " + lines[index].trimmed();
    }
    if (fullLine.endsWith(';'))
        fullLine.chop(1);

    static QRegularExpression reHead(R"(VAL_TABLE_\s+(\w+)\s+(.*))");
    auto match = reHead.match(fullLine);
    if (!match.hasMatch())
        return;

    QString tableName = match.captured(1);
    QString rest = match.captured(2).trimmed();

    QMap<int64_t, QString> table;
    static QRegularExpression rePair(R"re((-?\d+)\s+"([^"]*)")re");
    auto it = rePair.globalMatch(rest);
    while (it.hasNext()) {
        auto m = it.next();
        table[m.captured(1).toLongLong()] = m.captured(2);
    }

    db.valueTables[tableName] = table;
}

void DBCParser::parseSignalValueType(const QString& line, DBCDatabase& db)
{
    // SIG_VALTYPE_ <msgId> <sigName> : <type>;
    // type: 1 = float32, 2 = float64
    static QRegularExpression re(R"(SIG_VALTYPE_\s+(\d+)\s+(\w+)\s*:\s*(\d+)\s*;)");
    auto match = re.match(line);
    if (!match.hasMatch())
        return;

    uint32_t msgId = match.captured(1).toUInt();
    QString sigName = match.captured(2);
    int type = match.captured(3).toInt();

    uint32_t lookupId = (msgId & 0x80000000u) ? (msgId & 0x1FFFFFFFu) : (msgId & 0x7FFu);
    auto* msg = db.messageById(lookupId);
    if (!msg) return;
    auto* sig = msg->signal(sigName);
    if (!sig) return;

    if (type == 1)
        sig->valueType = ValueType::Float32;
    else if (type == 2)
        sig->valueType = ValueType::Float64;
}

void DBCParser::parseAttributeDefinition(const QString& /*line*/, DBCDatabase& /*db*/)
{
    // BA_DEF_ and BA_DEF_DEF_ — store for potential future use
    // Commonly used for GenMsgCycleTime, GenSigStartValue, etc.
    // For now, we skip attribute definitions.
}

void DBCParser::parseAttributeValue(const QString& /*line*/, DBCDatabase& /*db*/)
{
    // BA_ — attribute values; skip for now
}

} // namespace DBCManager
