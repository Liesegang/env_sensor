import SwiftUI

struct CardImageEditorPreview: View {
    let prepared: PreparedCardImage
    let source: CardImageSource
    let updating: Bool
    @Binding var adjustments: CardImageAdjustments
    @State private var size = CGSize.zero
    @State private var dragOrigin: CGSize?
    @State private var zoomOrigin: Double?

    var body: some View {
        Image(decorative: prepared.preview, scale: 1)
            .resizable().interpolation(.none).aspectRatio(1.5, contentMode: .fit)
            .clipShape(.rect(cornerRadius: 12))
            .overlay { RoundedRectangle(cornerRadius: 12).stroke(.secondary.opacity(0.3)) }
            .overlay(alignment: .topTrailing) {
                if updating {
                    ProgressView().padding(8).background(.regularMaterial, in: .capsule)
                        .padding(8).accessibilityLabel("プレビューを更新中")
                }
            }
            .onGeometryChange(for: CGSize.self) { $0.size } action: { size = $0 }
            .contentShape(.rect)
            .gesture(drag)
            .simultaneousGesture(magnify)
            .accessibilityElement(children: .ignore)
            .accessibilityLabel("電子ペーパー用の6色プレビュー")
            .accessibilityHint("位置とズームは下のスライダーでも調整できます。")
    }

    private var drag: some Gesture {
        DragGesture(minimumDistance: 8)
            .onChanged { value in
                if dragOrigin == nil {
                    dragOrigin = CGSize(width: adjustments.horizontalPosition,
                                        height: adjustments.verticalPosition)
                }
                guard let origin = dragOrigin, size.width > 0, size.height > 0 else { return }
                let travel = CardImageConverter.positionTravel(source, adjustments: adjustments)
                let displayScale = size.width / Double(CardImageConverter.logicalWidth)
                if travel.width > 0.5 {
                    adjustments.horizontalPosition = min(1, max(-1,
                        origin.width + value.translation.width / (travel.width * displayScale)))
                }
                if travel.height > 0.5 {
                    adjustments.verticalPosition = min(1, max(-1,
                        origin.height + value.translation.height / (travel.height * displayScale)))
                }
            }
            .onEnded { _ in dragOrigin = nil }
    }

    private var magnify: some Gesture {
        MagnifyGesture()
            .onChanged { value in
                if zoomOrigin == nil { zoomOrigin = adjustments.zoom }
                adjustments.zoom = min(4, max(1, (zoomOrigin ?? 1) * value.magnification))
            }
            .onEnded { _ in zoomOrigin = nil }
    }
}
