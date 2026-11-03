import Foundation

public enum CardImageProtocol {
    public static let serviceUUID = "722acd90-e77f-468d-a916-67e599e7cffd"
    public static let controlUUID = "722acd91-e77f-468d-a916-67e599e7cffd"
    public static let dataUUID = "722acd92-e77f-468d-a916-67e599e7cffd"
    public static let statusUUID = "722acd93-e77f-468d-a916-67e599e7cffd"
    public static let width = 400
    public static let height = 600
    public static let frameLength = 120_000
    public static let palette: Set<UInt8> = [0, 1, 2, 3, 5, 6]

    public static func validateFrame(_ frame: Data) throws {
        guard frame.count == frameLength else { throw CardImageError.invalidFrame }
        guard frame.allSatisfy({ palette.contains($0 >> 4) && palette.contains($0 & 0x0f) }) else {
            throw CardImageError.invalidPalette
        }
    }

    public static func crc32(_ data: Data) -> UInt32 {
        var crc = UInt32.max
        for byte in data {
            crc ^= UInt32(byte)
            for _ in 0..<8 { crc = (crc >> 1) ^ ((crc & 1) != 0 ? 0xedb8_8320 : 0) }
        }
        return crc ^ UInt32.max
    }

    public static func begin(id: UInt32, frame: Data) throws -> Data {
        guard id != 0 else { throw CardImageError.invalidTransfer }
        try validateFrame(frame)
        var bytes = control(id: id, operation: 1)
        bytes.appendLE(UInt32(frame.count))
        bytes.appendLE(crc32(frame))
        bytes.appendLE(UInt16(width))
        bytes.appendLE(UInt16(height))
        return bytes
    }

    public static func commit(id: UInt32) -> Data { control(id: id, operation: 2) }
    public static func abort(id: UInt32) -> Data { control(id: id, operation: 3) }

    public static func chunk(id: UInt32, frame: Data, offset: Int, capacity: Int) throws -> Data {
        guard id != 0, capacity > 12, offset >= 0, offset < frame.count else {
            throw CardImageError.invalidTransfer
        }
        let count = min(capacity - 12, 232, frame.count - offset)
        var bytes = Data([0x49, 0x44, 1, 0])
        bytes.appendLE(id)
        bytes.appendLE(UInt32(offset))
        bytes.append(frame[offset..<(offset + count)])
        return bytes
    }

    /// Logical 600×400 landscape -> native 400×600, clockwise rotation.
    public static func pack(logicalPalette: Data) throws -> Data {
        guard logicalPalette.count == width * height,
              logicalPalette.allSatisfy({ palette.contains($0) }) else {
            throw CardImageError.invalidPalette
        }
        var native = Data(repeating: 0, count: frameLength)
        for y in 0..<width {
            for x in 0..<height {
                let nativeX = width - 1 - y
                let nativeY = x
                let pixel = nativeY * width + nativeX
                let color = logicalPalette[y * height + x]
                if pixel & 1 == 0 { native[pixel / 2] |= color << 4 }
                else { native[pixel / 2] |= color }
            }
        }
        return native
    }

    private static func control(id: UInt32, operation: UInt8) -> Data {
        var bytes = Data([0x49, 0x43, 1, operation])
        bytes.appendLE(id)
        return bytes
    }
}

private extension Data {
    mutating func appendLE<T: FixedWidthInteger>(_ value: T) {
        append(contentsOf: (0..<MemoryLayout<T>.size).map { UInt8(truncatingIfNeeded: value >> ($0 * 8)) })
    }
}
