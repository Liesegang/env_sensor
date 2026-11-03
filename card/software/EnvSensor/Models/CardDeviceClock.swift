import Foundation

public struct CardDeviceClock: Sendable, Equatable {
    public let bootID: UInt64
    public let revision: UInt32
    public let unixAnchorMilliseconds: UInt64
    public let uptimeAnchorMilliseconds: UInt64
    public var known: Bool { revision != 0 }
    public var offsetSeconds: Double {
        (Double(unixAnchorMilliseconds) - Double(uptimeAnchorMilliseconds)) / 1_000
    }
    public init(bootID: UInt64, revision: UInt32, unixAnchorMilliseconds: UInt64, uptimeAnchorMilliseconds: UInt64) {
        self.bootID = bootID; self.revision = revision
        self.unixAnchorMilliseconds = unixAnchorMilliseconds; self.uptimeAnchorMilliseconds = uptimeAnchorMilliseconds
    }
    public init(packet: Data) throws {
        let b = [UInt8](packet)
        let schema = GeneratedClockSchema.self
        guard b.count == schema.State.size, Array(b[0..<2]) == schema.State.magic,
              b[schema.State.version] == schema.version, b[schema.State.flags] & ~schema.knownFlag == 0 else {
            throw CardHistoryError.invalidHeader
        }
        bootID = b.historyU64(schema.State.bootId); revision = b.historyU32(schema.State.clockRevision)
        unixAnchorMilliseconds = b.historyU64(schema.State.unixAnchorMs)
        uptimeAnchorMilliseconds = b.historyU64(schema.State.uptimeAnchorMs)
        let known = b[schema.State.flags] & schema.knownFlag != 0
        guard bootID != 0, known == (revision != 0),
              known ? (schema.utcMinimumMs...schema.utcMaximumMs).contains(unixAnchorMilliseconds)
                : unixAnchorMilliseconds == 0 && uptimeAnchorMilliseconds == 0 else {
            throw CardHistoryError.invalidHeader
        }
    }
    public func date(uptimeMilliseconds: UInt64) -> Date? {
        guard known else { return nil }
        let unix = offsetSeconds + Double(uptimeMilliseconds) / 1_000
        guard (Double(GeneratedClockSchema.utcMinimumMs) / 1_000...Double(GeneratedClockSchema.utcMaximumMs) / 1_000).contains(unix) else { return nil }
        return Date(timeIntervalSince1970: unix)
    }
}
