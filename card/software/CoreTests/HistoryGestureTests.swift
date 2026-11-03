import Foundation
import Testing
@testable import EnvSensorCore

struct HistoryGestureTests {
    private let bounds = Date(timeIntervalSince1970: 0)...Date(timeIntervalSince1970: 100000)
    private var base: HistoryViewport { .init(endingAt: Date(timeIntervalSince1970: 10000), duration: 1000) }

    @Test func panAndPinchTogetherKeepTheOriginalTimeUnderTheMovingCentroid() {
        let anchor = base.start.addingTimeInterval(base.duration * 0.25)
        let mixed = base.transformed(magnification: 2, anchorFraction: 0.25, translationFraction: 0.2, within: bounds)
        #expect(mixed.duration == 500)
        #expect(abs(mixed.start.addingTimeInterval(500 * 0.45).timeIntervalSince(anchor)) < 0.000001)
        let pixels = base.transformed(initialCentroidX: 100, currentCentroidX: 180, plotWidth: 400,
            magnification: 2, within: bounds)
        #expect(pixels == mixed)
        let purePan = base.transformed(magnification: 1, anchorFraction: 0.5, translationFraction: 0.2, within: bounds)
        #expect(purePan.start == base.start.addingTimeInterval(-200))
        let purePinch = base.transformed(magnification: 2, anchorFraction: 0.25, translationFraction: 0, within: bounds)
        #expect(purePinch == base.zoomed(magnification: 2, anchor: anchor))
    }

    @Test func reversingAMixedGestureRestoresTheWindowAndRepeatedUpdatesUseTheSameBase() {
        let transformed = base.transformed(magnification: 2, anchorFraction: 0.25, translationFraction: 0.2, within: bounds)
        let reversed = transformed.transformed(magnification: 0.5, anchorFraction: 0.45, translationFraction: -0.2, within: bounds)
        #expect(reversed == base)
        let near = base.transformed(magnification: 1.3, anchorFraction: 0.4, translationFraction: 0.1, within: bounds)
        let far = base.transformed(magnification: 3, anchorFraction: 0.4, translationFraction: 0.3, within: bounds)
        #expect(far != near.transformed(magnification: 3, anchorFraction: 0.4, translationFraction: 0.3, within: bounds))
        #expect(base.transformed(magnification: 1, anchorFraction: 0.4, translationFraction: 0, within: bounds) == base)
    }

    @Test func gestureEdgesClampAndTenSecondDetailDoesNotCollapse() {
        let earliest = base.transformed(magnification: 1, anchorFraction: 0.5, translationFraction: 100, within: bounds)
        let latest = base.transformed(magnification: 1, anchorFraction: 0.5, translationFraction: -100, within: bounds)
        #expect(earliest.start == bounds.lowerBound && latest.end == bounds.upperBound)
        let tiny = base.transformed(magnification: 1e9, anchorFraction: 0.5, translationFraction: 0, within: bounds)
        #expect(tiny.duration == 10 && tiny.bucketSeconds == 1)
        let narrowBounds = Date(timeIntervalSince1970: 9000)...Date(timeIntervalSince1970: 9100)
        let wide = base.transformed(magnification: 0.5, anchorFraction: 0.5, translationFraction: 0, within: narrowBounds)
        #expect(wide.duration == 2000 && wide.end == narrowBounds.upperBound)
        let enormous = base.transformed(magnification: 1e-300, anchorFraction: 0.5, translationFraction: 0, within: bounds)
        #expect(enormous.duration == HistoryViewport.maximumDuration && enormous.end == bounds.upperBound)
    }

    @Test func invalidGestureInputsLeaveTheCommittedWindowUnchanged() {
        for scale in [Double.nan, .infinity, 0, -1] {
            #expect(base.transformed(magnification: scale, anchorFraction: 0.5, translationFraction: 0, within: bounds) == base)
        }
        for anchor in [Double.nan, .infinity, -0.01, 1.01] {
            #expect(base.transformed(magnification: 2, anchorFraction: anchor, translationFraction: 0, within: bounds) == base)
        }
        #expect(base.transformed(magnification: 2, anchorFraction: 0.5, translationFraction: .nan, within: bounds) == base)
        #expect(base.transformed(magnification: 2, anchorFraction: 0.5, translationFraction: Double.greatestFiniteMagnitude, within: bounds) == base)
        #expect(base.transformed(initialCentroidX: 10, currentCentroidX: 20, plotWidth: 0, magnification: 2, within: bounds) == base)
        #expect(base.transformed(initialCentroidX: .infinity, currentCentroidX: 20, plotWidth: 100, magnification: 2, within: bounds) == base)
    }

    @Test func fixedAxesPreserveThePreviousUnitsWhenNewValuesLeaveTheVisibleRange() throws {
        let first = HistoryRenderSeries(key: "co2", samples: samples([600,1200]), minimumSpan: 50)
        let frozen = try #require(first.scale)
        let changed = HistoryRenderSeries(key: "co2", samples: samples([100,600,3000]), minimumSpan: 50, forcedScale: frozen)
        #expect(changed.scale == frozen)
        #expect(changed.points[0].normalizedValue < 0 && changed.points[2].normalizedValue > 1)
        #expect(changed.points.map(\.actualValue) == [100,600,3000])
        #expect(changed.points[1].normalizedValue == first.points[0].normalizedValue)
        #expect(changed.scale?.actualValue(at: 0) == first.scale?.actualValue(at: 0))
        #expect(changed.scale?.actualValue(at: 1) == first.scale?.actualValue(at: 1))
        let automatic = HistoryRenderSeries(key: "co2", samples: samples([100,600,3000]), minimumSpan: 50)
        #expect(automatic.scale != frozen)
        let empty = HistoryRenderSeries(key: "co2", samples: [], minimumSpan: 50, forcedScale: frozen)
        #expect(empty.scale == frozen && empty.points.isEmpty && empty.isolatedPoints.isEmpty)
        let neverMeasured = HistoryRenderSeries(key: "hcho", samples: [], minimumSpan: 0.002)
        #expect(neverMeasured.scale == nil && neverMeasured.points.isEmpty)
    }

    private func samples(_ values: [Double]) -> [HistoryRenderSample] {
        values.enumerated().map { HistoryRenderSample(timestamp: Date(timeIntervalSince1970: Double($0.offset)),
            value: $0.element, segment: 0, isIsolated: false) }
    }
}
