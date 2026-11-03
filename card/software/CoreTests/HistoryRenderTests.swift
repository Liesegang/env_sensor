import Foundation
import Testing
@testable import EnvSensorCore

struct HistoryRenderTests {
    @Test func preparedCoordinatesKeepUnitsAndSeparateMissingSegments() throws {
        let samples = [
            HistoryRenderSample(timestamp: Date(timeIntervalSince1970: 10), value: 600, segment: 0, isIsolated: false),
            HistoryRenderSample(timestamp: Date(timeIntervalSince1970: 11), value: 1_200, segment: 0, isIsolated: false),
            HistoryRenderSample(timestamp: Date(timeIntervalSince1970: 50), value: 900, segment: 1, isIsolated: true)
        ]
        let series = HistoryRenderSeries(key: "co2", samples: samples, minimumSpan: 50)
        let scale = try #require(series.scale)
        #expect(series.points.map(\.actualValue) == [600, 1_200, 900])
        #expect(series.points.map(\.seriesID) == ["co2-0", "co2-0", "co2-1"])
        #expect(series.isolatedPoints == [series.points[2]])
        for point in series.points {
            #expect(abs((scale.actualValue(at: point.normalizedValue) ?? .nan) - point.actualValue) < 0.00001)
        }
        let anotherMetric = HistoryRenderSeries(key: "humidity", samples: samples, minimumSpan: 1)
        #expect(Set(series.points.map(\.seriesID)).isDisjoint(with: anotherMetric.points.map(\.seriesID)))
    }

    @Test func uninstalledOrInvalidSensorHasNoFabricatedLine() {
        let absent = HistoryRenderSeries(key: "formaldehyde", samples: [], minimumSpan: 0.002)
        #expect(absent.scale == nil)
        #expect(absent.points.isEmpty && absent.isolatedPoints.isEmpty)
        let invalid = HistoryRenderSeries(key: "co2", samples: [
            HistoryRenderSample(timestamp: .now, value: .nan, segment: 0, isIsolated: true)
        ], minimumSpan: 1)
        #expect(invalid.scale == nil && invalid.points.isEmpty)
    }

    @Test func indexedSelectionMatchesLinearSelectionIncludingDuplicateTimestamps() throws {
        let readings = [sample(id: 3, measured: 20), sample(id: 2, measured: 10),
                        sample(id: 1, measured: 10), sample(id: 4, measured: nil)]
        let index = HistorySelectionIndex(readings: readings)
        #expect(index.readings.map(\.id) == [1, 2, 3])
        #expect(index.nearest(to: Date(timeIntervalSince1970: 11), maximumDistance: 2)?.id == 1)
        #expect(index.nearest(to: Date(timeIntervalSince1970: 15), maximumDistance: 5)?.id == 1)
        #expect(index.nearest(to: Date(timeIntervalSince1970: 10), maximumDistance: 0)?.id == 1)
        #expect(index.nearest(to: Date(timeIntervalSince1970: 15), maximumDistance: 4) == nil)
        #expect(index.adjacent(to: 1, step: -1)?.id == 1)
        #expect(index.adjacent(to: 3, step: 1)?.id == 3)
        for time in stride(from: 0.0, through: 30.0, by: 0.25) {
            let date = Date(timeIntervalSince1970: time)
            #expect(index.nearest(to: date, maximumDistance: 3)?.id ==
                    HistorySelection.nearest(in: index.readings, to: date, maximumDistance: 3)?.id)
        }
    }

    @Test func twentyFourHourEightSeriesAndScrubbingKeepImmutableBuffers() throws {
        let start = ContinuousClock.now
        let series = (0..<8).map { metric in
            HistoryRenderSeries(key: "metric\(metric)", samples: (0..<900).map { second in
                HistoryRenderSample(timestamp: Date(timeIntervalSince1970: Double(second * 96)),
                    value: Double(metric * 100) + sin(Double(second) / 20), segment: second < 450 ? 0 : 1,
                    isIsolated: false)
            }, minimumSpan: 1)
        }
        let index = HistorySelectionIndex(readings: (0..<900).map {
            sample(id: Int64($0 + 1), measured: Double($0 * 96))
        })
        let identities = series.map(\.id)
        var selected: StoredReading?
        for position in 0..<20_000 {
            selected = index.nearest(to: Date(timeIntervalSince1970: Double(position % 900 * 96)), maximumDistance: 48)
        }
        #expect(selected != nil)
        #expect(series.map(\.id) == identities)
        #expect(series.reduce(0) { $0 + $1.points.count } == 7_200)
        print("history render: 8 × 900 points + 20,000 indexed scrubs in \(start.duration(to: .now))")
    }

    private func sample(id: Int64, measured: TimeInterval?) -> StoredReading {
        let reading = SensorReading(sequence: UInt32(id), uptimeMilliseconds: 1_000,
            enabledMask: 0, validMask: 0, sensors: [:], receivedAt: Date(timeIntervalSince1970: 100_000),
            rawFrame: Data(), measuredAt: measured.map { Date(timeIntervalSince1970: $0) },
            measurementSource: "card-history-v1")
        return StoredReading(id: id, deviceID: "render-test", bootID: "history64:1", reading: reading)
    }
}
