import SwiftUI

struct CardImageResultPreview: View {
    let prepared: PreparedCardImage
    let updating: Bool

    var body: some View {
        Image(decorative: prepared.preview, scale: 1)
            .resizable().interpolation(.none).aspectRatio(1.5, contentMode: .fit)
            .clipShape(.rect(cornerRadius: 12))
            .overlay { RoundedRectangle(cornerRadius: 12).stroke(.secondary.opacity(0.3)) }
            .overlay(alignment: .topTrailing) {
                if updating {
                    ProgressView("更新中")
                        .font(.caption).padding(8).background(.regularMaterial, in: .capsule)
                        .padding(8)
                }
            }
            .opacity(updating ? 0.65 : 1)
            .accessibilityElement(children: .ignore)
            .accessibilityLabel(updating ? "電子ペーパー用の6色プレビューを更新中" : "電子ペーパー用の6色の仕上がり")
    }
}
