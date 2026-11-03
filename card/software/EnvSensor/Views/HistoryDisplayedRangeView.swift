import SwiftUI

struct HistoryDisplayedRangeView: View {
    let range: ClosedRange<Date>

    var body: some View {
        Text(Self.compactDescription(range))
            .font(.footnote.monospacedDigit()).foregroundStyle(.secondary)
            .lineLimit(1).frame(maxWidth: .infinity, alignment: .leading)
            .accessibilityLabel(Self.description(range))
    }

    static func description(_ range: ClosedRange<Date>) -> String {
        let format = Date.FormatStyle.dateTime.year().month().day().hour().minute().second()
        return "表示期間：\(range.lowerBound.formatted(format)) 〜 \(range.upperBound.formatted(format))"
    }

    private static func compactDescription(_ range: ClosedRange<Date>) -> String {
        let format = Date.FormatStyle.dateTime.month(.twoDigits).day(.twoDigits)
            .hour().minute().second().locale(Locale(identifier: "ja_JP"))
        return "\(range.lowerBound.formatted(format)) 〜 \(range.upperBound.formatted(format))"
    }
}
