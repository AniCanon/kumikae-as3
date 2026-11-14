import Foundation
import Testing

@testable import SimulatorBridge

struct MappedFrameTests {
  @Test func newestFrameWinsAndCloseRevokesWrites() async throws {
    let path = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
    defer { try? FileManager.default.removeItem(at: path) }
    let layout = try FrameLayout.load()
    let sink = try MappedFrameSink(path: path.path, layout: layout)
    var header = FrameHeader(
      sequence: 1, width: 2, height: 1, stride: 8, observedNanos: 1,
      writtenNanos: 2, nativeWidth: 2, nativeHeight: 1, densityMilli: 1000)
    try await sink.publish(CapturedFrame(header: header, pixels: Data(repeating: 11, count: 8)))
    header.sequence = 2
    try await sink.publish(CapturedFrame(header: header, pixels: Data(repeating: 22, count: 8)))
    let file = try FileHandle(forReadingFrom: path)
    defer { try? file.close() }
    let bytes = try #require(try file.read(upToCount: layout.headerBytes + 8))
    #expect(try FrameHeader.decode(bytes).sequence == 2)
    #expect(Array(bytes.suffix(8)) == Array(repeating: UInt8(22), count: 8))
    try await sink.close()
    await #expect(throws: (any Error).self) {
      try await sink.publish(CapturedFrame(header: header, pixels: Data(repeating: 33, count: 8)))
    }
    try await sink.close()
  }

  @Test func refusesExistingFile() throws {
    let path = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
    try Data("untouched".utf8).write(to: path)
    defer { try? FileManager.default.removeItem(at: path) }
    #expect(throws: (any Error).self) {
      try MappedFrameSink(path: path.path, layout: FrameLayout.load())
    }
    #expect(try String(contentsOf: path, encoding: .utf8) == "untouched")
  }
}
