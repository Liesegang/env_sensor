import Foundation

/// Holds one foreground request through a normal minute communication gap.
/// A connection alone is insufficient: its complete sensor Read must have
/// authenticated successfully, and a cancelled older write generation must end.
struct CardImageUploadQueue: Sendable {
    private(set) var deviceID: String?
    private(set) var frame: Data?
    var isWaiting: Bool { frame != nil }

    mutating func enqueue(_ frame: Data, deviceID: String) throws {
        try CardImageProtocol.validateFrame(frame)
        self.deviceID = deviceID
        self.frame = frame
    }

    mutating func take(for deviceID: String, authenticated: Bool,
                       foreground: Bool, fenceReady: Bool) -> Data? {
        guard self.deviceID == deviceID, authenticated, foreground, fenceReady, let frame else { return nil }
        cancel()
        return frame
    }

    mutating func cancel() { deviceID = nil; frame = nil }
}
