import SwiftUI

struct CardImageAdjustmentSlider: View {
    let title: String
    @Binding var value: Double
    let range: ClosedRange<Double>
    var suffix = ""
    var fractionDigits = 2

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack {
                Text(title)
                Spacer()
                Text(value, format: .number.precision(.fractionLength(fractionDigits)))
                    .monospacedDigit().foregroundStyle(.secondary)
                if !suffix.isEmpty { Text(suffix).foregroundStyle(.secondary) }
            }
            Slider(value: $value, in: range) { Text(title) }
                .accessibilityValue(Text(value, format: .number.precision(.fractionLength(fractionDigits))))
        }
    }
}
