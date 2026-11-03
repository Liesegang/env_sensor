import SwiftUI
import UIKit

/// A vertical touch fails before recognition, leaving ScrollView free to scroll.
struct HistorySingleFingerGesture: UIGestureRecognizerRepresentable {
    let isTransformActive: () -> Bool
    let onSelect: (CGFloat) -> Void

    func makeCoordinator(converter: CoordinateSpaceConverter) -> Coordinator {
        Coordinator(converter: converter, isTransformActive: isTransformActive)
    }

    func makeUIGestureRecognizer(context: Context) -> UIPanGestureRecognizer {
        let recognizer = UIPanGestureRecognizer()
        recognizer.minimumNumberOfTouches = 1
        recognizer.maximumNumberOfTouches = 1
        recognizer.delaysTouchesBegan = false
        recognizer.delegate = context.coordinator
        recognizer.allowedTouchTypes = [NSNumber(value: UITouch.TouchType.direct.rawValue)]
        return recognizer
    }

    func updateUIGestureRecognizer(_ recognizer: UIPanGestureRecognizer, context: Context) {
        context.coordinator.converter = context.converter
        context.coordinator.isTransformActive = isTransformActive
    }

    func handleUIGestureRecognizerAction(_ recognizer: UIPanGestureRecognizer, context: Context) {
        guard !isTransformActive() else { return }
        switch recognizer.state {
        case .began, .changed, .ended: onSelect(context.converter.localLocation.x)
        default: break
        }
    }

    final class Coordinator: NSObject, UIGestureRecognizerDelegate {
        var converter: CoordinateSpaceConverter
        var isTransformActive: () -> Bool

        init(converter: CoordinateSpaceConverter, isTransformActive: @escaping () -> Bool) {
            self.converter = converter
            self.isTransformActive = isTransformActive
        }

        func gestureRecognizerShouldBegin(_ gestureRecognizer: UIGestureRecognizer) -> Bool {
            guard !isTransformActive(), let pan = gestureRecognizer as? UIPanGestureRecognizer else { return false }
            let delta = converter.localTranslation ?? pan.translation(in: nil)
            return abs(delta.x) > abs(delta.y)
        }
    }
}
