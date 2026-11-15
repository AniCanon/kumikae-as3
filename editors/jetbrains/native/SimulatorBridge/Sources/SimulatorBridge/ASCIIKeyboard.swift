import Foundation

enum ASCIIKeyboard {
  struct Key: Sendable {
    let usage: UInt32
    let shift: Bool
  }

  static func keys(for text: String) throws -> [Key] {
    guard !text.isEmpty, text.utf8.count <= 4096 else {
      throw BridgeError.invalid("Text must contain 1–4096 printable ASCII characters")
    }
    let plain = "1234567890-=[]\\;\'`,./ "
    let shifted = "!@#$%^&*()_+{}|:\"~<>? "
    let usages: [UInt32] = Array(30...39) + [45, 46, 47, 48, 49, 51, 52, 53, 54, 55, 56, 44]
    return try text.unicodeScalars.map { scalar in
      let value = scalar.value
      if (97...122).contains(value) {
        return Key(usage: value - 97 + 4, shift: false)
      }
      if (65...90).contains(value) {
        return Key(usage: value - 65 + 4, shift: true)
      }
      if let index = Array(plain.unicodeScalars).firstIndex(of: scalar) {
        return Key(usage: usages[index], shift: false)
      }
      if let index = Array(shifted.unicodeScalars).firstIndex(of: scalar) {
        return Key(usage: usages[index], shift: true)
      }
      throw BridgeError.invalid("Only printable ASCII text is supported")
    }
  }
}
