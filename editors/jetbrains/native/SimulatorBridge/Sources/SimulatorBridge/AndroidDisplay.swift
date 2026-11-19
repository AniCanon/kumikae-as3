import Accelerate
import Darwin
import Foundation

/// Reads the Android Emulator's VideoShareCapturer BGRA surface. The guest owns its lifetime.
public enum AndroidDisplay {
  static func validate(width: Int, height: Int, bytes: Int, maxDimension: Int = 4096) throws {
    guard (1...maxDimension).contains(width), (1...maxDimension).contains(height),
      bytes >= 24 + width * height * 4
    else {
      throw BridgeError.invalid("Invalid Android emulator surface size")
    }
  }

  public static func isActive(handle: String) throws -> Bool {
    let result = try openDisplay(handle)
    if result.fd < 0 && result.error == ENOENT { return false }
    guard result.fd >= 0 else {
      throw BridgeError.system("Checking the emulator display", result.error)
    }
    Darwin.close(result.fd)
    return true
  }

  private static func openDisplay(_ handle: String) throws -> (fd: Int32, error: Int32) {
    guard handle.range(of: "^videmulator[0-9]+$", options: .regularExpression) != nil
    else {
      throw BridgeError.invalid("Invalid Android emulator display request")
    }
    // Darwin declares shm_open variadic. Opening an existing object takes only the two fixed arguments.
    guard let libc = dlopen(nil, RTLD_LAZY) else {
      throw BridgeError.unavailable("Cannot open libc")
    }
    defer { dlclose(libc) }
    guard let symbol = dlsym(libc, "shm_open") else {
      throw BridgeError.unavailable("Missing shm_open")
    }
    typealias OpenExisting = @convention(c) (UnsafePointer<CChar>, Int32) -> Int32
    let openExisting = unsafeBitCast(symbol, to: OpenExisting.self)
    let fd = handle.withCString { openExisting($0, O_RDONLY) }
    return (fd, errno)
  }

  private static func parentClosed() -> Bool {
    var descriptor = pollfd(fd: STDIN_FILENO, events: Int16(POLLHUP), revents: 0)
    let ready = Darwin.poll(&descriptor, 1, 0)
    return ready > 0 && (descriptor.revents & Int16(POLLHUP | POLLERR | POLLNVAL)) != 0
  }

  public static func stream(handle: String, edge: Int, slot: String) async throws {
    guard (240...1920).contains(edge) else { throw BridgeError.invalid("Invalid display size") }
    let opened = try openDisplay(handle)
    guard opened.fd >= 0 else {
      throw BridgeError.system("Opening the emulator display", opened.error)
    }
    defer { Darwin.close(opened.fd) }
    var stat = Darwin.stat()
    guard fstat(opened.fd, &stat) == 0, stat.st_size >= 24, stat.st_size <= 268_435_480 else {
      throw BridgeError.invalid("The emulator display has an invalid header")
    }
    let bytes = Int(stat.st_size)
    guard let base = mmap(nil, bytes, PROT_READ, MAP_SHARED, opened.fd, 0),
      base != MAP_FAILED
    else {
      throw BridgeError.system("Mapping the emulator display", errno)
    }
    defer { munmap(base, bytes) }
    let layout = try FrameLayout.load()
    let width = Int(base.load(as: UInt32.self))
    let height = Int(base.load(fromByteOffset: 4, as: UInt32.self))
    try validate(width: width, height: height, bytes: bytes, maxDimension: layout.maxDimension)
    let scale = min(1, Double(edge) / Double(max(width, height)))
    let scaledW = max(1, Int(Double(width) * scale))
    let scaledH = max(1, Int(Double(height) * scale))
    let sink = try MappedFrameSink(path: slot, layout: layout)
    do {
      // Signal that the slot has been sized and is ready to map.
      try FileHandle.standardOutput.write(contentsOf: Data("ready\n".utf8))
      var output = Data(count: scaledW * scaledH * 4)
      var previous: UInt32?
      while !Task.isCancelled && !parentClosed() {
        let frame = base.load(fromByteOffset: 12, as: UInt32.self)
        if frame == 0 || previous == frame {
          try await Task.sleep(for: .milliseconds(4))
          continue
        }
        let observed = DispatchTime.now().uptimeNanoseconds
        var source = vImage_Buffer(
          data: base + 24, height: UInt(height), width: UInt(width), rowBytes: width * 4)
        try output.withUnsafeMutableBytes { (data: UnsafeMutableRawBufferPointer) in
          guard let pointer = data.baseAddress else {
            throw BridgeError.invalid("Empty Android frame")
          }

          var target = vImage_Buffer(
            data: pointer, height: UInt(scaledH), width: UInt(scaledW), rowBytes: scaledW * 4)
          guard
            vImageScale_ARGB8888(&source, &target, nil, vImage_Flags(kvImageNoFlags))
              == kvImageNoError
          else {
            throw BridgeError.invalid("Cannot scale the emulator display")
          }
        }
        if base.load(fromByteOffset: 12, as: UInt32.self) != frame { continue }
        let header = FrameHeader(
          sequence: UInt64(frame), width: scaledW, height: scaledH,
          stride: scaledW * 4, observedNanos: observed, writtenNanos: observed,
          nativeWidth: width, nativeHeight: height, densityMilli: 1000)
        if try await sink.tryPublish(CapturedFrame(header: header, pixels: output)) {
          previous = frame
        }
      }
      try await sink.close()
    } catch {
      try? await sink.close()
      throw error
    }
  }
}
