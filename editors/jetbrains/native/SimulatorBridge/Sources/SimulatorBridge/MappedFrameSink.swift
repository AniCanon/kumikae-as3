import Darwin
import Foundation

public actor MappedFrameSink: FrameSink {
  private let layout: FrameLayout
  private var descriptor: Int32
  private var memory: UnsafeMutableRawPointer?
  private let size: Int

  public init(path: String, layout: FrameLayout) throws {
    self.layout = layout
    size = layout.headerBytes + layout.maxPayloadBytes
    descriptor = Darwin.open(path, O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW, S_IRUSR | S_IWUSR)
    guard descriptor >= 0 else { throw BridgeError.system("Creating the frame slot", errno) }
    guard ftruncate(descriptor, off_t(size)) == 0 else {
      let failure = errno
      Darwin.close(descriptor)
      throw BridgeError.system("Sizing the frame slot", failure)
    }
    let mapped = mmap(nil, size, PROT_READ | PROT_WRITE, MAP_SHARED, descriptor, 0)
    guard mapped != MAP_FAILED, let mapped else {
      let failure = errno
      Darwin.close(descriptor)
      throw BridgeError.system("Mapping the frame slot", failure)
    }
    memory = mapped
  }

  public func publish(_ frame: CapturedFrame) throws {
    guard let memory, descriptor >= 0 else { throw BridgeError.invalid("The frame slot is closed") }
    try frame.header.validate(layout)
    guard frame.pixels.count == frame.header.stride * frame.header.height else {
      throw BridgeError.invalid("The native frame payload does not match its geometry")
    }
    var lock = flock(
      l_start: 0, l_len: 0, l_pid: 0, l_type: Int16(F_WRLCK), l_whence: Int16(SEEK_SET))
    if fcntl(descriptor, F_SETLK, &lock) < 0 {
      if errno == EACCES || errno == EAGAIN { return }
      throw BridgeError.system("Locking the frame slot", errno)
    }
    do {
      // A killed writer must leave an invalid slot, never an old header over half-new pixels.
      memset(memory, 0, layout.headerBytes)
      frame.pixels.withUnsafeBytes { bytes in
        if let base = bytes.baseAddress { memcpy(memory + layout.headerBytes, base, bytes.count) }
      }
      var header = frame.header
      header.writtenNanos = DispatchTime.now().uptimeNanoseconds
      let encoded = try header.encode(layout: layout)
      encoded.withUnsafeBytes { bytes in
        if let base = bytes.baseAddress { memcpy(memory, base, bytes.count) }
      }
    } catch {
      lock.l_type = Int16(F_UNLCK)
      _ = fcntl(descriptor, F_SETLK, &lock)
      throw error
    }
    lock.l_type = Int16(F_UNLCK)
    guard fcntl(descriptor, F_SETLK, &lock) == 0 else {
      throw BridgeError.system("Unlocking the frame slot", errno)
    }
  }

  public func close() throws {
    guard descriptor >= 0 else { return }
    let fd = descriptor
    descriptor = -1
    let mapped = memory
    memory = nil
    let unmapped = mapped.map { munmap($0, size) } ?? 0
    let unmapError = errno
    let closed = Darwin.close(fd)
    guard unmapped == 0 else { throw BridgeError.system("Unmapping the frame slot", unmapError) }
    guard closed == 0 else { throw BridgeError.system("Closing the frame slot", errno) }
  }
}
