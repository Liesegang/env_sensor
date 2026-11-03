import Foundation
import Testing
@testable import EnvSensorCore

struct HistoryViewerTests {
    @Test func demoStartsAtItsActualDataAndLiveUsesCurrentTime() {
        let now = Date(timeIntervalSince1970: 1_900_000_000)
        let last = Date(timeIntervalSince1970: 1_780_000_000)
        let demo = HistoryViewport.initial(now: now, latestMeasurement: last, isDemo: true)
        #expect(demo.end == last)
        #expect(demo.range.contains(last))
        #expect(HistoryViewport.initial(now: now, latestMeasurement: last, isDemo: false).end == now)
        #expect(HistoryViewport.initial(now: now, latestMeasurement: nil, isDemo: true).end == now)
    }

    @Test func zoomKeepsCursorAtSameRelativePositionAndStopsAtOneSecondDetail() {
        let view = HistoryViewport(endingAt: Date(timeIntervalSince1970: 100_000), duration: 3_600)
        let anchor = view.start.addingTimeInterval(900)
        let zoomed = view.zoomed(magnification: 2, anchor: anchor)
        #expect(zoomed.duration == 1_800)
        #expect(abs(anchor.timeIntervalSince(zoomed.start) / zoomed.duration - 0.25) < 0.000001)
        let close = view.zoomed(magnification: 1_000_000)
        #expect(close.duration == HistoryViewport.minimumDuration)
        #expect(close.bucketSeconds == 1)
        #expect(close.zoomed(magnification: .nan) == close)
        #expect(close.zoomed(magnification: -1) == close)
    }

    @Test func movingDoesNotPassDataBoundsAndCustomDatesRequireForwardOrder() {
        let bounds = Date(timeIntervalSince1970: 0)...Date(timeIntervalSince1970: 10_000)
        let view = HistoryViewport(endingAt: bounds.upperBound, duration: 1_000)
        #expect(view.moved(fraction: 1, within: bounds) == view)
        #expect(view.moved(fraction: -0.5, within: bounds).end == Date(timeIntervalSince1970: 9_500))
        #expect(view.moved(fraction: -100, within: bounds).start == bounds.lowerBound)
        let wide = HistoryViewport(endingAt: bounds.upperBound, duration: 86_400)
        #expect(wide.moved(fraction: -1, within: bounds).end == bounds.upperBound)
        #expect(HistoryViewport(start: bounds.upperBound, end: bounds.lowerBound) == nil)
        #expect(HistoryViewport(start: bounds.lowerBound, end: bounds.lowerBound) == nil)
    }

    @Test func longRangeAggregationStillAllowsLinesAndShortRangeKeepsEverySecond() {
        for preset in HistoryRangePreset.allCases {
            let view = HistoryViewport(endingAt: Date(timeIntervalSince1970: 100_000), duration: preset.duration)
            #expect(view.duration / Double(view.bucketSeconds) <= 900)
            #expect(view.maximumGapSeconds >= Double(view.bucketSeconds))
        }
        let day = HistoryViewport(endingAt: Date(timeIntervalSince1970: 100_000), duration: 86_400)
        #expect(day.bucketSeconds == 120)
        #expect(day.maximumGapSeconds == 180)
        #expect(day.zoomed(magnification: 200).bucketSeconds == 1)
    }

    @Test func cursorUsesMeasuredTimeRejectsUnknownAndDoesNotBridgeMissingHours() {
        let known = reading(id: 1, measured: 10, received: 10_000, source: "card-history-v1")
        let unknown = reading(id: 2, measured: nil, received: 15, source: "card-history-v1")
        let legacy = reading(id: 3, measured: nil, received: 20, source: nil)
        let records = [known, unknown, legacy]
        #expect(HistorySelection.nearest(in: records, to: Date(timeIntervalSince1970: 11), maximumDistance: 2)?.id == 1)
        #expect(HistorySelection.nearest(in: records, to: Date(timeIntervalSince1970: 15), maximumDistance: 2) == nil)
        #expect(HistorySelection.nearest(in: records, to: Date(timeIntervalSince1970: 19), maximumDistance: 2)?.id == 3)
        #expect(HistorySelection.nearest(in: records, to: Date(timeIntervalSince1970: 15), maximumDistance: 5)?.id == 1)
        #expect(HistorySelection.nearest(in: records, to: Date(timeIntervalSince1970: 5_000), maximumDistance: 90) == nil)
        #expect(HistorySelection.nominalInterval(in: records) == 10)
    }

    private func reading(id: Int64, measured: TimeInterval?, received: TimeInterval, source: String?) -> StoredReading {
        let reading = SensorReading(sequence: UInt32(id), uptimeMilliseconds: 1_000, enabledMask: 0,
            validMask: 0, sensors: [:], receivedAt: Date(timeIntervalSince1970: received), rawFrame: Data(),
            measuredAt: measured.map { Date(timeIntervalSince1970: $0) }, measurementSource: source)
        return StoredReading(id: id, deviceID: "test", bootID: "history64:1", reading: reading)
    }
}
