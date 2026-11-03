import CoreGraphics
import Foundation
import ImageIO
import Testing
@testable import EnvSensorCore

struct CardImageTests {
    @Test func crcAndControlMatchIndependentIEEEWireValues() throws {
        #expect(CardImageProtocol.crc32(Data("123456789".utf8)) == 0xcbf4_3926)
        let white = Data(repeating: 0x11, count: 120_000)
        // Expected CRC independently generated with Python zlib.crc32.
        #expect(CardImageProtocol.crc32(white) == 0xd576_c086)
        let begin = try CardImageProtocol.begin(id: 0x1234_5678, frame: white)
        #expect(begin == Data([0x49, 0x43, 1, 1, 0x78, 0x56, 0x34, 0x12,
            0xc0, 0xd4, 1, 0, 0x86, 0xc0, 0x76, 0xd5, 0x90, 1, 0x58, 2]))
        #expect(CardImageProtocol.commit(id: 0x1234_5678) == Data([0x49, 0x43, 1, 2, 0x78, 0x56, 0x34, 0x12]))
        #expect(CardImageProtocol.abort(id: 0x1234_5678)[3] == 3)
        #expect(throws: CardImageError.invalidTransfer) { try CardImageProtocol.begin(id: 0, frame: white) }
        #expect(throws: CardImageError.invalidFrame) { try CardImageProtocol.validateFrame(white.dropLast()) }
        var invalid = white
        invalid[70_001] = 0x47
        #expect(throws: CardImageError.invalidPalette) { try CardImageProtocol.validateFrame(invalid) }
    }

    @Test func chunksRespectSmallAndLargeATTLimitAndReassembleExactly() throws {
        let frame = Data((0..<120_000).map { UInt8(truncatingIfNeeded: $0) })
        for capacity in [20, 244, 512] {
            var joined = Data(), offset = 0
            while offset < frame.count {
                let packet = try CardImageProtocol.chunk(id: 13, frame: frame, offset: offset, capacity: capacity)
                #expect(packet.count <= capacity)
                #expect(packet.count <= 244)
                #expect(packet.prefix(4) == Data([0x49, 0x44, 1, 0]))
                let wireOffset = (0..<4).reduce(UInt32(0)) { $0 | UInt32(packet[8 + $1]) << ($1 * 8) }
                #expect(wireOffset == offset)
                joined.append(packet.dropFirst(12))
                offset += packet.count - 12
            }
            #expect(joined == frame)
        }
        #expect(throws: CardImageError.invalidTransfer) {
            try CardImageProtocol.chunk(id: 13, frame: frame, offset: 0, capacity: 12)
        }
    }

    @Test func clockwiseRotationPlacesLandscapeCornersAndPacksLeftInHighNibble() throws {
        var landscape = Data(repeating: 1, count: 600 * 400)
        landscape[0] = 3
        landscape[599] = 2
        landscape[399 * 600] = 5
        landscape[399 * 600 + 599] = 6
        let native = try CardImageProtocol.pack(logicalPalette: landscape)
        #expect(native.count == 120_000)
        #expect(native[0] == 0x51)
        #expect(native[199] == 0x13)
        #expect(native[599 * 200] == 0x61)
        #expect(native[119_999] == 0x12)
        try CardImageProtocol.validateFrame(native)
    }

    @Test func statusAcceptsWorkerStatesAndRejectsInvalidProgressAndErrors() throws {
        var packet = Data([0x49, 0x53, 1, 6] + Array(repeating: UInt8(0), count: 20))
        packet.writeLE(UInt32(22), at: 4)
        packet.writeLE(UInt32(120_000), at: 12)
        packet.writeLE(UInt32(0xd576_c086), at: 16)
        #expect(try CardImageStatus(packet: packet).state == 6)
        packet[3] = 7
        #expect(try CardImageStatus(packet: packet).state == 7)
        packet.writeLE(UInt32(120_001), at: 8)
        #expect(throws: CardImageError.invalidStatus) { try CardImageStatus(packet: packet) }
        packet.writeLE(UInt32(0), at: 8)
        packet[3] = 3
        packet.writeLE(Int32(-5), at: 20)
        #expect(try CardImageStatus(packet: packet).error == -5)
        packet[3] = 5
        #expect(throws: CardImageError.invalidStatus) { try CardImageStatus(packet: packet) }
    }

    @Test func converterKeepsOrientationColorsAndWhiteMarginsWithoutCropping() throws {
        let width = 200, height = 400
        var rgba = [UInt8](repeating: 255, count: width * height * 4)
        for y in 0..<height {
            for x in 0..<width {
                let at = (y * width + x) * 4
                rgba[at] = y < 200 ? 255 : 0
                rgba[at + 1] = 0
                rgba[at + 2] = y < 200 ? 0 : 255
            }
        }
        let provider = try #require(CGDataProvider(data: Data(rgba) as CFData))
        let image = try #require(CGImage(width: width, height: height, bitsPerComponent: 8, bitsPerPixel: 32,
            bytesPerRow: width * 4, space: CGColorSpaceCreateDeviceRGB(),
            bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.last.rawValue | CGBitmapInfo.byteOrder32Big.rawValue),
            provider: provider, decode: nil, shouldInterpolate: false, intent: .defaultIntent))
        let encoded = NSMutableData()
        let destination = try #require(CGImageDestinationCreateWithData(encoded, "public.png" as CFString, 1, nil))
        CGImageDestinationAddImage(destination, image, nil)
        #expect(CGImageDestinationFinalize(destination))
        let converted = try CardImageConverter.prepare(encoded as Data)
        #expect(converted.preview.width == 600 && converted.preview.height == 400)
        try CardImageProtocol.validateFrame(converted.frame)
        func logicalPixel(_ x: Int, _ y: Int) -> UInt8 {
            let nativePixel = x * 400 + 399 - y
            let byte = converted.frame[nativePixel / 2]
            return nativePixel & 1 == 0 ? byte >> 4 : byte & 15
        }
        #expect(logicalPixel(100, 100) == 1)
        #expect(logicalPixel(500, 300) == 1)
        #expect(logicalPixel(300, 100) == 3)
        #expect(logicalPixel(300, 300) == 5)
        #expect(throws: CardImageError.decoding) { try CardImageConverter.prepare(Data("invalid".utf8)) }
    }
}
