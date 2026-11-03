import Foundation

enum CardImagePreviewMode: String, CaseIterable, Identifiable {
    case original
    case result

    var id: Self { self }
    var title: String { self == .original ? "元の写真" : "6色の結果" }
}
