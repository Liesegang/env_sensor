import CoreGraphics
import Foundation
import ImageIO
import Testing
@testable import EnvSensorCore

struct CardImageEditingTests {
    @Test func clockwiseAndCounterclockwiseEditsKeepExpectedPhotoOrientation() throws {
        let photo = try image(width: 200, height: 400) { _, y in
            y < 200 ? [255, 0, 0, 255] : [0, 0, 255, 255]
        }
        var edit = CardImageAdjustments()
        edit.rotate(by: 90)
        let clockwise = try CardImageConverter.prepare(photo, adjustments: edit)
        #expect(pixel(clockwise.frame, x: 100, y: 200) == 5)
        #expect(pixel(clockwise.frame, x: 500, y: 200) == 3)
        edit.rotate(by: -180)
        let counterclockwise = try CardImageConverter.prepare(photo, adjustments: edit)
        #expect(pixel(counterclockwise.frame, x: 100, y: 200) == 3)
        #expect(pixel(counterclockwise.frame, x: 500, y: 200) == 5)
    }

    @Test func cropPositionSelectsTopOrBottomAndZoomSelectsPhotoCenter() throws {
        let portrait = try image(width: 200, height: 600) { _, y in
            y < 200 ? [255, 0, 0, 255] : y < 400 ? [0, 255, 0, 255] : [0, 0, 255, 255]
        }
        var edit = CardImageAdjustments()
        edit.framing = .crop
        edit.verticalPosition = 1
        #expect(pixel(try CardImageConverter.prepare(portrait, adjustments: edit).frame, x: 300, y: 200) == 3)
        edit.verticalPosition = -1
        #expect(pixel(try CardImageConverter.prepare(portrait, adjustments: edit).frame, x: 300, y: 200) == 5)

        let landscape = try image(width: 600, height: 400) { x, _ in
            x < 200 ? [0, 0, 0, 255] : x < 400 ? [255, 0, 0, 255] : [0, 0, 255, 255]
        }
        edit = CardImageAdjustments()
        let original = try CardImageConverter.prepare(landscape)
        #expect(pixel(original.frame, x: 50, y: 200) == 0)
        #expect(pixel(original.frame, x: 550, y: 200) == 5)
        edit.zoom = 4
        let zoomed = try CardImageConverter.prepare(landscape, adjustments: edit)
        #expect(pixel(zoomed.frame, x: 50, y: 200) == 3)
        #expect(pixel(zoomed.frame, x: 550, y: 200) == 3)
    }

    @Test func angledCropCoversCornersAtExtremePositionAndFitMarginsStayWhite() throws {
        let red = try image(width: 200, height: 400) { _, _ in [255, 0, 0, 255] }
        var edit = CardImageAdjustments()
        edit.framing = .crop
        edit.rotationDegrees = 37
        edit.zoom = 1.8
        edit.horizontalPosition = 1
        edit.verticalPosition = -1
        let cropped = try CardImageConverter.prepare(red, adjustments: edit)
        for (x, y) in [(2, 2), (597, 2), (2, 397), (597, 397)] {
            #expect(pixel(cropped.frame, x: x, y: y) == 3)
        }
        edit = CardImageAdjustments()
        edit.brightness = -0.5
        let fitted = try CardImageConverter.prepare(red, adjustments: edit)
        #expect(pixel(fitted.frame, x: 50, y: 100) == 1)
        #expect(pixel(fitted.frame, x: 550, y: 300) == 1)
    }

    @Test func colorControlsChangeLuminanceContrastAndSaturationBeforeDithering() throws {
        let gray = try image(width: 600, height: 400) { _, _ in [80, 80, 80, 255] }
        let original = try CardImageConverter.prepare(gray)
        var edit = CardImageAdjustments()
        edit.brightness = 0.3
        let brighter = try CardImageConverter.prepare(gray, adjustments: edit)
        #expect(meanLuma(brighter.preview, left: 0, right: 600) > meanLuma(original.preview, left: 0, right: 600) + 60)

        let levels = try image(width: 600, height: 400) { x, _ in
            let value: UInt8 = x < 300 ? 80 : 175
            return [value, value, value, 255]
        }
        let normal = try CardImageConverter.prepare(levels)
        edit = CardImageAdjustments()
        edit.contrast = 1.5
        let contrasted = try CardImageConverter.prepare(levels, adjustments: edit)
        let normalDifference = meanLuma(normal.preview, left: 300, right: 600) - meanLuma(normal.preview, left: 0, right: 300)
        let editedDifference = meanLuma(contrasted.preview, left: 300, right: 600) - meanLuma(contrasted.preview, left: 0, right: 300)
        #expect(editedDifference > normalDifference + 30)

        let red = try image(width: 600, height: 400) { _, _ in [255, 0, 0, 255] }
        edit = CardImageAdjustments()
        edit.saturation = 0
        let monochrome = try CardImageConverter.prepare(red, adjustments: edit)
        #expect(monochrome.frame.allSatisfy { [0, 1].contains($0 >> 4) && [0, 1].contains($0 & 15) })
    }

    @Test func everyPreviewPixelMatchesPackedFrameAfterAllEdits() throws {
        let photo = try image(width: 500, height: 700) { x, y in
            [UInt8(x % 256), UInt8(y % 256), UInt8((x + y) % 256), 255]
        }
        var edit = CardImageAdjustments()
        edit.framing = .crop
        edit.rotationDegrees = 123
        edit.zoom = 1.7
        edit.horizontalPosition = -0.6
        edit.verticalPosition = 0.8
        edit.brightness = 0.12
        edit.contrast = 1.2
        edit.saturation = 0.6
        let prepared = try CardImageConverter.prepare(photo, adjustments: edit)
        try CardImageProtocol.validateFrame(prepared.frame)
        let data = try #require(prepared.preview.dataProvider?.data)
        let bytes = try #require(CFDataGetBytePtr(data))
        let colors: [UInt8: [UInt8]] = [0: [0, 0, 0], 1: [255, 255, 255], 2: [255, 255, 0],
                                      3: [255, 0, 0], 5: [0, 0, 255], 6: [0, 255, 0]]
        var matches = true
        for y in 0..<400 {
            for x in 0..<600 {
                guard let color = colors[pixel(prepared.frame, x: x, y: y)] else { matches = false; continue }
                let offset = (y * 600 + x) * 4
                if (0..<3).contains(where: { bytes[offset + $0] != color[$0] }) || bytes[offset + 3] != 255 {
                    matches = false
                }
            }
        }
        #expect(matches)
    }

    @Test func imageOrientationIsAppliedOnceAndDecodeIsBounded() throws {
        let portrait = try cgImage(width: 200, height: 400) { _, y in
            y < 200 ? [255, 0, 0, 255] : [0, 0, 255, 255]
        }
        let encoded = NSMutableData()
        let destination = try #require(CGImageDestinationCreateWithData(encoded, "public.jpeg" as CFString, 1, nil))
        CGImageDestinationAddImage(destination, portrait, [kCGImagePropertyOrientation: 6,
            kCGImageDestinationLossyCompressionQuality: 1] as CFDictionary)
        #expect(CGImageDestinationFinalize(destination))
        let source = try CardImageConverter.decode(encoded as Data)
        #expect(source.image.width == 400 && source.image.height == 200)
        let prepared = try CardImageConverter.prepare(source)
        #expect(pixel(prepared.frame, x: 100, y: 200) == 5)
        #expect(pixel(prepared.frame, x: 500, y: 200) == 3)

        let large = try image(width: 2_500, height: 1_000) { _, _ in [255, 255, 255, 255] }
        let bounded = try CardImageConverter.decode(large)
        #expect(max(bounded.image.width, bounded.image.height) == 1_200)
    }

    @Test func malformedAdjustmentsNormalizeAndCancellationStopsConversion() async throws {
        var invalid = CardImageAdjustments()
        invalid.rotationDegrees = .infinity
        invalid.zoom = .nan
        invalid.horizontalPosition = 8
        invalid.brightness = -8
        invalid.contrast = .nan
        #expect(invalid.normalized.rotationDegrees == 0)
        #expect(invalid.normalized.zoom == 1)
        #expect(invalid.normalized.horizontalPosition == 1)
        #expect(invalid.normalized.brightness == -0.5)
        #expect(invalid.normalized.contrast == 1)
        let source = try CardImageConverter.decode(image(width: 600, height: 400) { _, _ in [255, 0, 0, 255] })
        let cancelled = Task {
            withUnsafeCurrentTask { $0?.cancel() }
            return try CardImageConverter.prepare(source)
        }
        do {
            _ = try await cancelled.value
            Issue.record("Cancelled conversion produced a transferable frame")
        } catch is CancellationError { }
    }

    private func pixel(_ frame: Data, x: Int, y: Int) -> UInt8 {
        let at = x * 400 + 399 - y
        return at & 1 == 0 ? frame[at / 2] >> 4 : frame[at / 2] & 15
    }

    private func meanLuma(_ image: CGImage, left: Int, right: Int) -> Double {
        guard let data = image.dataProvider?.data, let bytes = CFDataGetBytePtr(data) else { return .nan }
        var total = 0.0
        for y in 0..<image.height {
            for x in left..<right {
                let offset = y * image.bytesPerRow + x * 4
                total += Double(bytes[offset]) * 0.2126 + Double(bytes[offset + 1]) * 0.7152 + Double(bytes[offset + 2]) * 0.0722
            }
        }
        return total / Double((right - left) * image.height)
    }

    private func image(width: Int, height: Int, color: (Int, Int) -> [UInt8]) throws -> Data {
        let image = try cgImage(width: width, height: height, color: color)
        let data = NSMutableData()
        let destination = try #require(CGImageDestinationCreateWithData(data, "public.png" as CFString, 1, nil))
        CGImageDestinationAddImage(destination, image, nil)
        #expect(CGImageDestinationFinalize(destination))
        return data as Data
    }

    private func cgImage(width: Int, height: Int, color: (Int, Int) -> [UInt8]) throws -> CGImage {
        var rgba = [UInt8](repeating: 255, count: width * height * 4)
        for y in 0..<height {
            for x in 0..<width {
                let values = color(x, y), offset = (y * width + x) * 4
                for channel in 0..<4 { rgba[offset + channel] = values[channel] }
            }
        }
        let provider = try #require(CGDataProvider(data: Data(rgba) as CFData))
        return try #require(CGImage(width: width, height: height, bitsPerComponent: 8, bitsPerPixel: 32,
            bytesPerRow: width * 4, space: CGColorSpaceCreateDeviceRGB(),
            bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.last.rawValue | CGBitmapInfo.byteOrder32Big.rawValue),
            provider: provider, decode: nil, shouldInterpolate: false, intent: .defaultIntent))
    }
}
