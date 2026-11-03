import SwiftUI

struct CardImagePlacementCanvas: View {
    let source: CardImageSource
    let prepared: PreparedCardImage?
    let mode: CardImagePreviewMode
    let updating: Bool
    @Binding var adjustments: CardImageAdjustments
    @State private var size = CGSize.zero
    @GestureState private var dragOrigin: CGSize?
    @GestureState private var zoomOrigin: Double?

    var body: some View {
        GeometryReader { geometry in
            let output = outputRect(in: geometry.size)
            let scale = output.width / Double(CardImageConverter.logicalWidth)
            let layout = CardImageConverter.placement(source, adjustments: adjustments)
            ZStack {
                Color.black.opacity(0.88)
                Rectangle().fill(.white)
                    .frame(width: output.width, height: output.height)
                    .position(x: output.midX, y: output.midY)
                Image(decorative: source.image, scale: 1)
                    .resizable()
                    .frame(width: Double(source.image.width) * layout.scale * scale,
                           height: Double(source.image.height) * layout.scale * scale)
                    .rotationEffect(.radians(layout.angle))
                    .offset(x: layout.offset.width * scale, y: layout.offset.height * scale)
                    .position(x: output.midX, y: output.midY)
                if mode == .result, let prepared {
                    Image(decorative: prepared.preview, scale: 1)
                        .resizable().interpolation(.none)
                        .frame(width: output.width, height: output.height)
                        .position(x: output.midX, y: output.midY)
                        .opacity(updating ? 0.55 : 1)
                }
                CardImageCropMask(output: output)
                    .fill(.black.opacity(0.55), style: FillStyle(eoFill: true))
                CardImageCropGrid()
                    .stroke(.white.opacity(0.7), lineWidth: 1)
                    .frame(width: output.width, height: output.height)
                    .position(x: output.midX, y: output.midY)
                Rectangle().stroke(.white, lineWidth: 2)
                    .frame(width: output.width, height: output.height)
                    .position(x: output.midX, y: output.midY)
            }
            .clipped()
            .overlay(alignment: .topLeading) {
                Text("白い枠の中が表示されます")
                    .font(.caption).foregroundStyle(.white)
                    .padding(10).background(.black.opacity(0.6), in: .capsule)
                    .padding(12)
            }
            .overlay(alignment: .bottomTrailing) {
                if mode == .result, updating {
                    ProgressView("6色に変換中")
                        .font(.caption).padding(10)
                        .background(.regularMaterial, in: .capsule)
                        .padding(12)
                }
            }
            .contentShape(.rect)
            .gesture(drag)
            .simultaneousGesture(magnify)
        }
        .onGeometryChange(for: CGSize.self) { $0.size } action: { size = $0 }
        .clipShape(.rect(cornerRadius: 16))
        .accessibilityElement(children: .ignore)
        .accessibilityLabel("写真の配置領域。表示範囲は横600、縦400です。")
        .accessibilityHint("指でドラッグして移動、ピンチで拡大できます。位置のスライダーでも調整できます。")
    }

    private func outputRect(in size: CGSize) -> CGRect {
        let width = max(1, min(size.width - 32, (size.height - 64) * 1.5))
        let height = width / 1.5
        return CGRect(x: (size.width - width) / 2, y: (size.height - height) / 2,
                      width: width, height: height)
    }

    private var drag: some Gesture {
        DragGesture(minimumDistance: 4)
            .updating($dragOrigin) { _, origin, _ in
                if origin == nil {
                    origin = CGSize(width: adjustments.horizontalPosition,
                                    height: adjustments.verticalPosition)
                }
            }
            .onChanged { value in
                guard let origin = dragOrigin, size.width > 0, size.height > 0 else { return }
                let travel = CardImageConverter.placement(source, adjustments: adjustments).travel
                let scale = outputRect(in: size).width / Double(CardImageConverter.logicalWidth)
                if travel.width > 0.5 {
                    adjustments.horizontalPosition = min(1, max(-1,
                        origin.width + value.translation.width / (travel.width * scale)))
                }
                if travel.height > 0.5 {
                    adjustments.verticalPosition = min(1, max(-1,
                        origin.height + value.translation.height / (travel.height * scale)))
                }
            }
    }

    private var magnify: some Gesture {
        MagnifyGesture()
            .updating($zoomOrigin) { _, origin, _ in
                if origin == nil { origin = adjustments.zoom }
            }
            .onChanged { value in
                adjustments.zoom = min(4, max(1, (zoomOrigin ?? 1) * value.magnification))
            }
    }
}
