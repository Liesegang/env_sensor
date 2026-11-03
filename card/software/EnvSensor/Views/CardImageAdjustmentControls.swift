import SwiftUI

struct CardImageAdjustmentControls: View {
    @Binding var adjustments: CardImageAdjustments

    var body: some View {
        VStack(alignment: .leading, spacing: 18) {
            Text("色の調整").font(.headline)
            Toggle("自動の明暗補正", isOn: $adjustments.autoEnhance)
            CardImageAdjustmentSlider(title: "発色", value: $adjustments.colorStrength,
                                      range: 0...2)
            CardImageAdjustmentSlider(title: "明るさ", value: $adjustments.brightness,
                                      range: -0.5...0.5)
            CardImageAdjustmentSlider(title: "コントラスト", value: $adjustments.contrast,
                                      range: 0.5...1.5)
            CardImageAdjustmentSlider(title: "彩度", value: $adjustments.saturation,
                                      range: 0...2)
            Button("色をリセット", systemImage: "arrow.counterclockwise") {
                adjustments.brightness = 0
                adjustments.contrast = 1
                adjustments.saturation = 1
                adjustments.autoEnhance = true
                adjustments.colorStrength = 1
            }
            .buttonStyle(.bordered)
        }
    }
}
