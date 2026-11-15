import Testing

@testable import SimulatorBridge

struct ASCIIKeyboardTests {
  @Test func everyPrintableCharacterHasAKeyboardUsage() throws {
    let printable = String((32...126).compactMap(UnicodeScalar.init).map(Character.init))
    let keys = try ASCIIKeyboard.keys(for: printable)
    #expect(keys.count == 95)
    #expect(keys.allSatisfy { (4...56).contains($0.usage) })
    let pairs = try ASCIIKeyboard.keys(for: "aA1!")
    #expect(pairs.map(\.usage) == [4, 4, 30, 30])
    #expect(pairs.map(\.shift) == [false, true, false, true])
  }
  @Test func refusesControlsUnicodeAndOversizedText() {
    for text in ["", "\n", "é", String(repeating: "a", count: 4097)] {
      #expect(throws: (any Error).self) { try ASCIIKeyboard.keys(for: text) }
    }
  }
}
