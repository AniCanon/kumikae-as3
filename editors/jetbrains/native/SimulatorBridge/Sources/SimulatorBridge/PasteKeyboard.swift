struct KeyboardEvent: Sendable, Equatable {
  let usage: UInt32
  let state: UInt32
}

enum PasteKeyboard {
  static let events: [KeyboardEvent] = [
    KeyboardEvent(usage: NativeInputValues.leftCommand, state: NativeInputValues.pressed),
    KeyboardEvent(usage: NativeInputValues.letterV, state: NativeInputValues.pressed),
    KeyboardEvent(usage: NativeInputValues.letterV, state: NativeInputValues.released),
    KeyboardEvent(usage: NativeInputValues.leftCommand, state: NativeInputValues.released),
  ]
}
