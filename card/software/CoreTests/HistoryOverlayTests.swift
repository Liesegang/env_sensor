import Foundation
import Testing
@testable import EnvSensorCore

struct HistoryOverlayTests {
    @Test func threeUnitsUseIndependentAxesAndRoundTripToRealValues() throws {
        let temperature = try #require(HistoryValueScale(values: [20, 25, 30]))
        let humidity = try #require(HistoryValueScale(values: [40, 50, 60]))
        let co2 = try #require(HistoryValueScale(values: [600, 900, 1_200], minimumSpan: 50))
        for (temp, rh, ppm) in [(20.0, 40.0, 600.0), (25, 50, 900), (30, 60, 1_200)] {
            let fraction = try #require(temperature.normalized(temp))
            #expect(abs(fraction - (humidity.normalized(rh) ?? .nan)) < 0.0000001)
            #expect(abs(fraction - (co2.normalized(ppm) ?? .nan)) < 0.0000001)
            #expect(abs((temperature.actualValue(at: fraction) ?? .nan) - temp) < 0.0000001)
            #expect(abs((humidity.actualValue(at: fraction) ?? .nan) - rh) < 0.0000001)
            #expect(abs((co2.actualValue(at: fraction) ?? .nan) - ppm) < 0.0000001)
        }
        #expect((temperature.normalized(20) ?? 0) > 0)
        #expect((temperature.normalized(30) ?? 1) < 1)
        #expect(co2.actualValue(at: 0.5) == 900)
    }

    @Test func flatLineAndTinyConcentrationsRemainFiniteAndVisible() throws {
        let flat = try #require(HistoryValueScale(values: [0, 0, 0]))
        #expect(flat.lowerBound < 0 && flat.upperBound > 0)
        #expect(flat.normalized(0) == 0.5)
        let concentration = try #require(HistoryValueScale(values: [0.000004, 0.000009], minimumSpan: 0.000001))
        let position = try #require(concentration.normalized(0.000007))
        #expect(position > 0 && position < 1)
        #expect(abs((concentration.actualValue(at: position) ?? .nan) - 0.000007) < 0.000000000001)
        #expect(HistoryValueScale(values: []) == nil)
        #expect(HistoryValueScale(values: [.nan, .infinity, -.infinity]) == nil)
        #expect(HistoryValueScale(values: [-Double.greatestFiniteMagnitude, Double.greatestFiniteMagnitude]) == nil)
        #expect(flat.normalized(.nan) == nil)
        #expect(flat.actualValue(at: .infinity) == nil)
    }

    @Test func cursorKeepsOriginalMeasurementsAndCommonTimestampAfterScaling() throws {
        let first = sample(id: 1, measured: 100, temperature: 20_000, humidity: 40_000, co2: 600)
        let last = sample(id: 2, measured: 101, temperature: 30_000, humidity: 60_000, co2: 1_200)
        let chosen = try #require(HistorySelection.nearest(in: [first, last],
            to: Date(timeIntervalSince1970: 101), maximumDistance: 1))
        #expect(chosen.id == 2)
        #expect(chosen.reading.chartTimestamp == Date(timeIntervalSince1970: 101))
        let actual = try #require(chosen.reading.value(sensor: "stcc4", field: "co2_ppm"))
        let scale = try #require(HistoryValueScale(values: [600, 1_200]))
        let position = try #require(scale.normalized(actual))
        #expect(actual == 1_200)
        #expect(abs((scale.actualValue(at: position) ?? .nan) - actual) < 0.0000001)
        #expect(chosen.reading.value(sensor: "stcc4", field: "temperature_mc", scale: 0.001) == 30)
        #expect(chosen.reading.value(sensor: "stcc4", field: "humidity_mpercent", scale: 0.001) == 60)
        #expect(chosen.reading.receivedAt == Date(timeIntervalSince1970: 10_000))
    }

    private func sample(id: Int64, measured: TimeInterval, temperature: Int64, humidity: Int64, co2: Int64) -> StoredReading {
        let reading = SensorReading(sequence: UInt32(id), uptimeMilliseconds: 1_000, enabledMask: 8,
            validMask: 8, sensors: ["stcc4": SensorBlock(id: 4, error: 0,
                values: ["temperature_mc": .integer(temperature), "humidity_mpercent": .integer(humidity), "co2_ppm": .integer(co2)])],
            receivedAt: Date(timeIntervalSince1970: 10_000), rawFrame: Data(),
            measuredAt: Date(timeIntervalSince1970: measured), measurementSource: "card-history-v1")
        return StoredReading(id: id, deviceID: "test", bootID: "history64:1", reading: reading)
    }
}
