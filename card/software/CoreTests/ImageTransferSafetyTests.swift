import Foundation
import Testing
@testable import EnvSensorCore

struct ImageTransferSafetyTests {
    @Test func cancelDuringControlWriteCannotStartUntilDisconnectionEvenAfterBothACKs() {
        var fence = CardImageWriteFence()
        #expect(fence.canStart)
        fence.stop(outstanding: "control", abort: "control")
        #expect(!fence.canStart)
        #expect(fence.pending["control"] == 2)
        let oldBegin = fence.acknowledge("control")
        #expect(oldBegin)
        #expect(!fence.canStart)
        let abort = fence.acknowledge("control")
        #expect(abort) // Worker may still be busy after accepting ABORT.
        #expect(fence.pending.isEmpty)
        #expect(!fence.canStart)
        fence.disconnected()
        #expect(fence.canStart)
        #expect(fence.generation == 1)
        let newGeneration = fence.acknowledge("control")
        #expect(!newGeneration)
    }

    @Test func cancelDuringDataWriteDrainsBothCharacteristicsAndDropsLostACKOnDisconnect() {
        var fence = CardImageWriteFence()
        fence.stop(outstanding: "data", abort: "control")
        let oldData = fence.acknowledge("data")
        #expect(oldData)
        #expect(fence.pending == ["control": 1])
        #expect(!fence.canStart)
        // A dropped ABORT response must not leave the next connection blocked.
        fence.disconnected()
        #expect(fence.canStart && fence.pending.isEmpty)
        fence.stop(outstanding: "data", abort: "control")
        #expect(!fence.canStart)
        fence.disconnected()
        #expect(fence.generation == 2)
    }

    @Test func commitSubmissionDisablesAbortBeforeACKAndStaysIrreversibleAfterDisconnect() {
        var boundary = CardImageCommitBoundary()
        #expect(boundary.canCancel)
        #expect(boundary.abort(id: 7) == Data([0x49, 0x43, 1, 3, 7, 0, 0, 0]))
        boundary.submit()
        #expect(!boundary.canCancel)
        #expect(boundary.abort(id: 7) == nil)
        var fence = CardImageWriteFence()
        fence.stop(outstanding: "control", abort: nil)
        fence.disconnected()
        #expect(fence.canStart)
        #expect(boundary.abort(id: 7) == nil)
        // Only an explicitly begun new transfer permits ABORT again.
        boundary.begin()
        #expect(boundary.canCancel)
    }
}
