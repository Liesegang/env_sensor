import CoreGraphics
import Foundation
import ImageIO
import Testing
@testable import EnvSensorCore

struct CardImageColorEnhancementTests {
    @Test func mutedPhotoColorsGainCoverageAcrossImageWithoutInventingHues() throws {
        let photo = try pastelFixture()
        var unenhanced = CardImageAdjustments()
        unenhanced.autoEnhance = false
        unenhanced.colorStrength = 0
        let before = try CardImageConverter.prepare(photo, adjustments: unenhanced)
        let after = try CardImageConverter.prepare(photo)
        let old = counts(before.frame), new = counts(after.frame)
        let oldColored = old.dropFirst(2).reduce(0, +), newColored = new.dropFirst(2).reduce(0, +)
        #expect(newColored > oldColored * 11 / 10)
        let oldColors = [2, 3, 5, 6].map { old[$0] }, newColors = [2, 3, 5, 6].map { new[$0] }
        // A single hue should not dominate the six equally represented inputs.
        #expect(Double(newColors.max()!) / Double(newColors.min()!) < Double(oldColors.max()!) / Double(oldColors.min()!))
        for quadrant in 0..<4 {
            let oldRegion = counts(before.frame, quadrant: quadrant)
            let newRegion = counts(after.frame, quadrant: quadrant)
            #expect(newRegion.dropFirst(2).reduce(0, +) > oldRegion.dropFirst(2).reduce(0, +) * 108 / 100)
            for code in [2, 3, 5, 6] { #expect(newRegion[code] > 50) }
        }
        try CardImageProtocol.validateFrame(after.frame)
        if let path = ProcessInfo.processInfo.environment["CARD_IMAGE_ARTIFACT_DIR"] {
            let directory = URL(fileURLWithPath: path)
            try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
            try photo.write(to: directory.appendingPathComponent("pastel-original.png"))
            try writePNG(before.preview, to: directory.appendingPathComponent("pastel-before.png"))
            try writePNG(after.preview, to: directory.appendingPathComponent("pastel-after.png"))
            let stats: [String: Any] = ["paletteCodes": [0, 1, 2, 3, 4, 5, 6], "before": old, "after": new,
                                       "beforeColoredFraction": Double(oldColored) / 240_000,
                                       "afterColoredFraction": Double(newColored) / 240_000]
            try JSONSerialization.data(withJSONObject: stats, options: [.prettyPrinted, .sortedKeys])
                .write(to: directory.appendingPathComponent("pastel-statistics.json"))
        }
    }

    @Test func grayscaleAndWhiteMarginsStayAchromaticAtMaximumColorStrength() throws {
        var edit = CardImageAdjustments()
        edit.colorStrength = 2
        let grayscale = try encode(width: 600, height: 400) { x, _ in
            let value = UInt8(40 + x * 180 / 599)
            return [value, value, value, 255]
        }
        let neutral = try CardImageConverter.prepare(grayscale, adjustments: edit)
        #expect(neutral.frame.allSatisfy { $0 >> 4 <= 1 && $0 & 15 <= 1 })
        let portrait = try encode(width: 200, height: 400) { _, y in
            y < 200 ? [225, 190, 194, 255] : [190, 220, 233, 255]
        }
        edit.brightness = -0.2
        let fitted = try CardImageConverter.prepare(portrait, adjustments: edit)
        var white = true
        for y in 0..<400 {
            for x in 0..<190 {
                if pixel(fitted.frame, x: x, y: y) != 1 || pixel(fitted.frame, x: 599 - x, y: y) != 1 {
                    white = false
                }
            }
        }
        #expect(white)
        edit.saturation = 0
        let manualGray = try CardImageConverter.prepare(portrait, adjustments: edit)
        #expect(manualGray.frame.allSatisfy { $0 >> 4 <= 1 && $0 & 15 <= 1 })
    }

    @Test func colorfulPixelsDoNotBleedIntoNeutralArtworkAndTransparentArea() throws {
        let photo = try encode(width: 600, height: 400) { x, _ in
            if x < 200 { return [210, 165, 170, 255] }
            if x < 400 { return [160, 160, 160, 255] }
            return [0, 0, 0, 0]
        }
        let prepared = try CardImageConverter.prepare(photo)
        var clean = true
        for y in 0..<400 {
            for x in 200..<600 {
                let color = pixel(prepared.frame, x: x, y: y)
                if (x < 400 && color > 1) || (x >= 400 && color != 1) { clean = false }
            }
        }
        #expect(clean)
    }

    @Test func brightPresetPreservesExactPanelPaletteAndIsRepeatable() throws {
        let colors: [[UInt8]] = [[0, 0, 0, 255], [255, 255, 255, 255], [255, 255, 0, 255],
                                 [255, 0, 0, 255], [0, 0, 255, 255], [0, 255, 0, 255]]
        let codes: [UInt8] = [0, 1, 2, 3, 5, 6]
        let photo = try encode(width: 600, height: 400) { x, _ in colors[x / 100] }
        let first = try CardImageConverter.prepare(photo)
        let second = try CardImageConverter.prepare(photo)
        #expect(first.frame == second.frame)
        for x in 0..<6 { #expect(pixel(first.frame, x: x * 100 + 50, y: 200) == codes[x]) }
    }

    private func pastelFixture() throws -> Data {
        let colors: [[UInt8]] = [[224, 188, 191], [232, 224, 188], [188, 223, 195],
                                 [190, 205, 234], [219, 190, 231], [235, 207, 187]]
        return try encode(width: 600, height: 400) { x, y in
            let color = colors[(x / 100 + y / 100 * 2) % colors.count]
            let shade = (x % 100 + y % 100) / 8 - 12
            return color.map { UInt8(min(255, max(0, Int($0) + shade))) } + [255]
        }
    }

    private func counts(_ frame: Data, quadrant: Int? = nil) -> [Int] {
        var result = [Int](repeating: 0, count: 7)
        for y in 0..<400 {
            for x in 0..<600 {
                if let quadrant, (x >= 300 ? 1 : 0) + (y >= 200 ? 2 : 0) != quadrant { continue }
                result[Int(pixel(frame, x: x, y: y))] += 1
            }
        }
        return result
    }

    private func pixel(_ frame: Data, x: Int, y: Int) -> UInt8 {
        let at = x * 400 + 399 - y
        return at & 1 == 0 ? frame[at / 2] >> 4 : frame[at / 2] & 15
    }

    private func encode(width: Int, height: Int, color: (Int, Int) -> [UInt8]) throws -> Data {
        var rgba = [UInt8](repeating: 255, count: width * height * 4)
        for y in 0..<height {
            for x in 0..<width {
                let values = color(x, y), offset = (y * width + x) * 4
                for channel in 0..<4 { rgba[offset + channel] = values[channel] }
            }
        }
        let provider = try #require(CGDataProvider(data: Data(rgba) as CFData))
        let image = try #require(CGImage(width: width, height: height, bitsPerComponent: 8, bitsPerPixel: 32,
            bytesPerRow: width * 4, space: CGColorSpaceCreateDeviceRGB(),
            bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.last.rawValue | CGBitmapInfo.byteOrder32Big.rawValue),
            provider: provider, decode: nil, shouldInterpolate: false, intent: .defaultIntent))
        let data = NSMutableData()
        let destination = try #require(CGImageDestinationCreateWithData(data, "public.png" as CFString, 1, nil))
        CGImageDestinationAddImage(destination, image, nil)
        #expect(CGImageDestinationFinalize(destination))
        return data as Data
    }

    private func writePNG(_ image: CGImage, to url: URL) throws {
        let destination = try #require(CGImageDestinationCreateWithURL(url as CFURL, "public.png" as CFString, 1, nil))
        CGImageDestinationAddImage(destination, image, nil)
        #expect(CGImageDestinationFinalize(destination))
    }
}
