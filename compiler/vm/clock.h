// What the time is where the program runs, for Date. A host with a clock of its own gives it (Godot's);
// any other gets the system's.
#pragma once

#include <optional>

namespace kas3 {
namespace vm {

// The system's clock that only goes forward, in milliseconds (vm/standalone_host.cpp).
double system_elapsed();

struct Clock {
	virtual ~Clock() {}
	// Milliseconds since 1970 began, in UTC.
	virtual double now() = 0;
	// How far local time is ahead of UTC at that moment, in milliseconds: the zone and its daylight saving.
	virtual double local_offset(double p_utc) = 0;
	// Milliseconds on a clock that only goes forward, from a moment of its own: how long a run has taken
	// is the difference of two (the limit on a script's time). The system's, unless a host has another.
	virtual double elapsed() { return system_elapsed(); }
};

// The system's clock and zone, from the C library (vm/standalone_host.cpp). Made with a time, now() is
// always that time, for tests: `kas3c run` takes one from KAS3_NOW, and the zone from TZ as the C
// library does.
class SystemClock final : public Clock {
	std::optional<double> fixed_;

public:
	explicit SystemClock(std::optional<double> p_now = std::nullopt) :
			fixed_(p_now) {}
	static SystemClock &instance();
	double now() override;
	double local_offset(double p_utc) override;
	// The C library's offset at that moment, which any host may use.
	static double system_offset(double p_utc);
};

} // namespace vm
} // namespace kas3
