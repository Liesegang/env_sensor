import Foundation

/// Built with the snapshot; a scrub uses a binary search instead of scanning it.
public final class HistorySelectionIndex: Sendable {
    public let readings: [StoredReading]
    private let timestamps: [TimeInterval]
    private let positions: [Int64: Int]

    public init(readings: [StoredReading]) {
        self.readings = readings.filter { $0.reading.chartTimestamp != nil }.sorted {
            let first = $0.reading.chartTimestamp ?? .distantPast
            let second = $1.reading.chartTimestamp ?? .distantPast
            return first == second ? $0.id < $1.id : first < second
        }
        timestamps = self.readings.map { $0.reading.chartTimestamp?.timeIntervalSince1970 ?? 0 }
        positions = Dictionary(self.readings.enumerated().map { ($0.element.id, $0.offset) },
                               uniquingKeysWith: { first, _ in first })
    }

    public func nearest(to date: Date, maximumDistance: TimeInterval) -> StoredReading? {
        let target = date.timeIntervalSince1970
        guard target.isFinite, maximumDistance >= 0, !timestamps.isEmpty else { return nil }
        var lower = 0
        var upper = timestamps.count
        while lower < upper {
            let middle = lower + (upper - lower) / 2
            if timestamps[middle] < target { lower = middle + 1 } else { upper = middle }
        }
        var best: Int?
        var distance = TimeInterval.infinity
        let earlier = lower > 0 ? firstIndex(at: timestamps[lower - 1]) : -1
        for index in [earlier, lower] where timestamps.indices.contains(index) {
            let delta = abs(timestamps[index] - target)
            if delta <= maximumDistance, delta < distance {
                best = index
                distance = delta
            }
        }
        return best.map { readings[$0] }
    }

    private func firstIndex(at timestamp: TimeInterval) -> Int {
        var lower = 0
        var upper = timestamps.count
        while lower < upper {
            let middle = lower + (upper - lower) / 2
            if timestamps[middle] < timestamp { lower = middle + 1 } else { upper = middle }
        }
        return lower
    }

    public func adjacent(to id: Int64?, step: Int) -> StoredReading? {
        guard !readings.isEmpty else { return nil }
        let current = id.flatMap { positions[$0] } ?? readings.count - 1
        return readings[min(readings.count - 1, max(0, current + step))]
    }
}
