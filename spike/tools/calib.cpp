// A fixed workload timed outside the engine, used to detect the machine changing speed between repetitions.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

static const int32_t LIMIT = 200000;

__attribute__((noinline)) static int32_t work(int32_t *flags) {
	for (int32_t i = 0; i < LIMIT; i++) {
		flags[i] = 1;
	}
	int32_t count = 0;
	for (int32_t i = 2; i < LIMIT; i++) {
		if (flags[i] == 1) {
			count++;
			for (int32_t j = i + i; j < LIMIT; j += i) {
				flags[j] = 0;
			}
		}
	}
	return count;
}

int main() {
	std::vector<int32_t> flags(LIMIT);
	std::vector<double> times;
	int32_t sink = 0;
	for (int run = 0; run < 2500; run++) {
		auto start = std::chrono::steady_clock::now();
		sink += work(flags.data());
		auto end = std::chrono::steady_clock::now();
		if (run >= 500) {
			times.push_back(std::chrono::duration<double, std::micro>(end - start).count());
		}
	}
	std::sort(times.begin(), times.end());
	std::printf("%.1f\n", times[times.size() / 2]);
	return sink == 0;
}
