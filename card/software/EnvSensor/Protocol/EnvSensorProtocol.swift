import Foundation

public enum SensorProtocolError: Error, Sendable, Equatable, LocalizedError {
    case invalidHeader
    case invalidLength
    case unsupportedVersion
    case invalidFlags
    case duplicateSensor
    case invalidSensor(UInt8)
    case missingSensor(UInt8)
    case invalidBoolean
    case fragmentMissing
    case fragmentConflict
    case fragmentTimeout
    case sequenceMismatch
    case invalidClock

    public var errorDescription: String? {
        switch self {
        case .invalidHeader: "測定パケットのヘッダーが不正です。"
        case .invalidLength: "測定パケットの長さが不正です。"
        case .unsupportedVersion: "未対応のプロトコルバージョンです。"
        case .invalidFlags: "センサーの状態フラグが不正です。"
        case .duplicateSensor: "センサーのブロックが重複しています。"
        case .invalidSensor(let id): "センサー \(id) のデータが不正です。"
        case .missingSensor(let id): "センサー \(id) のデータが欠けています。"
        case .invalidBoolean: "センサーの boolean 値が不正です。"
        case .fragmentMissing: "BLE 通知の一部が欠けています。次の測定を待ちます。"
        case .fragmentConflict: "BLE 通知の内容が一致しません。"
        case .fragmentTimeout: "BLE 通知の組み立てがタイムアウトしました。"
        case .sequenceMismatch: "BLE 通知の連番が一致しません。"
        case .invalidClock: "同期する時刻が範囲外です。"
        }
    }

    public var canRecoverByReadingLatest: Bool {
        self == .fragmentMissing || self == .fragmentTimeout
    }
}

public enum EnvSensorProtocol {
    public static let serviceUUID = SensorSchema.serviceUUID
    public static let characteristicUUID = SensorSchema.characteristicUUID
    public static let headerSize = 24
    public static let maximumSize = 512

    /// OT v1 keeps the same acknowledged UTC millisecond write as Outdoor.
    public static func encodeClockSync(unixMilliseconds: UInt64) throws -> Data {
        guard (1_577_836_800_000..<4_102_444_800_000).contains(unixMilliseconds) else {
            throw SensorProtocolError.invalidClock
        }
        return Data([0x4f, 0x54, 1, 0] + (0..<8).map {
            UInt8(truncatingIfNeeded: unixMilliseconds >> ($0 * 8))
        })
    }

    public static func decode(_ data: Data, receivedAt: Date = Date()) throws -> SensorReading {
        let bytes = [UInt8](data)
        guard (headerSize...maximumSize).contains(bytes.count) else { throw SensorProtocolError.invalidLength }
        guard bytes[0] == 0x4f, bytes[1] == 0x45 else { throw SensorProtocolError.invalidHeader }
        guard bytes[2] == 1, bytes[3] == 24 else { throw SensorProtocolError.unsupportedVersion }
        guard bytes.u16(4) == bytes.count, bytes.u16(10) == 0 else { throw SensorProtocolError.invalidHeader }
        let enabled = bytes.u16(6)
        let valid = bytes.u16(8)
        guard valid & ~enabled == 0 else { throw SensorProtocolError.invalidFlags }
        var seen: Set<UInt8> = []
        var sensors: [String: SensorBlock] = [:]
        var cursor = headerSize
        while cursor < bytes.count {
            guard cursor + 4 <= bytes.count else { throw SensorProtocolError.invalidLength }
            let id = bytes[cursor]
            let version = bytes[cursor + 1]
            let length = Int(bytes.u16(cursor + 2))
            cursor += 4
            guard seen.insert(id).inserted else { throw SensorProtocolError.duplicateSensor }
            guard length >= 2, cursor + length <= bytes.count else { throw SensorProtocolError.invalidLength }
            if let definition = SensorSchema.sensors.first(where: { $0.id == id }) {
                guard definition.version == version else { throw SensorProtocolError.unsupportedVersion }
                let bit = UInt16(1) << (id - 1)
                let error = Int16(bitPattern: bytes.u16(cursor))
                guard enabled & bit != 0, error <= 0, (valid & bit != 0) == (error == 0) else {
                    throw SensorProtocolError.invalidFlags
                }
                let expected = error == 0 ? 2 + definition.fields.reduce(0) { $0 + $1.type.width } : 2
                guard length == expected else { throw SensorProtocolError.invalidSensor(id) }
                var values: [String: SensorValue] = [:]
                if error == 0 {
                    var fieldCursor = cursor + 2
                    for field in definition.fields {
                        values[field.key] = try bytes.scalar(at: fieldCursor, field: field)
                        fieldCursor += field.type.width
                    }
                }
                sensors[definition.key] = SensorBlock(id: id, error: error, values: values)
            }
            cursor += length
        }
        for sensor in SensorSchema.sensors where enabled & (UInt16(1) << (sensor.id - 1)) != 0 {
            guard sensors[sensor.key] != nil else { throw SensorProtocolError.missingSensor(sensor.id) }
        }
        return SensorReading(sequence: bytes.u32(12), uptimeMilliseconds: bytes.u64(16),
                             enabledMask: enabled, validMask: valid, sensors: sensors,
                             receivedAt: receivedAt, rawFrame: data)
    }
}

/// One instance per BLE connection. The caller serializes push/reset on its delegate queue.
public final class FragmentAssembler {
    private struct PartialFrame {
        let sequence: UInt32
        let length: Int
        let startedAt: TimeInterval
        var bytes: [UInt8]
    }
    private var pending: PartialFrame?
    private var completed: SensorReading?
    public let timeout: TimeInterval

    public init(timeout: TimeInterval = 3) { self.timeout = timeout }
    public func reset() { pending = nil; completed = nil }

    /// A full ATT Read can recover a notification whose first fragment was
    /// missed during process restoration. Validate any later OF tail against
    /// these already validated bytes instead of reporting a false missing head.
    public func rememberCompleted(_ reading: SensorReading) {
        completed = reading
        if pending?.sequence == reading.sequence { pending = nil }
    }

    public func push(_ data: Data, now: TimeInterval = ProcessInfo.processInfo.systemUptime,
                     receivedAt: Date = Date()) throws -> SensorReading? {
        do {
            let bytes = [UInt8](data)
            guard bytes.count > 12, bytes[0] == 0x4f, bytes[1] == 0x46,
                  bytes[3] == 0 else { throw SensorProtocolError.invalidHeader }
            guard bytes[2] == 1 else { throw SensorProtocolError.unsupportedVersion }
            let sequence = bytes.u32(4)
            let length = Int(bytes.u16(8))
            let offset = Int(bytes.u16(10))
            let payload = Array(bytes[12...])
            guard (24...512).contains(length), offset + payload.count <= length else {
                throw SensorProtocolError.invalidLength
            }
            if let completed, completed.sequence == sequence {
                guard completed.rawFrame.count == length,
                      Array(completed.rawFrame[offset..<(offset + payload.count)]) == payload else {
                    throw SensorProtocolError.fragmentConflict
                }
                return nil
            }
            if let frame = pending, now - frame.startedAt > timeout || now < frame.startedAt {
                pending = nil
                if offset != 0 { throw SensorProtocolError.fragmentTimeout }
            }
            if offset == 0, pending?.sequence != sequence {
                pending = PartialFrame(sequence: sequence, length: length, startedAt: now, bytes: [])
            }
            guard var frame = pending, frame.sequence == sequence else { throw SensorProtocolError.fragmentMissing }
            guard frame.length == length else { throw SensorProtocolError.fragmentConflict }
            if offset < frame.bytes.count {
                guard offset + payload.count <= frame.bytes.count,
                      Array(frame.bytes[offset..<(offset + payload.count)]) == payload else {
                    throw SensorProtocolError.fragmentConflict
                }
                return nil // Harmless retransmission of already accepted bytes.
            }
            guard offset == frame.bytes.count else { throw SensorProtocolError.fragmentMissing }
            frame.bytes.append(contentsOf: payload)
            pending = frame
            guard frame.bytes.count == length else { return nil }
            pending = nil
            let sample = try EnvSensorProtocol.decode(Data(frame.bytes), receivedAt: receivedAt)
            guard sample.sequence == sequence else { throw SensorProtocolError.sequenceMismatch }
            rememberCompleted(sample)
            return sample
        } catch {
            // Keep the validated complete frame so delayed duplicate tails can
            // still be checked after another incomplete notification failed.
            pending = nil
            throw error
        }
    }
}

private extension [UInt8] {
    func u16(_ offset: Int) -> UInt16 { UInt16(self[offset]) | UInt16(self[offset + 1]) << 8 }
    func u32(_ offset: Int) -> UInt32 {
        (0..<4).reduce(0) { $0 | UInt32(self[offset + $1]) << ($1 * 8) }
    }
    func u64(_ offset: Int) -> UInt64 {
        (0..<8).reduce(0) { $0 | UInt64(self[offset + $1]) << ($1 * 8) }
    }
    func scalar(at offset: Int, field: SensorFieldDefinition) throws -> SensorValue {
        switch field.type {
        case .u8: return .integer(Int64(self[offset]))
        case .u16: return .integer(Int64(u16(offset)))
        case .i16: return .integer(Int64(Int16(bitPattern: u16(offset))))
        case .u32: return .integer(Int64(u32(offset)))
        case .i32: return .integer(Int64(Int32(bitPattern: u32(offset))))
        case .u64: return .text(field.hexadecimal ? String(format: "0x%016llx", u64(offset)) : String(u64(offset)))
        case .bool:
            guard self[offset] <= 1 else { throw SensorProtocolError.invalidBoolean }
            return .boolean(self[offset] == 1)
        }
    }
}
