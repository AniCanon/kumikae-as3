import Testing

@testable import SimulatorBridge

struct InputOperationTests {
  @Test func knownWireOperationsHaveTypedValues() throws {
    for name in ["ping", "down", "move", "up", "home", "lock", "rotate", "paste", "text"] {
      let command = InputCommand(id: 1, op: name, x: nil, y: nil, text: nil)
      #expect(try command.operation.rawValue == name)
    }
  }

  @Test func unknownOperationKeepsTheActionableError() {
    let command = InputCommand(id: 1, op: "unknown", x: nil, y: nil, text: nil)
    do {
      _ = try command.operation
      Issue.record("Expected unsupported operation to fail")
    } catch {
      #expect(error.localizedDescription == "Unsupported native input command: unknown")
    }
  }
}
