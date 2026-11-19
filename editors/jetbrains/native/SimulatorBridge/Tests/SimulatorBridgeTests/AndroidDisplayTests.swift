import Foundation
import Testing

@testable import SimulatorBridge

@Test func sharedAndroidDisplayRejectsTruncatedPixels() throws {
  #expect(throws: (any Error).self) {
    try AndroidDisplay.validate(width: 1080, height: 2424, bytes: 24)
  }
  try AndroidDisplay.validate(width: 2, height: 4, bytes: 56)
}
