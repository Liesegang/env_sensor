import Foundation

/// Core Bluetooth's write callback identifies the characteristic, not the
/// command's transfer ID. After cancellation, a fresh connection is required
/// before starting another operation; old ATT callbacks are drained meanwhile.
struct CardImageWriteFence: Sendable {
    private(set) var generation = 0
    private(set) var isStopping = false
    private(set) var pending: [String: Int] = [:]
    var canStart: Bool { !isStopping && pending.isEmpty }

    mutating func stop(outstanding: String?, abort: String?) {
        isStopping = true
        if let outstanding { pending[outstanding, default: 0] += 1 }
        if let abort { pending[abort, default: 0] += 1 }
    }

    /// Returns true for callbacks belonging to the stopped generation.
    mutating func acknowledge(_ characteristic: String) -> Bool {
        guard isStopping else { return false }
        if let count = pending[characteristic] {
            if count == 1 { pending.removeValue(forKey: characteristic) }
            else { pending[characteristic] = count - 1 }
        }
        return true
    }

    mutating func disconnected() {
        pending.removeAll()
        isStopping = false
        generation += 1
    }
}
