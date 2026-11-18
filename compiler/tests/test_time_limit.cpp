// The watch on how long a call of native code has run (runtime/time_limit.h), with a clock of the test's
// own and no program: what its thread holds, that the thread ends with the watch, and what a limit changed
// while a call runs does. The sanitizer build runs this too: a thread left behind, or one that reads a
// context that is gone, is reported there.
#include "check.h"

#include "../runtime/time_limit.h"

#include <memory>
#include <thread>

using namespace kas3;

namespace {

// A clock that goes forward when the test says.
struct Told final : vm::Clock {
	double at = 1000;
	double now() override { return at; }
	double local_offset(double) override { return 0; }
	double elapsed() override { return at; }
};

// What a host has: the context, the clock, and the watch on them.
struct Watched {
	rt::Context context;
	Told clock;
	rt::TimeLimit limit{ context, clock };
	uint32_t raised = 0; // The limit the last error was raised for.
	bool look() {
		return limit.look([this](uint32_t p_limit) { raised = p_limit; });
	}
	// Until the thread asks: it does within a few of its periods, while one call runs.
	bool asked_soon() {
		for (int i = 0; i < 4000 && !limit.asked(); i++) {
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		return limit.asked();
	}
};

void wait(int p_milliseconds) {
	std::this_thread::sleep_for(std::chrono::milliseconds(p_milliseconds));
}

} // namespace

// The thread asks a call that goes on to look at the clock; the call's time is up when the clock says so,
// and not before.
static void test_asks_and_stops() {
	Watched host;
	host.limit.set(40);
	host.limit.began();
	CHECK(host.asked_soon());
	CHECK(!host.look()); // Its first look: the time kept begins.
	host.clock.at += 30;
	CHECK(host.asked_soon());
	CHECK(!host.look());
	host.clock.at += 30;
	CHECK(host.asked_soon());
	CHECK(host.look());
	CHECK_EQ(host.raised, 40u);
	CHECK(host.limit.up);
	// The next call from outside starts with nothing on its way, and its own time.
	host.limit.began();
	CHECK(!host.limit.up);
	CHECK(host.asked_soon());
	host.clock.at += 1000;
	CHECK(!host.look());
}

// An alarm set for a call that has ended since is not the next call's.
static void test_alarm_of_an_ended_call() {
	Watched host;
	host.limit.set(20);
	host.limit.began();
	CHECK(host.asked_soon());
	CHECK(!host.look());
	host.clock.at += 500;
	CHECK(host.asked_soon());
	host.limit.began();
	const bool stopped = host.look();
	CHECK(!stopped);
	CHECK(!host.limit.up);
}

// The watch and what it watches go together, with the thread running and its alarm set: the thread has
// ended by the time they are gone. (A thread still there would be std::terminate's, or would write to
// memory that is no longer the context's.)
static void test_ends_with_its_owner() {
	for (int round = 0; round < 20; round++) {
		auto host = std::make_unique<Watched>();
		host->limit.set(1);
		host->limit.began();
		CHECK(host->asked_soon());
		host.reset();
	}
	wait(20);
	// One that never had a limit, and one whose limit was taken away, have no thread to end.
	Watched none;
	Watched taken;
	taken.limit.set(5);
	taken.limit.set(0);
	CHECK_EQ(taken.limit.limit(), 0u);
}

// The limit made shorter while a call runs: the call's time so far counts against the new one.
static void test_limit_changed_mid_call() {
	Watched host;
	host.limit.set(400);
	host.limit.began();
	CHECK(host.asked_soon());
	CHECK(!host.look());
	host.clock.at += 100;
	host.limit.set(48);
	CHECK_EQ(host.limit.limit(), 48u);
	CHECK(host.asked_soon()); // The thread that took the first one's place asks of the same call.
	CHECK(host.look());
	CHECK_EQ(host.raised, 48u);
}

// The limit made longer: the call goes on, to the new one.
static void test_limit_made_longer_mid_call() {
	Watched host;
	host.limit.set(16);
	host.limit.began();
	CHECK(host.asked_soon());
	CHECK(!host.look());
	host.limit.set(160);
	host.clock.at += 100;
	CHECK(host.asked_soon());
	CHECK(!host.look());
	host.clock.at += 100;
	CHECK(host.asked_soon());
	CHECK(host.look());
	CHECK_EQ(host.raised, 160u);
}

// The limit taken away while a call runs with the alarm set: the call's next look finds no limit, puts
// the alarm and the stack's floor back, and nothing asks again.
static void test_limit_taken_away_mid_call() {
	Watched host;
	host.limit.set(16);
	host.limit.began();
	CHECK(host.asked_soon());
	host.limit.set(0);
	host.clock.at += 100000;
	CHECK(!host.look());
	CHECK(!host.limit.asked());
	CHECK(host.context.stack_floor.load() != UINTPTR_MAX);
	wait(20);
	CHECK(!host.limit.asked());
	CHECK(!host.look());
}

// With no call running nobody is stopped: the alarm waits for a call, which finds it is not its own.
static void test_between_calls() {
	Watched host;
	host.limit.set(8);
	host.limit.began();
	CHECK(host.asked_soon());
	host.clock.at += 100000;
	host.limit.began();
	CHECK(!host.look());
	host.limit.began();
	CHECK(!host.look());
}

int main() {
	test_asks_and_stops();
	test_alarm_of_an_ended_call();
	test_ends_with_its_owner();
	test_limit_changed_mid_call();
	test_limit_made_longer_mid_call();
	test_limit_taken_away_mid_call();
	test_between_calls();
	return check::finish();
}
