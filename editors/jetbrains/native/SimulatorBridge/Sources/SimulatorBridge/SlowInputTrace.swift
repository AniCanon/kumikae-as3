import Foundation
import os

struct InputTrace: Codable, Sendable {
  let id: Int
  let receivedNanos: UInt64
  var actorNanos: UInt64?
  var barrierNanos: UInt64?
  var callbackNanos: UInt64?
  var inputReturnedNanos: UInt64?
}

enum InputTracePhase: Sendable { case actor, barrier, callback }

final class SlowInputTrace: Sendable {
  struct Snapshot: Codable, Sendable { let inputs: [InputTrace]; let dropped: Int }
  private struct State {
    var active: InputTrace?
    var completed: [InputTrace] = []
    var dropped = 0
  }
  private let state = OSAllocatedUnfairLock(initialState: State())
  func begin(id: Int, at: UInt64) {
    state.withLock { $0.active = InputTrace(id: id, receivedNanos: at) }
  }
  func mark(id: Int, phase: InputTracePhase, at: UInt64) {
    state.withLock { state in
      guard state.active?.id == id else { return }
      switch phase {
      case .actor: state.active?.actorNanos = at
      case .barrier: state.active?.barrierNanos = at
      case .callback: state.active?.callbackNanos = at
      }
    }
  }
  func finish(id: Int, at: UInt64) {
    state.withLock { state in
      guard var active = state.active, active.id == id else { return }
      state.active = nil
      guard at >= active.receivedNanos, at - active.receivedNanos >= 10_000_000 else { return }
      active.inputReturnedNanos = at
      if state.completed.count < 64 { state.completed.append(active) }
      else { state.dropped += 1 }
    }
  }
  func drain() -> Snapshot {
    state.withLock { state in
      let snapshot = Snapshot(inputs: state.completed, dropped: state.dropped)
      state.completed.removeAll(keepingCapacity: true)
      state.dropped = 0
      return snapshot
    }
  }
}
