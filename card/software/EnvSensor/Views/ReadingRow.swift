import SwiftUI

struct ReadingRow: View {
    let reading: StoredReading
    let deviceName: String
    var metric: SensorMetric = .temperature

    var body: some View {
        HStack(alignment: .firstTextBaseline, spacing: 16) {
            VStack(alignment: .leading, spacing: 5) {
                Text(deviceName)
                    .font(.subheadline.weight(.semibold))
                if let date = reading.reading.chartTimestamp {
                    Text(date, format: .dateTime.month().day().hour().minute().second())
                        .font(.caption).foregroundStyle(.secondary)
                } else {
                    Text("日時未確定").font(.caption).foregroundStyle(.secondary)
                }
            }
            Spacer(minLength: 0)
            VStack(alignment: .trailing, spacing: 5) {
                Text(displayValue)
                    .font(.body.weight(.medium).monospacedDigit())
                Text(metric.title)
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
        }
        .padding(.vertical, 3)
        .accessibilityElement(children: .combine)
        .accessibilityLabel("\(deviceName)、\(metric.title)、\(value == nil ? "未取得" : displayValue)。受信日時 \(reading.reading.receivedAt.formatted(date: .abbreviated, time: .standard))")
    }

    private var value: Double? { metric.value(in: reading.reading) }
    private var displayValue: String {
        value.map { "\(metric.formatted($0)) \(metric.unit)" } ?? "—"
    }
}
