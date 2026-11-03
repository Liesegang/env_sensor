import SwiftUI

/// Even-odd fill leaves the output rectangle clear and dims everything outside.
struct CardImageCropMask: Shape {
    let output: CGRect

    func path(in rect: CGRect) -> Path {
        var path = Path(rect)
        path.addRect(output)
        return path
    }
}
