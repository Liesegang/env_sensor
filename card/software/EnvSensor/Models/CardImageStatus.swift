import Foundation

public struct CardImageStatus: Sendable, Equatable {
    public let state: UInt8
    public let id: UInt32
    public let received: UInt32
    public let total: UInt32
    public let crc: UInt32
    public let error: Int32

    public init(packet: Data) throws {
        let bytes = [UInt8](packet)
        guard bytes.count == 24, bytes[0...2] == [0x49, 0x53, 1], bytes[3] <= 7 else {
            throw CardImageError.invalidStatus
        }
        func u32(_ offset: Int) -> UInt32 {
            (0..<4).reduce(0) { $0 | UInt32(bytes[offset + $1]) << ($1 * 8) }
        }
        state = bytes[3]
        id = u32(4)
        received = u32(8)
        total = u32(12)
        crc = u32(16)
        error = Int32(bitPattern: u32(20))
        guard received <= total else { throw CardImageError.invalidStatus }
        guard state == 3 ? error < 0 : error == 0 else { throw CardImageError.invalidStatus }
    }
}
