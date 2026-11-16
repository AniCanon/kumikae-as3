#include "host.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace kas3 {
namespace vm {

void StandaloneOutput::print(std::string_view p_text) {
	std::fwrite(p_text.data(), 1, p_text.size(), stdout);
	std::fputc('\n', stdout);
}

void StandaloneOutput::error(std::string_view p_text) {
	std::fflush(stdout);
	std::fwrite(p_text.data(), 1, p_text.size(), stderr);
	std::fputc('\n', stderr);
}

SystemClock &SystemClock::instance() {
	static SystemClock clock;
	return clock;
}

double SystemClock::now() {
	if (fixed_) {
		return *fixed_;
	}
	const auto since = std::chrono::system_clock::now().time_since_epoch();
	return static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(since).count());
}

double SystemClock::local_offset(double p_utc) {
	return system_offset(p_utc);
}

// The local time and UTC at the moment, as the C library breaks them down: their difference is the offset.
// A moment the library can't break down (too far from now for its calendar) is taken as UTC.
double SystemClock::system_offset(double p_utc) {
	if (!std::isfinite(p_utc) || std::fabs(p_utc) > 8.64e15) {
		return 0;
	}
	const std::time_t moment = static_cast<std::time_t>(std::floor(p_utc / 1000));
	std::tm local{}, utc{};
#ifdef _WIN32
	if (localtime_s(&local, &moment) != 0 || gmtime_s(&utc, &moment) != 0) {
		return 0;
	}
#else
	if (!localtime_r(&moment, &local) || !gmtime_r(&moment, &utc)) {
		return 0;
	}
#endif
	// The two are less than a day apart: a different year is a day either way.
	const int days = local.tm_year != utc.tm_year ? (local.tm_year > utc.tm_year ? 1 : -1) : local.tm_yday - utc.tm_yday;
	const long seconds = ((days * 24L + local.tm_hour - utc.tm_hour) * 60 + local.tm_min - utc.tm_min) * 60 + local.tm_sec - utc.tm_sec;
	return seconds * 1000.0;
}

} // namespace vm
} // namespace kas3
