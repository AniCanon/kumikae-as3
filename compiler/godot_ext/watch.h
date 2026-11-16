// The rule the watch of a project's files follows, apart from Godot and from any clock: the times come
// from whoever asks (godot_ext/sources.cpp gives the system's; tests/test_watch.cpp its own).
#pragma once

#include <cstdint>

namespace kas3 {

// A file or a folder as it was when last read: the time it was written (in whole seconds, as the file
// system gives it), what it held as one number, and when that was read.
struct Seen {
	bool read = false;
	uint64_t written = 0;
	uint64_t held = 0;
	uint64_t looked = 0;
};

// Whether what it holds must be read again, with its time now p_written. Two writes within one second have
// one time, so the time alone does not say it is as it was: a read made in that second, or in the one
// after (the two clocks need not agree to the second), may have come before another write of that time.
// A read made later than that has seen all there can be under that time, however long ago the last look
// was: nothing here asks what the time is now, so a watch that is kept from looking misses nothing.
constexpr bool read_again(const Seen &p_seen, uint64_t p_written) {
	return !p_seen.read || p_seen.written != p_written || p_seen.looked <= p_written + 1;
}

// What it holds now: read (p_read gives it as one number) only where read_again() says so.
template <typename Read>
uint64_t look(Seen &r_seen, uint64_t p_written, uint64_t p_now, Read p_read) {
	if (read_again(r_seen, p_written)) {
		r_seen = Seen{ true, p_written, p_read(), p_now };
	}
	return r_seen.held;
}

} // namespace kas3
