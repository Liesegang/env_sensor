import CoreGraphics
import Foundation
import ImageIO
import UniformTypeIdentifiers

guard let context = CGContext(data: nil, width: 1024, height: 1024, bitsPerComponent: 8,
    bytesPerRow: 4096, space: CGColorSpaceCreateDeviceRGB(), bitmapInfo: CGImageAlphaInfo.noneSkipLast.rawValue)
else { throw NSError(domain: "Icon", code: 1) }
context.setFillColor(CGColor(red: 0.06, green: 0.43, blue: 0.39, alpha: 1))
context.fill(CGRect(x: 0, y: 0, width: 1024, height: 1024))
context.setStrokeColor(CGColor(red: 0.94, green: 0.98, blue: 0.95, alpha: 1))
context.setFillColor(CGColor(red: 0.94, green: 0.98, blue: 0.95, alpha: 1))
context.setLineWidth(40)
context.setLineCap(.round)
let center = CGPoint(x: 512, y: 512)
for radius in [150.0, 255.0, 360.0] {
    for offset in [0.0, Double.pi] {
        context.addArc(center: center, radius: radius, startAngle: offset - .pi / 3,
                       endAngle: offset + .pi / 3, clockwise: false)
        context.strokePath()
    }
}
context.fillEllipse(in: CGRect(x: 457, y: 457, width: 110, height: 110))
let root = URL(fileURLWithPath: #filePath).deletingLastPathComponent().deletingLastPathComponent()
let url = root.appendingPathComponent("EnvSensor/Assets.xcassets/AppIcon.appiconset/AppIcon.png")
guard let bitmap = context.makeImage(),
      let output = CGImageDestinationCreateWithURL(url as CFURL, UTType.png.identifier as CFString, 1, nil)
else { throw NSError(domain: "Icon", code: 2) }
CGImageDestinationAddImage(output, bitmap, nil)
guard CGImageDestinationFinalize(output) else { throw NSError(domain: "Icon", code: 3) }
