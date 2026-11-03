import Foundation

public enum HistorySelection {
    public static func nearest(in readings: [StoredReading], to date: Date,
                               maximumDistance: TimeInterval) -> StoredReading? {
        guard date.timeIntervalSince1970.isFinite, maximumDistance >= 0 else { return nil }
        var selected: StoredReading?
        var best = TimeInterval.infinity
        for sample in readings {
            guard let timestamp = sample.reading.chartTimestamp else { continue }
            let distance = abs(timestamp.timeIntervalSince(date))
            guard distance <= maximumDistance else { continue }
            if distance < best || (distance == best &&
                timestamp < (selected?.reading.chartTimestamp ?? .distantFuture)) {
                selected = sample
                best = distance
            }
        }
        return selected
    }

    public static func nominalInterval(in readings: [StoredReading]) -> TimeInterval {
        let dates = readings.compactMap { $0.reading.chartTimestamp }.sorted()
        guard dates.count > 1 else { return 1 }
        let intervals = zip(dates, dates.dropFirst()).map { $1.timeIntervalSince($0) }
            .filter { $0 > 0 }.sorted()
        return intervals.isEmpty ? 1 : intervals[intervals.count / 2]
    }
}
