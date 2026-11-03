import Foundation
import Testing
@testable import EnvSensorCore

struct ImageUploadQueueTests {
    private let frame = Data(repeating: 0x11, count: 120_000)

    @Test func minuteGapRequestWaitsForTheSameAuthenticatedForegroundConnection() throws {
        var queue = CardImageUploadQueue()
        try queue.enqueue(frame, deviceID: "card")
        let wrongDevice = queue.take(for: "outdoor", authenticated: true, foreground: true, fenceReady: true)
        #expect(wrongDevice == nil && queue.isWaiting)
        let unauthenticated = queue.take(for: "card", authenticated: false, foreground: true, fenceReady: true)
        #expect(unauthenticated == nil && queue.isWaiting)
        let background = queue.take(for: "card", authenticated: true, foreground: false, fenceReady: true)
        #expect(background == nil && queue.isWaiting)
        let connected = queue.take(for: "card", authenticated: true, foreground: true, fenceReady: true)
        #expect(connected == frame)
        #expect(!queue.isWaiting)
        let duplicateCallback = queue.take(for: "card", authenticated: true, foreground: true, fenceReady: true)
        #expect(duplicateCallback == nil)
    }

    @Test func quickRequeueCannotBypassCanceledWriteFenceEvenAfterBothACKs() throws {
        var fence = CardImageWriteFence()
        fence.stop(outstanding: "data", abort: "control")
        var queue = CardImageUploadQueue()
        try queue.enqueue(frame, deviceID: "card")
        let stillStopping = queue.take(for: "card", authenticated: true, foreground: true, fenceReady: fence.canStart)
        #expect(stillStopping == nil && queue.isWaiting)
        _ = fence.acknowledge("data")
        _ = fence.acknowledge("control")
        let afterACKs = queue.take(for: "card", authenticated: true, foreground: true, fenceReady: fence.canStart)
        #expect(afterACKs == nil && queue.isWaiting)
        fence.disconnected()
        let freshConnection = queue.take(for: "card", authenticated: true, foreground: true, fenceReady: fence.canStart)
        #expect(freshConnection == frame)
    }

    @Test func cancelAndBackgroundDropRequestRatherThanStartingItOnNextMinute() throws {
        var queue = CardImageUploadQueue()
        try queue.enqueue(frame, deviceID: "card")
        queue.cancel()
        let canceled = queue.take(for: "card", authenticated: true, foreground: true, fenceReady: true)
        #expect(canceled == nil)
        try queue.enqueue(frame, deviceID: "card")
        queue.cancel() // Collector's background, collection-off, and bond-error paths.
        let reopened = queue.take(for: "card", authenticated: true, foreground: true, fenceReady: true)
        #expect(reopened == nil && queue.deviceID == nil)
    }
}
