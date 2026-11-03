import Foundation

/// Each overlaid series gets its own scale; the plot coordinates are shared,
/// while axis labels and cursor values remain in the original measurement unit.
public struct HistoryValueScale: Sendable, Equatable {
    public let lowerBound: Double
    public let upperBound: Double

    public init?(values: [Double], minimumSpan: Double = 1) {
        let finite = values.filter(\.isFinite)
        guard let low = finite.min(), let high = finite.max() else { return nil }
        let minimum = minimumSpan.isFinite && minimumSpan > 0 ? minimumSpan : 1
        let center = low / 2 + high / 2
        let span = max(minimum, high - low)
        guard span.isFinite else { return nil }
        let half = span * 0.58
        let lower = center - half, upper = center + half
        guard lower.isFinite, upper.isFinite, lower < upper else { return nil }
        lowerBound = lower
        upperBound = upper
    }

    public func normalized(_ value: Double) -> Double? {
        guard value.isFinite else { return nil }
        let result = (value - lowerBound) / (upperBound - lowerBound)
        return result.isFinite ? result : nil
    }

    public func actualValue(at fraction: Double) -> Double? {
        guard fraction.isFinite else { return nil }
        let result = lowerBound + fraction * (upperBound - lowerBound)
        return result.isFinite ? result : nil
    }
}
