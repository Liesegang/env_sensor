import CoreGraphics
import Foundation
import ImageIO

public enum CardImageConverter {
    public static let logicalWidth = 600
    public static let logicalHeight = 400
    private static let colors: [(code: UInt8, red: Float, green: Float, blue: Float)] = [
        (0, 0, 0, 0), (1, 255, 255, 255), (2, 255, 255, 0),
        (3, 255, 0, 0), (5, 0, 0, 255), (6, 0, 255, 0),
    ]

    public static func decode(_ photo: Data) throws -> CardImageSource {
        guard !photo.isEmpty, photo.count <= 50_000_000,
              let source = CGImageSourceCreateWithData(photo as CFData, nil),
              let image = CGImageSourceCreateThumbnailAtIndex(source, 0, [
                kCGImageSourceCreateThumbnailFromImageAlways: true,
                kCGImageSourceCreateThumbnailWithTransform: true,
                kCGImageSourceThumbnailMaxPixelSize: 1_200,
              ] as CFDictionary) else { throw CardImageError.decoding }
        return CardImageSource(image: image)
    }

    public static func prepare(_ photo: Data, adjustments: CardImageAdjustments = .init()) throws -> PreparedCardImage {
        try prepare(decode(photo), adjustments: adjustments)
    }

    public static func prepare(_ source: CardImageSource,
                               adjustments: CardImageAdjustments = .init()) throws -> PreparedCardImage {
        try Task.checkCancellation()
        let image = source.image
        let adjustments = adjustments.normalized
        let layout = placement(source, adjustments: adjustments)
        let width = logicalWidth, height = logicalHeight
        var rgba = [UInt8](repeating: 0, count: width * height * 4)
        let rendered = rgba.withUnsafeMutableBytes { memory -> Bool in
            guard let context = CGContext(data: memory.baseAddress, width: width, height: height,
                bitsPerComponent: 8, bytesPerRow: width * 4, space: CGColorSpaceCreateDeviceRGB(),
                bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue | CGBitmapInfo.byteOrder32Big.rawValue) else {
                return false
            }
            context.clear(CGRect(x: 0, y: 0, width: width, height: height))
            context.interpolationQuality = .high
            context.translateBy(x: Double(width) / 2 + layout.offset.width,
                                y: Double(height) / 2 - layout.offset.height)
            context.rotate(by: -layout.angle)
            context.scaleBy(x: layout.scale, y: layout.scale)
            context.draw(image, in: CGRect(x: -Double(image.width) / 2, y: -Double(image.height) / 2,
                                           width: Double(image.width), height: Double(image.height)))
            return true
        }
        guard rendered else { throw CardImageError.decoding }
        let raster = try dither(rgba: rgba, adjustments: adjustments)
        try Task.checkCancellation()
        let frame = try CardImageProtocol.pack(logicalPalette: raster.palette)
        guard let provider = CGDataProvider(data: Data(raster.rgba) as CFData),
              let preview = CGImage(width: width, height: height, bitsPerComponent: 8, bitsPerPixel: 32,
                bytesPerRow: width * 4, space: CGColorSpaceCreateDeviceRGB(),
                bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.last.rawValue | CGBitmapInfo.byteOrder32Big.rawValue),
                provider: provider, decode: nil, shouldInterpolate: false, intent: .defaultIntent) else {
            throw CardImageError.decoding
        }
        return PreparedCardImage(frame: frame, preview: preview)
    }

    /// Used to map a preview drag onto the same travel used by the renderer.
    public static func positionTravel(_ source: CardImageSource,
                                      adjustments: CardImageAdjustments) -> CGSize {
        placement(source, adjustments: adjustments).travel
    }

    /// Landscape output placement, in pixels: positive offset Y moves down.
    public static func placement(_ source: CardImageSource, adjustments requested: CardImageAdjustments)
        -> (scale: Double, angle: Double, offset: CGSize, travel: CGSize) {
        let adjustments = requested.normalized
        let width = Double(logicalWidth), height = Double(logicalHeight)
        let sourceWidth = Double(source.image.width), sourceHeight = Double(source.image.height)
        let angle = adjustments.rotationDegrees * .pi / 180
        let cosine = cos(angle), sine = sin(angle)
        let c = abs(cosine) < 1e-10 ? 0 : abs(cosine)
        let s = abs(sine) < 1e-10 ? 0 : abs(sine)
        let extentWidth = sourceWidth * c + sourceHeight * s
        let extentHeight = sourceWidth * s + sourceHeight * c
        let base: Double
        if adjustments.framing == .fit {
            base = min(width / extentWidth, height / extentHeight)
        } else {
            // Transform the output corners into image coordinates. Unlike using
            // the rotated bounding box, this keeps all four crop corners covered.
            base = max((width * c + height * s) / sourceWidth,
                       (width * s + height * c) / sourceHeight)
        }
        let scale = base * adjustments.zoom
        let travel = CGSize(width: abs(extentWidth * scale - width) / 2,
                            height: abs(extentHeight * scale - height) / 2)
        var x = adjustments.horizontalPosition * travel.width
        var y = adjustments.verticalPosition * travel.height
        if adjustments.framing == .crop {
            let localX = (cosine * x + sine * y) / scale
            let localY = (-sine * x + cosine * y) / scale
            let limitX = max(0, (sourceWidth - (width * c + height * s) / scale) / 2)
            let limitY = max(0, (sourceHeight - (width * s + height * c) / scale) / 2)
            let clampedX = min(limitX, max(-limitX, localX))
            let clampedY = min(limitY, max(-limitY, localY))
            x = scale * (cosine * clampedX - sine * clampedY)
            y = scale * (sine * clampedX + cosine * clampedY)
        }
        return (scale, angle, CGSize(width: x, height: y), travel)
    }

    private static func dither(rgba: [UInt8], adjustments: CardImageAdjustments) throws
        -> (palette: Data, rgba: [UInt8]) {
        let width = logicalWidth, height = logicalHeight
        var palette = Data(repeating: 1, count: width * height)
        var preview = [UInt8](repeating: 255, count: rgba.count)
        var current = [Float](repeating: 0, count: (width + 2) * 3)
        var next = current
        let saturation = Float(adjustments.saturation)
        let contrast = Float(adjustments.contrast)
        let brightness = Float(adjustments.brightness) * 255
        let tone = adjustments.autoEnhance ? try toneRange(rgba) : (gain: Float(1), center: Float(127.5))
        func adjusted(_ component: Float, luma: Float, chroma: Float, alpha: Float, error: Float) -> Float {
            // Vibrance gives muted colors more help than already saturated ones.
            // The luma anchor keeps the original hue and never colors gray pixels.
            let vibrance = 1 + 1.25 * Float(adjustments.colorStrength) * (1 - chroma / 255)
            let tonalLuma = (luma - tone.center) * tone.gain + tone.center
            let correctedLuma = luma + (tonalLuma - luma) * (1 - chroma / 255)
            let saturated = correctedLuma + (component - luma) * saturation * vibrance
            let value = (saturated - 127.5) * contrast + 127.5 + brightness
            let composited = min(255, max(0, value)) * alpha + 255 * (1 - alpha)
            return min(255, max(0, composited + error))
        }
        for y in 0..<height {
            try Task.checkCancellation()
            let direction = y & 1 == 0 ? 1 : -1
            for column in 0..<width {
                let x = direction > 0 ? column : width - 1 - column
                let pixel = (y * width + x) * 4
                let alpha = Float(rgba[pixel + 3]) / 255
                if alpha == 0 {
                    // Never carry photo errors into the empty crop/fit margins.
                    palette[y * width + x] = 1
                    continue
                }
                // Color adjustments affect the photo before it is composited onto
                // white, so empty margins and transparent pixels remain white.
                let red = alpha > 0 ? Float(rgba[pixel]) / alpha : 255
                let green = alpha > 0 ? Float(rgba[pixel + 1]) / alpha : 255
                let blue = alpha > 0 ? Float(rgba[pixel + 2]) / alpha : 255
                let luma = red * 0.2126 + green * 0.7152 + blue * 0.0722
                let chroma = max(red, green, blue) - min(red, green, blue)
                let isNeutral = chroma < 0.5 || saturation == 0
                let errorOffset = (x + 1) * 3
                let valueR = adjusted(red, luma: luma, chroma: chroma, alpha: alpha, error: current[errorOffset])
                let valueG = adjusted(green, luma: luma, chroma: chroma, alpha: alpha, error: current[errorOffset + 1])
                let valueB = adjusted(blue, luma: luma, chroma: chroma, alpha: alpha, error: current[errorOffset + 2])
                var nearest = colors[0]
                var closest = Float.greatestFiniteMagnitude
                for color in colors {
                    if isNeutral && color.code > 1 { continue }
                    let dr = valueR - color.red, dg = valueG - color.green, db = valueB - color.blue
                    let distance = dr * dr + dg * dg + db * db
                    if distance < closest { nearest = color; closest = distance }
                }
                palette[y * width + x] = nearest.code
                for channel in 0..<3 {
                    let color = channel == 0 ? nearest.red : channel == 1 ? nearest.green : nearest.blue
                    let value = channel == 0 ? valueR : channel == 1 ? valueG : valueB
                    preview[pixel + channel] = UInt8(color)
                    let error = value - color
                    current[(x + 1 + direction) * 3 + channel] += error * 7 / 16
                    next[(x + 1 - direction) * 3 + channel] += error * 3 / 16
                    next[(x + 1) * 3 + channel] += error * 5 / 16
                    next[(x + 1 + direction) * 3 + channel] += error / 16
                }
            }
            current = next
            next = [Float](repeating: 0, count: next.count)
        }
        return (palette, preview)
    }

    private static func toneRange(_ rgba: [UInt8]) throws -> (gain: Float, center: Float) {
        var histogram = [Int](repeating: 0, count: 256)
        var count = 0
        for y in 0..<logicalHeight {
            try Task.checkCancellation()
            for x in 0..<logicalWidth {
                let offset = (y * logicalWidth + x) * 4
                // Empty margins and mostly transparent edges do not change the
                // photo's exposure or contrast estimate.
                guard rgba[offset + 3] >= 250 else { continue }
                let luma = (Int(rgba[offset]) * 54 + Int(rgba[offset + 1]) * 183 + Int(rgba[offset + 2]) * 19) / 256
                histogram[luma] += 1
                count += 1
            }
        }
        guard count > 0 else { return (1, 127.5) }
        let lowerCount = max(1, count / 100), upperCount = count - lowerCount
        var running = 0, low = 0, high = 255
        for value in 0..<256 {
            running += histogram[value]
            if running >= lowerCount { low = value; break }
        }
        running = 0
        for value in 0..<256 {
            running += histogram[value]
            if running >= upperCount { high = value; break }
        }
        let span = Float(high - low)
        // Avoid changing flat artwork or amplifying a nearly uniform image's noise.
        guard span >= 24 else { return (1, 127.5) }
        return (max(1, min(1.25, 220 / span)), Float(high + low) / 2)
    }
}
