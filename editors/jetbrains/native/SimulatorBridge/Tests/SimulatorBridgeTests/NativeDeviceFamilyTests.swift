import Testing

@testable import SimulatorBridge

struct NativeDeviceFamilyTests {
  @Test func phoneAndTabletUseTheSameScreenAndInputContract() throws {
    try NativeDeviceFamily.validate(1)
    try NativeDeviceFamily.validate(2)
  }
  @Test func otherFamiliesRemainExplicitlyUnsupported() {
    for family: Int32 in [0, 3, 4, 5, -1] {
      #expect(throws: BridgeError.self) { try NativeDeviceFamily.validate(family) }
    }
  }
}
