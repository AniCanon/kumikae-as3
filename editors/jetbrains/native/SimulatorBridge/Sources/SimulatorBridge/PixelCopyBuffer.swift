import Foundation

/// Actor-owned storage; retained frame values still receive Data's copy-on-write protection.
struct PixelCopyBuffer {
  private var pixels = Data()
  private(set) var storageChanged = false
  mutating func copy(from source: UnsafeRawPointer, count: Int) -> Data {
    let previous = pixels.withUnsafeBytes { UInt(bitPattern: $0.baseAddress) }
    storageChanged = pixels.count != count
    if pixels.count != count { pixels = Data(count: count) }
    pixels.withUnsafeMutableBytes { destination in
      if let base = destination.baseAddress {
        storageChanged = storageChanged || UInt(bitPattern: base) != previous
        memcpy(base, source, count)
      }
    }
    return pixels
  }
}
