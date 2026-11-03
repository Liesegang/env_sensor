import Foundation

public struct CardHistoryStatus: Sendable {
    public let state: UInt8
    public let requestID: UInt32
    public let earliest: CardHistoryCursor
    public let latest: CardHistoryCursor
    public let flags: UInt32
    public let pageCount: UInt16
    public let pageBytes: UInt16
    public let pageLastSequence: UInt32
    public let error: Int32
    public let currentBootID: UInt64
    public let clockRevision: UInt32
    public let utcAnchor: UInt64
    public let uptimeAnchor: UInt32
    public var hasMore: Bool { flags & 1 != 0 }
    public var hasGap: Bool { flags & 2 != 0 }
    public var deviceClock: CardDeviceClock {
        .init(bootID: currentBootID, revision: clockRevision, unixAnchorMilliseconds: utcAnchor * 1_000,
              uptimeAnchorMilliseconds: UInt64(uptimeAnchor) * 1_000)
    }
    public init(packet: Data) throws {
        let b = [UInt8](packet)
        guard b.count == GeneratedHistorySchema.Status.size, b[0...2] == GeneratedHistorySchema.Status.magic + [GeneratedHistorySchema.version], b[3] <= 4 else { throw CardHistoryError.invalidHeader }
        state = b[3]; requestID = b.historyU32(GeneratedHistorySchema.Status.requestId)
        earliest = .init(bootID: b.historyU64(GeneratedHistorySchema.Status.earliestBootId), sequence: b.historyU32(GeneratedHistorySchema.Status.earliestSequence))
        latest = .init(bootID: b.historyU64(GeneratedHistorySchema.Status.latestBootId), sequence: b.historyU32(GeneratedHistorySchema.Status.latestSequence))
        flags = b.historyU32(GeneratedHistorySchema.Status.flags); pageCount = b.historyU16(GeneratedHistorySchema.Status.pageRecordCount); pageBytes = b.historyU16(GeneratedHistorySchema.Status.pageSize)
        pageLastSequence = b.historyU32(GeneratedHistorySchema.Status.pageLastSequence); error = Int32(bitPattern: b.historyU32(GeneratedHistorySchema.Status.error))
        currentBootID = b.historyU64(GeneratedHistorySchema.Status.currentBootId); clockRevision = b.historyU32(GeneratedHistorySchema.Status.clockRevision)
        utcAnchor = b.historyU64(GeneratedHistorySchema.Status.utcAnchorS); uptimeAnchor = b.historyU32(GeneratedHistorySchema.Status.uptimeAnchorS)
        guard flags & ~3 == 0, currentBootID != 0,
              (earliest.bootID == 0) == (earliest.sequence == 0),
              (latest.bootID == 0) == (latest.sequence == 0),
              (earliest == .beginning) == (latest == .beginning),
              state == 4 ? error < 0 : error == 0 else { throw CardHistoryError.invalidHeader }
        if state == 2 {
            guard (1...GeneratedHistorySchema.maxRecords).contains(Int(pageCount)), pageBytes == 32 + pageCount * 68,
                  pageLastSequence != 0 else { throw CardHistoryError.invalidHeader }
        } else {
            guard pageCount == 0, pageBytes == 0, pageLastSequence == 0 else { throw CardHistoryError.invalidHeader }
        }
        guard clockRevision == 0 ? utcAnchor == 0 && uptimeAnchor == 0 :
            (GeneratedHistorySchema.utcMinimum...GeneratedHistorySchema.utcMaximum).contains(utcAnchor) else { throw CardHistoryError.invalidHeader }
    }
}
