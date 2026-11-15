import Testing

@testable import SimulatorBridge

struct SlowInputTraceTests {
  @Test func pairsStagesByCommandAndBoundsCompletedSlowTraces() throws {
    let trace = SlowInputTrace()
    trace.begin(id: 1, at: 10)
    trace.mark(id: 1, phase: .actor, at: 20)
    trace.mark(id: 1, phase: .barrier, at: 30)
    trace.mark(id: 1, phase: .callback, at: 40)
    trace.finish(id: 1, at: 20_000_010)
    let first = try #require(trace.drain().inputs.first)
    #expect(first.actorNanos == 20 && first.barrierNanos == 30 && first.callbackNanos == 40)
    #expect(first.inputReturnedNanos == 20_000_010)
    trace.begin(id: 2, at: 30_000_000)
    trace.mark(id: 1, phase: .callback, at: 30_000_001)
    trace.finish(id: 2, at: 30_000_100)
    #expect(trace.drain().inputs.isEmpty)
    for id in 3...70 {
      trace.begin(id: id, at: 0)
      trace.finish(id: id, at: 20_000_000)
    }
    let bounded = trace.drain()
    #expect(bounded.inputs.count == 64)
    #expect(bounded.dropped == 4)
    #expect(trace.drain().inputs.isEmpty)
  }
}
