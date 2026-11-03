import SwiftUI

struct HistoryOverlayLegend: View {
    let metrics: [SensorMetric]
    let scales: [SensorMetric: HistoryValueScale]
    let highlightedSample: StoredReading?

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            ForEach(metrics) { metric in
                VStack(alignment: .leading, spacing: 3) {
                    HStack {
                        Label(metric.title, systemImage: metric.systemImage)
                            .foregroundStyle(metric.color)
                            .lineLimit(1)
                        Spacer()
                        Text("\(metric.formatted(highlightedSample.flatMap { metric.value(in: $0.reading) })) \(metric.unit)")
                            .monospacedDigit().lineLimit(1)
                    }
                    .font(.subheadline)
                    Text(scales[metric].map { "範囲：\(metric.formatted($0.lowerBound))〜\(metric.formatted($0.upperBound)) \(metric.unit)" } ?? "範囲：記録なし")
                        .font(.footnote.monospacedDigit()).foregroundStyle(.secondary).lineLimit(1)
                }
                .accessibilityElement(children: .combine)
            }
        }
    }
}
