import Accelerate
import Darwin
import Foundation

/// Reads the Android Emulator's VideoShareCapturer BGRA surface. The guest owns its lifetime.
public enum AndroidDisplay {
  static func validate(width: Int, height: Int, bytes: Int) throws {
    guard (1...8192).contains(width), (1...8192).contains(height),
      bytes >= 24 + width * height * 4
    else {
      throw BridgeError.invalid("Invalid Android emulator surface size")
    }
  }

  public static func isActive(handle: String) throws -> Bool {
    let fd = try openDisplay(handle)
    if fd < 0 && errno == ENOENT { return false }
    guard fd >= 0 else { throw BridgeError.system("Checking the emulator display", errno) }
    Darwin.close(fd)
    return true
  }

  private static func openDisplay(_ handle: String) throws -> Int32 {
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
    return handle.withCString { openExisting($0, O_RDONLY) }
  }

  public static func stream(handle: String, edge: Int, slot: String) async throws {
    guard (240...1920).contains(edge) else { throw BridgeError.invalid("Invalid display size") }
    let fd = try openDisplay(handle)
    guard fd >= 0 else { throw BridgeError.system("Opening the emulator display", errno) }
    defer { Darwin.close(fd) }
    var stat = Darwin.stat()
    guard fstat(fd, &stat) == 0, stat.st_size >= 24, stat.st_size <= 268_435_480 else {
      throw BridgeError.invalid("The emulator display has an invalid header")
    }
    let bytes = Int(stat.st_size)
    guard let base = mmap(nil, bytes, PROT_READ, MAP_SHARED, fd, 0), base != MAP_FAILED else {
      throw BridgeError.system("Mapping the emulator display", errno)
    }
    defer { munmap(base, bytes) }
    let width = Int(base.load(as: UInt32.self))
    let height = Int(base.load(fromByteOffset: 4, as: UInt32.self))
    try validate(width: width, height: height, bytes: bytes)
    let scale = min(1, Double(edge) / Double(max(width, height)))
    let scaledW = max(1, Int(Double(width) * scale))
    let scaledH = max(1, Int(Double(height) * scale))
    let layout = try FrameLayout.load()
    let sink = try MappedFrameSink(path: slot, layout: layout)
    // Signal that the slot has been sized and is ready to map.
    try FileHandle.standardOutput.write(contentsOf: Data("ready\n".utf8))
    var output = Data(count: scaledW * scaledH * 4)
    var previous: UInt32?
    while true {
      let frame = base.load(fromByteOffset: 12, as: UInt32.self)
      if frame == 0 || previous == frame { usleep(4000); continue }
      let observed = DispatchTime.now().uptimeNanoseconds
      previous = frame
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
      try await sink.publish(CapturedFrame(header: header, pixels: output))
    }
  }
}
