import Foundation

/// A GAP may start before the durable position. The rescan advances using the
/// actual ACKed page tail, while a restart safely repeats the durable prefix.
public struct CardHistoryResumePosition: Sendable {
    public private(set) var scanCursor: CardHistoryCursor?
    public init() {}
    public func requestCursor(durable: CardHistoryCursor) -> CardHistoryCursor { scanCursor ?? durable }
    public mutating func acknowledged(_ cursor: CardHistoryCursor, hasMore: Bool) {
        scanCursor = hasMore ? cursor : nil
    }
}
