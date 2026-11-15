struct KeyboardEvent: Sendable, Equatable {
  let usage: UInt32
  let state: UInt32
}

enum PasteKeyboard {
  static let events: [KeyboardEvent] = [
    KeyboardEvent(usage: 227, state: 1), KeyboardEvent(usage: 25, state: 1),
    KeyboardEvent(usage: 25, state: 2), KeyboardEvent(usage: 227, state: 2)
  ]
}
