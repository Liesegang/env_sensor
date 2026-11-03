import Foundation

/// Measurements after missing-data and boot boundaries have been determined.
public struct HistoryRenderSample: Sendable {
    public let timestamp: Date
    public let value: Double
    public let segment: Int
    public let isIsolated: Bool

    public init(timestamp: Date, value: Double, segment: Int, isIsolated: Bool) {
        self.timestamp = timestamp
        self.value = value
        self.segment = segment
        self.isIsolated = isIsolated
    }
}
