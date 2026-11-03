import Foundation

public struct CardHistoryCursor: Codable, Sendable, Equatable {
    public let bootID: UInt64
    public let sequence: UInt32
    public static let beginning = Self(bootID: 0, sequence: 0)
    public init(bootID: UInt64, sequence: UInt32) { self.bootID = bootID; self.sequence = sequence }
    public var bootKey: String { "0x" + String(bootID, radix: 16).leftPadded(to: 16) }
}

private extension String {
    func leftPadded(to count: Int) -> String { String(repeating: "0", count: max(0, count - self.count)) + self }
}
