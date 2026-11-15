import Foundation

private final class ResourceLocation {}

public struct FrameLayout: Decodable, Sendable {
  public let headerBytes: Int
  public let maxPayloadBytes: Int
  public let maxDimension: Int
  let magic: UInt64
  let version: UInt64
  let pixelFormat: UInt64
  let fields: [String: [Int]]

  public static func load() throws -> FrameLayout {
    // Bundle.module traps if packaging is incomplete; turn that into an actionable startup error.
    let roots = [Bundle.main.bundleURL, Bundle(for: ResourceLocation.self).bundleURL]
    let candidates = roots.flatMap { [$0, $0.deletingLastPathComponent()] }
    let url = candidates.compactMap {
      Bundle(url: $0.appendingPathComponent("SimulatorBridge_SimulatorBridge.bundle"))?
        .url(forResource: "frame-layout", withExtension: "json")
    }.first
    guard let url else {
      throw BridgeError.unavailable(
        "The bridge frame-layout resource is missing. Rebuild the plugin.")
    }
    return try JSONDecoder().decode(FrameLayout.self, from: Data(contentsOf: url))
  }

  func field(_ name: String) throws -> (offset: Int, bytes: Int) {
    guard let pair = fields[name], pair.count == 2, [4, 8].contains(pair[1]),
      pair[0] >= 0, pair[0] + pair[1] <= headerBytes
    else { throw BridgeError.invalid("Invalid frame layout field: \(name)") }
    return (pair[0], pair[1])
  }

  func read(_ name: String, from data: Data) throws -> UInt64 {
    let field = try field(name)
    guard data.count >= headerBytes else {
      throw BridgeError.invalid("Truncated native frame header")
    }
    return (0..<field.bytes).reduce(0) { value, byte in
      value | (UInt64(data[data.startIndex + field.offset + byte]) << (byte * 8))
    }
  }

  func write(_ name: String, value: UInt64, into data: inout Data) throws {
    let field = try field(name)
    guard field.bytes == 8 || value <= UInt32.max else {
      throw BridgeError.invalid("Native frame field is too large: \(name)")
    }
    for byte in 0..<field.bytes {
      data[field.offset + byte] = UInt8(truncatingIfNeeded: value >> (byte * 8))
    }
  }
}
