import Foundation

struct HistoryChartPoint: Identifiable, Sendable {
    let id: Int64
    let receivedAt: Date
    let value: Double
    let segment: Int
    let isIsolated: Bool
    var timestamp: Date { receivedAt }

    static func make(readings: [StoredReading], metric: SensorMetric, since: Date,
                     maximumGapSeconds: Double = 90) -> [HistoryChartPoint] {
        let ordered = readings.filter { ($0.reading.chartTimestamp ?? .distantPast) >= since }
            .sorted { first, second in
                first.reading.chartTimestamp == second.reading.chartTimestamp
                    ? first.id < second.id
                    : (first.reading.chartTimestamp ?? .distantPast) < (second.reading.chartTimestamp ?? .distantPast)
            }
        var result: [HistoryChartPoint] = []
        var previous: StoredReading?
        var segment = 0
        var missing = false
        for record in ordered {
            guard let value = metric.value(in: record.reading), value.isFinite else {
                missing = true
                continue
            }
            if let previous, missing || previous.bootID != record.bootID ||
                (record.reading.chartTimestamp ?? .distantPast).timeIntervalSince(previous.reading.chartTimestamp ?? .distantPast) > max(1, maximumGapSeconds) {
                segment += 1
            }
            result.append(HistoryChartPoint(id: record.id, receivedAt: record.reading.chartTimestamp ?? .distantPast,
                value: value, segment: segment, isIsolated: false))
            previous = record
            missing = false
        }
        let counts = Dictionary(grouping: result, by: \.segment).mapValues(\.count)
        return result.map { point in
            HistoryChartPoint(id: point.id, receivedAt: point.receivedAt, value: point.value,
                segment: point.segment, isIsolated: counts[point.segment] == 1)
        }
    }
}
