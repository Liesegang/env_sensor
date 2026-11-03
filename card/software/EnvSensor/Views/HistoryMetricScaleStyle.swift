import Foundation

extension SensorMetric {
    var axisTitle: String {
        switch self {
        case .voc: "VOC"
        case .nox: "NOx"
        case .formaldehyde: "HCHO"
        default: title
        }
    }

    var minimumPlotSpan: Double {
        switch self {
        case .temperature, .humidity, .pressure: 1
        case .co2, .illuminance, .voc, .nox: 50
        case .formaldehyde: 0.002
        }
    }

    func axisFormatted(_ value: Double?) -> String {
        guard let value else { return "—" }
        if abs(value) >= 10_000 {
            return value.formatted(.number.notation(.compactName).precision(.significantDigits(3)))
        }
        return formatted(value)
    }

    var plotDash: [CGFloat] {
        switch self {
        case .temperature, .illuminance: []
        case .humidity, .pressure: [6, 3]
        case .co2, .voc: [2, 3]
        case .nox, .formaldehyde: [8, 3, 2, 3]
        }
    }
}
