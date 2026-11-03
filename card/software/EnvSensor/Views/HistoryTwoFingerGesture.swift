import SwiftUI
import UIKit

struct HistoryTwoFingerGesture: UIGestureRecognizerRepresentable {
    let plotWidth: CGFloat
    let onTransform: (HistoryPlotTransform) -> Void

    func makeCoordinator(converter: CoordinateSpaceConverter) -> Coordinator { Coordinator() }

    func makeUIGestureRecognizer(context: Context) -> HistoryTwoFingerRecognizer {
        let recognizer = HistoryTwoFingerRecognizer()
        recognizer.cancelsTouchesInView = true
        recognizer.delaysTouchesBegan = false
        recognizer.allowedTouchTypes = [NSNumber(value: UITouch.TouchType.direct.rawValue)]
        return recognizer
    }

    func handleUIGestureRecognizerAction(_ recognizer: HistoryTwoFingerRecognizer, context: Context) {
        let phase: HistoryPlotTransform.Phase
        switch recognizer.state {
        case .began:
            context.coordinator.initialX = context.converter.localLocation.x
            context.coordinator.width = plotWidth
            phase = .began
        case .changed: phase = .changed
        case .ended: phase = .ended
        case .cancelled, .failed: phase = .cancelled
        default: return
        }
        guard let initialX = context.coordinator.initialX else { return }
        onTransform(HistoryPlotTransform(phase: phase, initialCentroidX: Double(initialX),
            currentCentroidX: Double(context.converter.localLocation.x),
            plotWidth: Double(context.coordinator.width), magnification: Double(recognizer.magnification)))
        if phase == .ended || phase == .cancelled { context.coordinator.initialX = nil }
    }

    final class Coordinator {
        var initialX: CGFloat?
        var width: CGFloat = 0
    }
}
