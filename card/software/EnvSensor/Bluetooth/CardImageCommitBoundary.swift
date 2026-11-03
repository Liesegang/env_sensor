import Foundation

/// Treat an in-flight COMMIT as irreversible: ATT can already have accepted it
/// while its callback is still queued on iOS. Disconnection must not roll back
/// that command, and the UI must not offer an ABORT for it.
struct CardImageCommitBoundary: Sendable {
    private(set) var submitted = false
    var canCancel: Bool { !submitted }

    mutating func begin() { submitted = false }
    mutating func submit() { submitted = true }
    func abort(id: UInt32) -> Data? { submitted ? nil : CardImageProtocol.abort(id: id) }
}
