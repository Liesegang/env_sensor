import SwiftUI

struct MetricTileView: View {
    let metric: SensorMetric
    let value: Double?

    var body: some View {
        VStack(alignment: .leading, spacing: 7) {
            Label(metric.title, systemImage: metric.systemImage)
                .font(.caption).foregroundStyle(.secondary)
            ViewThatFits(in: .horizontal) {
                HStack(alignment: .firstTextBaseline, spacing: 5) {
                    Text(metric.formatted(value)).font(.title2.weight(.semibold)).monospacedDigit()
                    Text(metric.unit).font(.caption).foregroundStyle(.secondary)
                }
                VStack(alignment: .leading, spacing: 1) {
                    Text(metric.formatted(value)).font(.title2.weight(.semibold)).monospacedDigit()
                    Text(metric.unit).font(.caption).foregroundStyle(.secondary)
                }
            }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(metric.title)
        .accessibilityValue(value == nil ? "未取得" : "\(metric.formatted(value)) \(metric.unit)")
    }
}
