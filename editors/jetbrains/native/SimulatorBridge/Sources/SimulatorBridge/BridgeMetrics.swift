import Foundation
import os

/// Optional bounded counters for a controlled probe; absent from ordinary runs.
public final class BridgeMetrics: Sendable {
  struct Metric: Codable, Sendable {
    var count: UInt64 = 0
    var total: UInt64 = 0
    var maximum: UInt64 = 0
  }

  let inputTrace = SlowInputTrace()
  private let values = OSAllocatedUnfairLock<[String: Metric]>(initialState: [:])
  private let file: FileHandle
  private let last = OSAllocatedUnfairLock<UInt64>(
    initialState: DispatchTime.now().uptimeNanoseconds)

  public init(path: String) throws {
    let fd = Darwin.open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, S_IRUSR | S_IWUSR)
    guard fd >= 0 else {
      throw BridgeError.system("Creating probe metrics", errno)
    }
    file = FileHandle(fileDescriptor: fd, closeOnDealloc: true)
  }

  public func record(_ name: String, _ value: UInt64 = 1) {
    values.withLock { values in
      var metric = values[name, default: Metric()]
      metric.count += 1
      metric.total += value
      metric.maximum = max(metric.maximum, value)
      values[name] = metric
    }
  }

  public func flush(force: Bool = false) throws {
    let now = DispatchTime.now().uptimeNanoseconds
    let start = last.withLock { previous -> UInt64? in
      guard force || now - previous >= 10_000_000_000 else {
        return nil
      }
      let start = previous
      previous = now
      return start
    }
    guard let start else {
      return
    }
    let snapshot = values.withLock { current in
      let snapshot = current
      current.removeAll(keepingCapacity: true)
      return snapshot
    }
    struct Window: Encodable {
      let nanos: UInt64
      let startNanos: UInt64
      let endNanos: UInt64
      let metrics: [String: Metric]
      let slow: SlowInputTrace.Snapshot
    }
    var data = try JSONEncoder().encode(
      Window(
        nanos: now - start, startNanos: start, endNanos: now, metrics: snapshot,
        slow: inputTrace.drain()))
    data.append(10)
    try file.write(contentsOf: data)
  }
}
