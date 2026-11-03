import Foundation

/// A shared time window for all plots. Only display samples are bucketed;
/// changing this window never changes the stored one-second measurements.
public struct HistoryViewport: Equatable, Sendable {
    public static let minimumDuration: TimeInterval = 10
    public static let maximumDuration: TimeInterval = 100 * 365.25 * 86_400
    public let start: Date
    public let end: Date
    public var range: ClosedRange<Date> { start...end }
    public var duration: TimeInterval { end.timeIntervalSince(start) }

    public init(endingAt date: Date, duration: TimeInterval) {
        let end = date.timeIntervalSince1970.isFinite ? date : Date(timeIntervalSince1970: 0)
        let span = Self.clampDuration(duration)
        self.end = end
        start = end.addingTimeInterval(-span)
    }

    public init?(start: Date, end: Date) {
        guard start.timeIntervalSince1970.isFinite, end.timeIntervalSince1970.isFinite, start < end else { return nil }
        self.init(endingAt: end, duration: end.timeIntervalSince(start))
    }

    public static func initial(now: Date, latestMeasurement: Date?, isDemo: Bool) -> Self {
        Self(endingAt: isDemo ? latestMeasurement ?? now : now, duration: HistoryRangePreset.day.duration)
    }

    public func ending(at date: Date) -> Self { Self(endingAt: date, duration: duration) }

    /// Magnification > 1 zooms in. A selected point keeps its relative position.
    public func zoomed(magnification: Double, anchor requested: Date? = nil) -> Self {
        let scale = magnification.isFinite && magnification > 0 ? magnification : 1
        let span = Self.clampDuration(duration / scale)
        let rawAnchor = requested ?? start.addingTimeInterval(duration / 2)
        let anchor = min(end, max(start, rawAnchor))
        let fraction = anchor.timeIntervalSince(start) / duration
        let newEnd = anchor.addingTimeInterval(span * (1 - fraction))
        return Self(endingAt: newEnd, duration: span)
    }

    public func moved(fraction: Double, within bounds: ClosedRange<Date>) -> Self {
        guard fraction.isFinite else { return self }
        let proposedEnd = end.addingTimeInterval(duration * fraction)
        // A wide preset may contain white space before the first measurement.
        // It remains anchored at the latest bound when the data span is shorter.
        let earliestEnd = min(bounds.upperBound, bounds.lowerBound.addingTimeInterval(duration))
        return ending(at: min(bounds.upperBound, max(earliestEnd, proposedEnd)))
    }

    /// Apply each gesture update to the viewport captured when two touches
    /// began. A rightward drag moves the content right and shows earlier time.
    /// Bounds can override the finger anchor when the window reaches an edge.
    public func transformed(magnification: Double, anchorFraction: Double,
                            translationFraction: Double, within bounds: ClosedRange<Date>) -> Self {
        guard magnification.isFinite, magnification > 0,
              anchorFraction.isFinite, (0...1).contains(anchorFraction), translationFraction.isFinite,
              bounds.lowerBound.timeIntervalSince1970.isFinite, bounds.upperBound.timeIntervalSince1970.isFinite,
              duration.isFinite, duration > 0 else { return self }
        let rawSpan = duration / magnification
        let span = rawSpan.isFinite ? Self.clampDuration(rawSpan) : Self.maximumDuration
        let currentFraction = anchorFraction + translationFraction
        let anchorTime = start.timeIntervalSince1970 + duration * anchorFraction
        let proposedEnd = anchorTime + span * (1 - currentFraction)
        guard currentFraction.isFinite, anchorTime.isFinite, proposedEnd.isFinite else { return self }
        let upper = bounds.upperBound.timeIntervalSince1970
        let earliestEnd = min(upper, bounds.lowerBound.timeIntervalSince1970 + span)
        let adjustedEnd = min(upper, max(earliestEnd, proposedEnd))
        let result = Self(endingAt: Date(timeIntervalSince1970: adjustedEnd), duration: span)
        guard result.start.timeIntervalSince1970.isFinite, result.duration >= Self.minimumDuration else { return self }
        return result
    }

    /// Coordinates are relative to the actual plot rectangle, excluding axes.
    public func transformed(initialCentroidX: Double, currentCentroidX: Double, plotWidth: Double,
                            magnification: Double, within bounds: ClosedRange<Date>) -> Self {
        guard plotWidth.isFinite, plotWidth > 0, initialCentroidX.isFinite, currentCentroidX.isFinite else { return self }
        return transformed(magnification: magnification, anchorFraction: initialCentroidX / plotWidth,
            translationFraction: (currentCentroidX - initialCentroidX) / plotWidth, within: bounds)
    }

    public var bucketSeconds: Int {
        let needed = max(1, Int(ceil(duration / 900)))
        let steps = [1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 900, 1_800, 3_600, 7_200, 21_600, 86_400]
        return steps.first { $0 >= needed } ?? needed
    }

    public var maximumGapSeconds: Double { max(90, Double(bucketSeconds) * 1.5) }

    private static func clampDuration(_ duration: TimeInterval) -> TimeInterval {
        guard duration.isFinite else { return 86_400 }
        return min(maximumDuration, max(minimumDuration, duration))
    }
}
