import Foundation
import Testing

@testable import SimulatorBridge

struct CommandReplyTests {
  @Test func successOmitsErrorAndKeepsWireFields() throws {
    let reply = CommandReply(id: 7, receivedNanos: 10, completedNanos: 20, error: nil)
    let object = try #require(
      JSONSerialization.jsonObject(with: JSONEncoder().encode(reply)) as? [String: Any])
    #expect(object.count == 4)
    #expect(object["id"] as? Int == 7)
    #expect(object["receivedNanos"] as? Int == 10)
    #expect(object["completedNanos"] as? Int == 20)
    #expect(object["ok"] as? Bool == true)
    #expect(object["error"] == nil)
  }

  @Test func failureIncludesItsMessage() throws {
    let reply = CommandReply(id: 7, receivedNanos: 10, completedNanos: 20, error: "Input failed")
    let object = try #require(
      JSONSerialization.jsonObject(with: JSONEncoder().encode(reply)) as? [String: Any])
    #expect(object.count == 5)
    #expect(object["ok"] as? Bool == false)
    #expect(object["error"] as? String == "Input failed")
  }
}
