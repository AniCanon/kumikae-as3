import Foundation
import Testing

@testable import SimulatorBridge

struct PixelCopyBufferTests {
  @Test func newPixelsNeverMutateAnOlderRetainedFrameAndGeometryCanChange() {
    var buffer = PixelCopyBuffer()
    func sample(_ byte: UInt8, _ count: Int) -> Data {
      Data(repeating: byte, count: count).withUnsafeBytes { source in
        buffer.copy(from: source.baseAddress!, count: count)
      }
    }
    let first = sample(11, 16)
    let second = sample(22, 16)
    #expect(first == Data(repeating: 11, count: 16))
    #expect(second == Data(repeating: 22, count: 16))
    #expect(sample(33, 32) == Data(repeating: 33, count: 32))
    #expect(second == Data(repeating: 22, count: 16))
  }
}
