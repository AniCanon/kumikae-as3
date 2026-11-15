import Testing
import XPC

@testable import SimulatorBridge

struct OrientationTests {
  @Test func rotationUsesTheDedicatedServiceAndRejectsUnknownNames() throws {
    for (name, value) in [("portrait", "portrait"), ("portrait_upside_down", "portraitUpsideDown"),
                          ("landscape_left", "landscapeLeft"), ("landscape_right", "landscapeRight")] {
      let message = try AppleSimulator.orientationMessage(name)
      let payload = try #require(xpc_dictionary_get_value(message, "payload"))
      let change = try #require(xpc_dictionary_get_value(payload, "changeOrientation"))
      let orientation = try #require(xpc_dictionary_get_string(change, "_0"))
      #expect(String(cString: orientation) == value)
    }
    #expect(throws: BridgeError.self) { try AppleSimulator.orientationMessage("sideways") }
  }
  @Test func confirmsTheRequestedOrientationAndRefusesMalformedReplies() throws {
    #expect(throws: BridgeError.self) {
      try AppleSimulator.validateOrientationReply(xpc_string_create("bad"), expected: "portrait")
    }
    let reply = xpc_dictionary_create_empty()
    #expect(throws: BridgeError.self) { try AppleSimulator.validateOrientationReply(reply, expected: "landscape_left") }
    xpc_dictionary_set_int64(reply, "currentDeviceOrientation", 3)
    #expect(throws: BridgeError.self) { try AppleSimulator.validateOrientationReply(reply, expected: "landscape_left") }
    xpc_dictionary_set_string(reply, "currentDeviceOrientation", "landscapeLeft")
    try AppleSimulator.validateOrientationReply(reply, expected: "landscape_left")
    #expect(throws: BridgeError.self) { try AppleSimulator.validateOrientationReply(reply, expected: "portrait") }
    xpc_dictionary_set_string(reply, "error", "refused")
    #expect(throws: BridgeError.self) { try AppleSimulator.validateOrientationReply(reply, expected: "landscape_left") }
  }

}
