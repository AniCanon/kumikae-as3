import Foundation
import Testing

@testable import SimulatorBridge

struct FrameRotationTests {
  @Test func anExternalHalfTurnRevokesEvenEqualSizedPictures() throws {
    try FrameRotation(1).checkContinuation(from: nil)
    try FrameRotation(3).checkContinuation(from: FrameRotation(3))
    #expect(throws: BridgeError.self) {
      try FrameRotation(2).checkContinuation(from: FrameRotation(1))
    }
    #expect(throws: BridgeError.self) {
      try FrameRotation(4).checkContinuation(from: FrameRotation(3))
    }
  }

  @Test func rotatesPaddedPixelsAndTheirTouchCoordinatesTogether() throws {
    let layout = try FrameLayout.load()
    let pixels = Data([
      1, 0, 0, 0, 2, 0, 0, 0, 99, 99, 99, 99,
      3, 0, 0, 0, 4, 0, 0, 0, 99, 99, 99, 99,
      5, 0, 0, 0, 6, 0, 0, 0, 99, 99, 99, 99,
    ])
    let frame = CapturedFrame(
      header: FrameHeader(
        sequence: 1, width: 2, height: 3,
        stride: 12, observedNanos: 1, writtenNanos: 1, nativeWidth: 2, nativeHeight: 3,
        densityMilli: 1000), pixels: pixels)
    for (orientation, expected, point) in [
      (UInt64(2), [6, 5, 4, 3, 2, 1], (0.75, 0.6)),
      (3, [5, 3, 1, 6, 4, 2], (0.4, 0.75)),
      (4, [2, 4, 6, 1, 3, 5], (0.6, 0.25)),
    ] {
      let rotation = try FrameRotation(orientation)
      let result = try rotation.apply(frame, layout: layout)
      #expect(result.header.width == (orientation == 2 ? 2 : 3))
      #expect(result.header.height == (orientation == 2 ? 3 : 2))
      #expect(result.header.stride == result.header.width * 4)
      #expect(result.header.nativeWidth == result.header.width)
      #expect(
        stride(from: 0, to: result.pixels.count, by: 4).map { Int(result.pixels[$0]) } == expected)
      let mapped = rotation.sourcePoint(x: 0.25, y: 0.4)
      #expect(abs(mapped.0 - point.0) < 0.00001)
      #expect(abs(mapped.1 - point.1) < 0.00001)
    }
    #expect(try FrameRotation(1).apply(frame, layout: layout).pixels == pixels)
    #expect(throws: BridgeError.self) { try FrameRotation(9) }
  }
}
