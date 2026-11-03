import Foundation

/// Both coordinate systems are prepared once, rather than during a chart body.
public struct HistoryRenderPoint: Sendable, Equatable {
    public let timestamp: Date
    public let actualValue: Double
    public let normalizedValue: Double
    public let seriesID: String
}
