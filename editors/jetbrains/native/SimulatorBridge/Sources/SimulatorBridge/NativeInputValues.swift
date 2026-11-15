/// Values required by the runtime input messages; kept inside the native adapter.
enum NativeInputValues {
  static let pressed: UInt32 = 1
  static let released: UInt32 = 2
  static let keyPress = [pressed, released]
  static let leftShift: UInt32 = 225
  static let leftCommand: UInt32 = 227
  static let letterV: UInt32 = 25
}
