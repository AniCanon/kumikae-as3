import Foundation

public struct FrameHeader: Equatable, Sendable {
  public var sequence: UInt64
  public var width: Int
  public var height: Int
  public var stride: Int
  public var observedNanos: UInt64
  public var writtenNanos: UInt64
  public var nativeWidth: Int
  public var nativeHeight: Int
  public var densityMilli: Int

  public func encode(layout: FrameLayout? = nil) throws -> Data {
    let layout = try layout ?? FrameLayout.load()
    try validate(layout)
    var data = Data(count: layout.headerBytes)
    let values: [String: UInt64] = [
      "magic": layout.magic, "version": layout.version, "sequence": sequence,
      "width": UInt64(width), "height": UInt64(height), "stride": UInt64(stride),
      "pixelFormat": layout.pixelFormat, "observedNanos": observedNanos,
      "writtenNanos": writtenNanos, "nativeWidth": UInt64(nativeWidth),
      "nativeHeight": UInt64(nativeHeight), "densityMilli": UInt64(densityMilli), "flags": 1,
    ]
    for (name, value) in values {
      try layout.write(name, value: value, into: &data)
    }
    return data
  }

  public static func decode(_ data: Data, layout: FrameLayout? = nil) throws -> FrameHeader {
    let layout = try layout ?? FrameLayout.load()
    for (name, expected) in [
      ("magic", layout.magic), ("version", layout.version), ("pixelFormat", layout.pixelFormat),
      ("flags", 1),
    ] {
      guard try layout.read(name, from: data) == expected else {
        throw BridgeError.invalid("Unsupported native frame \(name)")
      }
    }
    func read(_ name: String) throws -> UInt64 {
      try layout.read(name, from: data)
    }
    let header = try FrameHeader(
      sequence: read("sequence"), width: Int(read("width")), height: Int(read("height")),
      stride: Int(read("stride")), observedNanos: read("observedNanos"),
      writtenNanos: read("writtenNanos"),
      nativeWidth: Int(read("nativeWidth")), nativeHeight: Int(read("nativeHeight")),
      densityMilli: Int(read("densityMilli")))
    try header.validate(layout)
    return header
  }

  func validate(_ layout: FrameLayout) throws {
    guard sequence > 0, sequence <= Int64.max, writtenNanos <= Int64.max,
      [width, height, nativeWidth, nativeHeight].allSatisfy({
        (1...layout.maxDimension).contains($0)
      }),
      stride >= width * 4, stride <= layout.maxDimension * 4, stride % 4 == 0,
      stride * height <= layout.maxPayloadBytes, (1000...10000).contains(densityMilli),
      observedNanos > 0, writtenNanos >= observedNanos
    else {
      throw BridgeError.invalid("Invalid native frame geometry or timestamp")
    }
  }
}
