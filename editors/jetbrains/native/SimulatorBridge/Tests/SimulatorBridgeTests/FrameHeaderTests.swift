import Foundation
import Testing

@testable import SimulatorBridge

struct FrameHeaderTests {
  let sample = FrameHeader(
    sequence: 7, width: 2, height: 3, stride: 8, observedNanos: 123,
    writtenNanos: 456, nativeWidth: 6, nativeHeight: 9, densityMilli: 3000)

  @Test func roundTrip() throws {
    let bytes = try sample.encode()
    #expect(bytes.count == 64)
    let fixture = try #require(
      Bundle.module.url(forResource: "frame-header-golden", withExtension: "hex"))
    let golden = try String(contentsOf: fixture, encoding: .utf8).trimmingCharacters(
      in: .whitespacesAndNewlines)
    #expect(bytes.map { String(format: "%02x", $0) }.joined() == golden)
    #expect(try FrameHeader.decode(bytes) == sample)
    #expect(Array(bytes[8..<16]) == [7, 0, 0, 0, 0, 0, 0, 0])
  }

  @Test func rejectsUnsignedValuesTheJVMCannotRepresent() throws {
    var bad = sample
    bad.sequence = UInt64.max
    #expect(throws: (any Error).self) { try bad.encode() }
    bad = sample
    bad.writtenNanos = UInt64.max
    #expect(throws: (any Error).self) { try bad.encode() }
  }

  @Test func rejectsInvalidDimensionsAndStride() {
    for (width, height, stride) in [(0, 3, 8), (2, 0, 8), (2, 3, 7), (4097, 1, 16388)] {
      var bad = sample
      bad.width = width
      bad.height = height
      bad.stride = stride
      #expect(throws: (any Error).self) { try bad.encode() }
    }
  }

  @Test func rejectsTruncationUnknownVersionAndFormat() throws {
    let bytes = try sample.encode()
    #expect(throws: (any Error).self) { try FrameHeader.decode(bytes.prefix(63)) }
    for offset in [0, 4, 28] {
      var bad = bytes
      bad[offset] ^= 255
      #expect(throws: (any Error).self) { try FrameHeader.decode(bad) }
    }
  }
}
