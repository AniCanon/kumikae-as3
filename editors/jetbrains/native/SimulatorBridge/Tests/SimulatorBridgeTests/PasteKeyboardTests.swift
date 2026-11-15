import Testing
@testable import SimulatorBridge

struct PasteKeyboardTests {
  @Test func pasteHoldsCommandOnlyUntilVIsReleased() {
    #expect(PasteKeyboard.events == [
      KeyboardEvent(usage: 227, state: 1), KeyboardEvent(usage: 25, state: 1),
      KeyboardEvent(usage: 25, state: 2), KeyboardEvent(usage: 227, state: 2)
    ])
  }
}
