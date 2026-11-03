import UIKit
import UIKit.UIGestureRecognizerSubclass

/// One recognizer measures translation and distance from the same two touches.
final class HistoryTwoFingerRecognizer: UIGestureRecognizer {
    private var tracked: Set<UITouch> = []
    private var centroid = CGPoint.zero
    private var initialDistance: CGFloat = 1
    private(set) var magnification: CGFloat = 1

    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent) {
        tracked.formUnion(touches)
        guard tracked.count <= 2 else { state = state == .possible ? .failed : .cancelled; return }
        guard tracked.count == 2, state == .possible else { return }
        updateGeometry()
        initialDistance = max(1, distance)
        magnification = 1
        state = .began
    }

    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent) {
        guard tracked.count == 2, state == .began || state == .changed else { return }
        updateGeometry()
        magnification = distance / initialDistance
        state = .changed
    }

    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent) {
        if state == .began || state == .changed {
            updateGeometry()
            magnification = distance / initialDistance
            state = .ended
        } else if state == .possible { state = .failed }
        tracked.subtract(touches)
    }

    override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent) {
        state = state == .possible ? .failed : .cancelled
        tracked.subtract(touches)
    }

    override func reset() {
        super.reset()
        tracked.removeAll(keepingCapacity: true)
        magnification = 1
        initialDistance = 1
    }

    override func location(in view: UIView?) -> CGPoint {
        view?.convert(centroid, from: nil) ?? centroid
    }

    // A one-finger scroll/cursor may already be possible when the second
    // finger arrives. It must not exclude the two-finger recognizer.
    override func canBePrevented(by preventingGestureRecognizer: UIGestureRecognizer) -> Bool { false }

    private var distance: CGFloat {
        let points = tracked.map { $0.location(in: nil) }
        guard points.count == 2 else { return initialDistance }
        return hypot(points[1].x - points[0].x, points[1].y - points[0].y)
    }

    private func updateGeometry() {
        let points = tracked.map { $0.location(in: nil) }
        guard points.count == 2 else { return }
        centroid = CGPoint(x: (points[0].x + points[1].x) / 2,
                           y: (points[0].y + points[1].y) / 2)
    }
}
