import Testing
import XPC

@testable import SimulatorBridge

struct NativeInputMessageTests {
  @Test func touchUsesNormalizedCoordinatesAndOrderedPhases() throws {
    for (phase, value) in [("down", 0), ("move", 1), ("up", 2)] {
      let message = try AppleSimulator.touchMessage(phase: phase, x: 0.25, y: 0.75)
      let type = try #require(xpc_dictionary_get_string(message, "messageType"))
      #expect(String(cString: type) == "IndigoDigitizerEvent")
      let payload = try #require(xpc_dictionary_get_value(message, "payload"))
      #expect(xpc_dictionary_get_uint64(payload, "eventType") == value)
      #expect(xpc_dictionary_get_uint64(payload, "target") == 0)
      let point = try #require(xpc_dictionary_get_value(payload, "pointOne"))
      #expect(xpc_dictionary_get_double(point, "x") == 0.25)
      #expect(xpc_dictionary_get_double(point, "y") == 0.75)
      #expect(xpc_dictionary_get_value(payload, "pointTwo") == nil)
    }
    #expect(throws: BridgeError.self) { try AppleSimulator.touchMessage(phase: "bad", x: 0, y: 0) }
  }
}
