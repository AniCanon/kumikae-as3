// How long a call from outside may run where generated code is the program: Flash's Error #1502, as the
// interpreter raises it for its own (vm/interpreter_time_limit.cpp), after the same time and in the same
// words. The interpreter counts statements and looks at the clock once in a while; generated code counts
// nothing. A thread of the host's wakes a few times in the limit's length, and where the call that was
// running at its last look is still running, it sets the context's alarm and puts the stack's floor above
// every frame. Generated code looks at the alarm where a loop goes round (overdue()), and at the floor as
// it always does where a function that can fail is entered (stack_overflow()): one of the two is in every
// loop of calls and jumps. The program's own thread then reads the clock, and the call goes on unless its
// time is up. What a running call pays is the alarm's byte read once round a loop.
#pragma once

#include "clock.h"
#include "values.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

namespace kas3 {
namespace rt {

class TimeLimit {
	Context &context_;
	vm::Clock &clock_;
	uint32_t limit_ = 0; // In milliseconds; 0 for none.
	std::chrono::milliseconds period_{ 0 }; // Between two looks of the thread's.
	// Which call from outside is running, or was the last to: counted by the program's thread as each
	// begins, and read by the other.
	std::atomic<uint64_t> call_{ 0 };
	// The call the alarm was last set for: one that has ended since is not the one the alarm is about.
	std::atomic<uint64_t> asked_{ UINT64_MAX };
	uint64_t timed_ = UINT64_MAX; // The call whose time is being kept, by the program's thread.
	double began_ = 0; // When it began, on the host's clock, as near as its first alarm says.
	std::thread thread_;
	std::mutex mutex_;
	std::condition_variable wake_;
	bool ending_ = false;
	void watch();

public:
	TimeLimit(Context &p_context, vm::Clock &p_clock) :
			context_(p_context), clock_(p_clock) {}
	~TimeLimit() { set(0); }
	TimeLimit(const TimeLimit &) = delete;
	TimeLimit &operator=(const TimeLimit &) = delete;

	// The error on its way out is the limit's: no catch takes it and no finally runs, until the call from
	// outside that it ends is over.
	bool up = false;
	// The limit, in milliseconds (0 for none): the thread is started, or stopped and waited for.
	void set(uint32_t p_milliseconds);
	uint32_t limit() const { return limit_; }
	// A call from outside begins that no other is running inside (vm::NativeCode::call_began): the time
	// kept is its own, and an error of the limit's that ended the one before is no longer on its way.
	void began() {
		call_.store(call_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
		up = false;
	}
	// Whether the thread has asked for the clock to be looked at since the last look.
	bool asked() const { return context_.alarm.load(std::memory_order_relaxed) || context_.stack_floor.load(std::memory_order_relaxed) == UINTPTR_MAX; }
	// The program's thread looks: the alarm and the floor are as they were, and if the call that is running
	// has run longer than it may, Error #1502 is on its way out (through `p_raise`, the host's own way of
	// raising an Error with a message). True if it is.
	template <typename Raise>
	bool look(const Raise &p_raise);

private:
	bool overdue();
};

template <typename Raise>
bool TimeLimit::look(const Raise &p_raise) {
	if (!overdue()) {
		return false;
	}
	up = true;
	p_raise(limit_);
	return true;
}

} // namespace rt
} // namespace kas3
