import Foundation

public enum CardHistoryError: Error, Sendable, Equatable, LocalizedError {
    case invalidHeader, invalidRecord, invalidCRC, invalidCursor, device(Int32)
    public var errorDescription: String? {
        switch self {
        case .invalidHeader: "履歴ページの形式が不正です。"
        case .invalidRecord: "履歴の測定値が不正です。"
        case .invalidCRC: "履歴ページのCRCが一致しません。保存とACKは行いません。"
        case .invalidCursor: "履歴の保存位置が不正です。"
        case .device(let error): "cardの履歴を取得できませんでした（エラー \(error)）。"
        }
    }
}

public enum CardHistoryProtocol {
    public static let serviceUUID = GeneratedHistorySchema.serviceUuid
    public static let controlUUID = GeneratedHistorySchema.controlUuid
    public static let statusUUID = GeneratedHistorySchema.statusUuid
    public static let dataUUID = GeneratedHistorySchema.dataUuid
    public static func control(operation: UInt8, requestID: UInt32, cursor: CardHistoryCursor) throws -> Data {
        let empty = cursor == .beginning
        let complete = cursor.bootID != 0 && cursor.sequence != 0
        guard requestID != 0,
              operation == GeneratedHistorySchema.Operations.pull ? empty || complete :
                operation == GeneratedHistorySchema.Operations.ack ? complete :
                operation == GeneratedHistorySchema.Operations.cancel && empty else { throw CardHistoryError.invalidCursor }
        var data = Data(GeneratedHistorySchema.Control.magic + [GeneratedHistorySchema.version, operation])
        data.historyAppend(requestID); data.historyAppend(cursor.bootID); data.historyAppend(cursor.sequence)
        return data
    }
}

public struct CardHistoryPage: Sendable {
    public let requestID: UInt32
    public let bootID: UInt64
    public let records: [CardHistoryRecord]
    public let clockRevision: UInt32
    public let flags: UInt8
    public let raw: Data
    public var cursor: CardHistoryCursor { CardHistoryCursor(bootID: bootID, sequence: records.last?.sequence ?? 0) }
    public init(packet: Data) throws {
        let b = [UInt8](packet)
        guard b.count >= GeneratedHistorySchema.Page.size, Array(b[0..<2]) == GeneratedHistorySchema.Page.magic,
              b[GeneratedHistorySchema.Page.version] == GeneratedHistorySchema.version,
              b[GeneratedHistorySchema.Page.flags] & ~3 == 0,
              b.historyU16(GeneratedHistorySchema.Page.recordSize) == GeneratedHistorySchema.Record.size else { throw CardHistoryError.invalidHeader }
        let count = Int(b.historyU16(GeneratedHistorySchema.Page.recordCount))
        guard (1...GeneratedHistorySchema.maxRecords).contains(count), b.count == 32 + count * 68,
              b.historyU32(GeneratedHistorySchema.Page.requestId) != 0, b.historyU64(GeneratedHistorySchema.Page.bootId) != 0 else { throw CardHistoryError.invalidHeader }
        var checked = packet
        checked.replaceSubrange(24..<28, with: [0, 0, 0, 0])
        guard CardImageProtocol.crc32(checked) == b.historyU32(GeneratedHistorySchema.Page.crc32) else { throw CardHistoryError.invalidCRC }
        requestID = b.historyU32(GeneratedHistorySchema.Page.requestId); bootID = b.historyU64(GeneratedHistorySchema.Page.bootId); raw = packet
        clockRevision = b.historyU32(GeneratedHistorySchema.Page.clockRevision)
        flags = b[GeneratedHistorySchema.Page.flags]
        records = try (0..<count).map { try CardHistoryRecord(raw: packet.subdata(in: (32 + $0 * 68)..<(32 + ($0 + 1) * 68))) }
        guard records.first?.sequence == b.historyU32(GeneratedHistorySchema.Page.firstSequence),
              zip(records, records.dropFirst()).allSatisfy({ $0.sequence < $1.sequence }) else {
            throw CardHistoryError.invalidRecord
        }
        let discontinuous = zip(records, records.dropFirst()).contains { UInt64($0.sequence) + 1 != UInt64($1.sequence) }
        guard !discontinuous || flags & UInt8(GeneratedHistorySchema.Flags.gap) != 0 else {
            throw CardHistoryError.invalidRecord
        }
    }
}

extension Data {
    mutating func historyAppend<T: FixedWidthInteger>(_ value: T) {
        append(contentsOf: (0..<MemoryLayout<T>.size).map { UInt8(truncatingIfNeeded: value >> ($0 * 8)) })
    }
}
