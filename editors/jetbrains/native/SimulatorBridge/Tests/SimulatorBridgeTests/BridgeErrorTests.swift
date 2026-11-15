import Testing

@testable import SimulatorBridge

@Suite struct BridgeErrorTests {
  @Test func stoppedDeviceIsRetryableWithoutChangingBackend() {
    #expect(BridgeError.deviceUnavailable("Stopped").code == "DEVICE_UNAVAILABLE")
    #expect(BridgeError.unavailable("Missing symbol").code == "INPUT_UNAVAILABLE")
  }
}
