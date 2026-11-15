import Accelerate
import Foundation

/// Display orientation applies to pixels and the inverse mapping of input together.
struct FrameRotation: Sendable {
  let orientation: UInt64
  init(_ orientation: UInt64) throws {
    guard (1...4).contains(orientation) else { throw BridgeError.invalid("Unsupported display orientation") }
    self.orientation = orientation
  }
  func checkContinuation(from previous: FrameRotation?) throws {
    if let previous, orientation != previous.orientation {
      throw BridgeError.unavailable("The Simulator orientation changed. Reconnect its picture before input.")
    }
  }

  func apply(_ frame: CapturedFrame, layout: FrameLayout) throws -> CapturedFrame {
    if orientation == 1 { return frame }
    let h = frame.header
    try h.validate(layout)
    guard frame.pixels.count == h.stride * h.height else {
      throw BridgeError.invalid("Invalid pixels for display rotation")
    }
    let width = orientation == 2 ? h.width : h.height
    let height = orientation == 2 ? h.height : h.width
    var pixels = Data(count: width * height * 4)
    let constant = UInt8(orientation == 2 ? 2 : orientation == 3 ? 3 : 1)
    try frame.pixels.withUnsafeBytes { source in
      try pixels.withUnsafeMutableBytes { destination in
        var input = vImage_Buffer(data: UnsafeMutableRawPointer(mutating: source.baseAddress),
                                  height: UInt(h.height), width: UInt(h.width), rowBytes: h.stride)
        var output = vImage_Buffer(data: destination.baseAddress, height: UInt(height), width: UInt(width), rowBytes: width * 4)
        let background: [UInt8] = [0, 0, 0, 0]
        let result = vImageRotate90_ARGB8888(&input, &output, constant, background, vImage_Flags(kvImageNoFlags))
        guard result == kvImageNoError else { throw BridgeError.invalid("Display rotation failed: \(result)") }
      }
    }
    let header = FrameHeader(sequence: h.sequence, width: width, height: height, stride: width * 4,
      observedNanos: h.observedNanos, writtenNanos: h.writtenNanos, nativeWidth: width,
      nativeHeight: height, densityMilli: h.densityMilli)
    return CapturedFrame(header: header, pixels: pixels)
  }
  func sourcePoint(x: Double, y: Double) -> (Double, Double) {
    switch orientation {
    case 2: return (1 - x, 1 - y)
    case 3: return (y, 1 - x)
    case 4: return (1 - y, x)
    default: return (x, y)
    }
  }
}
