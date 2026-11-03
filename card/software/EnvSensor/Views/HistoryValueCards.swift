import SwiftUI

struct HistoryValueCards: View {
    let metrics: [SensorMetric]
    let sample: StoredReading?
    let selectedTime: Date?
    let onClear: () -> Void
    @Environment(\.dynamicTypeSize) private var dynamicTypeSize

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack {
                VStack(alignment: .leading, spacing: 3) {
                    Text(selectedTime == nil ? "期間内の最新値" : "選択した時刻の値")
                        .font(.headline).lineLimit(2, reservesSpace: true)
                    Text(displayDate ?? Date(timeIntervalSince1970: 0),
                         format: .dateTime.month().day().hour().minute().second())
                        .font(.footnote.monospacedDigit()).foregroundStyle(.secondary)
                        .lineLimit(1)
                        .opacity(displayDate == nil ? 0 : 1)
                        .accessibilityHidden(displayDate == nil)
                }
                Spacer()
                Button(action: onClear) {
                    Label("選択を解除", systemImage: "xmark.circle")
                        .labelStyle(.iconOnly).frame(width: 44, height: 44)
                }
                .opacity(selectedTime == nil ? 0 : 1)
                .disabled(selectedTime == nil)
                .accessibilityHidden(selectedTime == nil)
            }
            // Keep the exact same text and its Dynamic Type height while a
            // cursor crosses gaps. Removing it moves every chart under a drag.
            Text("この時刻の近くに測定がありません。")
                .font(.footnote).foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
                .opacity(isMissingSelection ? 1 : 0)
                .accessibilityHidden(!isMissingSelection)
            LazyVGrid(columns: [GridItem(.adaptive(minimum: dynamicTypeSize.isAccessibilitySize ? 240 : 140))], spacing: 10) {
                ForEach(metrics) { metric in
                    VStack(alignment: .leading, spacing: 8) {
                        Label(metric.title, systemImage: metric.systemImage)
                            .font(.subheadline).foregroundStyle(metric.color)
                        HStack(alignment: .firstTextBaseline, spacing: 4) {
                            Text(metric.formatted(sample.flatMap { metric.value(in: $0.reading) }))
                                .font(.title2.bold().monospacedDigit())
                                .lineLimit(1)
                            Text(metric.unit).font(.footnote).foregroundStyle(.secondary).lineLimit(1)
                        }
                        .frame(maxWidth: .infinity, alignment: .leading)
                    }
                    .padding(12)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .background(metric.color.opacity(0.07), in: .rect(cornerRadius: 12))
                    .accessibilityElement(children: .combine)
                }
            }
        }
    }

    private var displayDate: Date? { sample?.reading.chartTimestamp ?? selectedTime }
    private var isMissingSelection: Bool { sample == nil && selectedTime != nil }
}
