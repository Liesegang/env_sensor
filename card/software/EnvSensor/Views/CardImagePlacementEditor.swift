import SwiftUI

struct CardImagePlacementEditor: View {
    let source: CardImageSource
    let prepared: PreparedCardImage?
    let updating: Bool
    @Binding var adjustments: CardImageAdjustments
    @Environment(\.dismiss) private var dismiss
    @Environment(\.verticalSizeClass) private var verticalSizeClass
    @Environment(\.dynamicTypeSize) private var dynamicTypeSize
    @State private var mode = CardImagePreviewMode.original
    @State private var availableHeight: Double = 0

    var body: some View {
        NavigationStack {
            VStack(spacing: 12) {
                Picker("表示する画像", selection: $mode) {
                    ForEach(CardImagePreviewMode.allCases) { mode in
                        Text(mode.title).tag(mode)
                    }
                }
                .pickerStyle(.segmented).padding(.horizontal)
                Text("写真をドラッグで移動、ピンチで拡大")
                    .font(.subheadline).foregroundStyle(.secondary)
                CardImagePlacementCanvas(source: source, prepared: prepared, mode: mode,
                                         updating: updating, adjustments: $adjustments)
                    .frame(minHeight: compactLayout ? 180 : 240, maxHeight: .infinity)
                    .layoutPriority(1)
                    .padding(.horizontal, 12)
                ScrollView {
                    VStack(alignment: .leading, spacing: 14) {
                        Picker("写真の収め方", selection: $adjustments.framing) {
                            Text("全体を表示").tag(CardImageAdjustments.Framing.fit)
                            Text("トリミング").tag(CardImageAdjustments.Framing.crop)
                        }
                        .pickerStyle(.segmented)
                        HStack(spacing: 12) {
                            Button("左90°", systemImage: "rotate.left") { adjustments.rotate(by: -90) }
                            Button("右90°", systemImage: "rotate.right") { adjustments.rotate(by: 90) }
                            Spacer(minLength: 0)
                        }
                        .buttonStyle(.bordered)
                        CardImageAdjustmentSlider(title: "倍率", value: $adjustments.zoom,
                                                  range: 1...4, suffix: "倍", fractionDigits: 2)
                        DisclosureGroup("位置・角度を細かく調整") {
                            VStack(spacing: 14) {
                                CardImageAdjustmentSlider(title: "左右の位置", value: $adjustments.horizontalPosition,
                                                          range: -1...1)
                                CardImageAdjustmentSlider(title: "上下の位置", value: $adjustments.verticalPosition,
                                                          range: -1...1)
                                CardImageAdjustmentSlider(title: "回転角度", value: $adjustments.rotationDegrees,
                                                          range: 0...360, suffix: "°", fractionDigits: 0)
                            }
                            .padding(.top, 12)
                        }
                        Button("配置をリセット", systemImage: "arrow.counterclockwise", action: resetPlacement)
                            .buttonStyle(.bordered)
                        Text("枠の比率は3:2で固定です。「全体を表示」は余白付き、「トリミング」は枠いっぱいに切り抜きます。")
                            .font(.footnote).foregroundStyle(.secondary)
                    }
                    .padding(.horizontal).padding(.bottom)
                }
                .frame(maxHeight: compactLayout ? 180 : 230)
            }
            .frame(maxHeight: .infinity)
            .onGeometryChange(for: Double.self) { Double($0.size.height) } action: { availableHeight = $0 }
            .navigationTitle("配置・トリミング")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) { Button("配置を完了") { dismiss() } }
            }
            .onChange(of: adjustments.framing) {
                adjustments.zoom = 1
                adjustments.horizontalPosition = 0
                adjustments.verticalPosition = 0
            }
        }
    }

    private var compactLayout: Bool {
        verticalSizeClass == .compact || dynamicTypeSize.isAccessibilitySize ||
            (availableHeight > 0 && availableHeight < 640)
    }

    private func resetPlacement() {
        adjustments.framing = .fit
        adjustments.rotationDegrees = 0
        adjustments.zoom = 1
        adjustments.horizontalPosition = 0
        adjustments.verticalPosition = 0
    }
}
