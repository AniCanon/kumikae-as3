#include "time_limit.h"

#include "stack.h"

#include <algorithm>

namespace kas3 {
namespace rt {

void TimeLimit::set(uint32_t p_milliseconds) {
	if (thread_.joinable()) {
		{
			const std::lock_guard<std::mutex> lock(mutex_);
			ending_ = true;
		}
		wake_.notify_all();
		thread_.join();
		ending_ = false;
	}
	limit_ = p_milliseconds;
	if (limit_) {
		// Sixteen looks in the limit's length: a call is stopped within a sixteenth of its time after it.
		period_ = std::chrono::milliseconds(std::clamp<uint32_t>(limit_ / 16, 1, 250));
		thread_ = std::thread([this] { watch(); });
	}
}

void TimeLimit::watch() {
	std::unique_lock<std::mutex> lock(mutex_);
	uint64_t seen = call_.load(std::memory_order_relaxed);
	while (!ending_) {
		const auto next = std::chrono::steady_clock::now() + period_;
		if (wake_.wait_until(lock, next, [this] { return ending_; })) {
			break;
		}
		// The same call as a whole period ago: it is asked to look at the clock, now and at each look
		// after, for as long as it runs. With no call running nobody looks, and the next call finds the
		// alarm set for one that has ended.
		const uint64_t now = call_.load(std::memory_order_relaxed);
		if (now == seen) {
			asked_.store(now, std::memory_order_relaxed);
			context_.stack_floor.store(UINTPTR_MAX, std::memory_order_relaxed);
			context_.alarm.store(true, std::memory_order_relaxed);
		}
		seen = now;
	}
}

bool TimeLimit::overdue() {
	// The thread may set both again while this runs: the call then looks once more, and finds the same.
	context_.alarm.store(false, std::memory_order_relaxed);
	context_.stack_floor.store(vm::stack_floor(), std::memory_order_relaxed);
	const uint64_t call = call_.load(std::memory_order_relaxed);
	if (!limit_ || up || context_.failing || asked_.load(std::memory_order_relaxed) != call) {
		return false;
	}
	const double now = clock_.elapsed();
	if (timed_ != call) {
		// Its first alarm: the call has run for a period at least, which is counted as its time so far.
		timed_ = call;
		began_ = now - static_cast<double>(period_.count());
		return false;
	}
	return now - began_ > limit_;
}

} // namespace rt
} // namespace kas3
