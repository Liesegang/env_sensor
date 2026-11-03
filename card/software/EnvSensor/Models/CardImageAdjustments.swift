import Foundation

public struct CardImageAdjustments: Sendable, Equatable {
    public enum Framing: String, CaseIterable, Sendable, Identifiable {
        case fit
        case crop

        public var id: Self { self }
        public var title: String { self == .fit ? "全体" : "トリミング" }
    }

    public var framing: Framing = .fit
    /// Positive angles rotate clockwise in the landscape preview.
    public var rotationDegrees: Double = 0
    public var zoom: Double = 1
    /// Position in the available horizontal/vertical travel, from -1 to 1.
    public var horizontalPosition: Double = 0
    public var verticalPosition: Double = 0
    public var brightness: Double = 0
    public var contrast: Double = 1
    public var saturation: Double = 1
    public var autoEnhance = true
    /// 0 disables vibrance; 1 is the default, 2 emphasizes muted colors further.
    public var colorStrength: Double = 1

    public init() {}

    public mutating func rotate(by degrees: Double) {
        rotationDegrees = (rotationDegrees + degrees).truncatingRemainder(dividingBy: 360)
        if rotationDegrees < 0 { rotationDegrees += 360 }
        horizontalPosition = 0
        verticalPosition = 0
    }

    public var normalized: Self {
        var result = self
        result.rotationDegrees = finite(rotationDegrees, fallback: 0).truncatingRemainder(dividingBy: 360)
        result.zoom = min(4, max(1, finite(zoom, fallback: 1)))
        result.horizontalPosition = min(1, max(-1, finite(horizontalPosition, fallback: 0)))
        result.verticalPosition = min(1, max(-1, finite(verticalPosition, fallback: 0)))
        result.brightness = min(0.5, max(-0.5, finite(brightness, fallback: 0)))
        result.contrast = min(1.5, max(0.5, finite(contrast, fallback: 1)))
        result.saturation = min(2, max(0, finite(saturation, fallback: 1)))
        result.colorStrength = min(2, max(0, finite(colorStrength, fallback: 1)))
        return result
    }

    private func finite(_ value: Double, fallback: Double) -> Double {
        value.isFinite ? value : fallback
    }
}
