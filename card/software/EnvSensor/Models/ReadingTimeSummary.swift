import Foundation

public struct ReadingTimeSummary: Sendable {
    public let measuredCount: Int
    public let legacyReceivedCount: Int
    /// Device-wide: unknown dates cannot truthfully be assigned to a time range.
    public let unknownTimeCount: Int
}
