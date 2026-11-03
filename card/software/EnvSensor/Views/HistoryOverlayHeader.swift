import SwiftUI

struct HistoryOverlayHeader: View {
    let selectedTime: Date?
    let sampleTime: Date?

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text("重ねたグラフ").font(.headline)
            ZStack(alignment: .leading) {
                Text(sampleTime ?? Date(timeIntervalSince1970: 0),
                     format: .dateTime.month().day().hour().minute().second())
                    .monospacedDigit()
                    .opacity(showsTimestamp ? 1 : 0)
                    .accessibilityHidden(!showsTimestamp)
                Text("選択時刻付近は欠測")
                    .opacity(showsMissing ? 1 : 0)
                    .accessibilityHidden(!showsMissing)
            }
            .font(.footnote).foregroundStyle(.secondary).lineLimit(1)
        }
    }

    private var showsTimestamp: Bool { selectedTime != nil && sampleTime != nil }
    private var showsMissing: Bool { selectedTime != nil && sampleTime == nil }
}
