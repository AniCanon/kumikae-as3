// The time limit on a call from outside: Flash's Error #1502 for a run that goes on too long.
#include "interpreter_internal.h"

namespace kas3 {
namespace vm {

// A run that has gone on too long ends with Flash's error, which nothing catches.
void Interpreter::look_at_clock() {
	countdown_ = STATEMENTS_PER_LOOK;
	if (!time_limit_ || failing_ || stopped_ || host_.clock.elapsed() - entered_ <= time_limit_) {
		return;
	}
	raise(module_.error_class, timeout_message(time_limit_));
	stopped_ = true;
}

void Interpreter::entering() {
	if (entries_ == 0 && time_limit_) {
		entered_ = host_.clock.elapsed();
		countdown_ = STATEMENTS_PER_LOOK;
	}
}

std::string timeout_message(uint32_t p_milliseconds) {
	return std::string("Error #1502: A script has executed for longer than the ") +
			(p_milliseconds == Interpreter::FLASH_TIME_LIMIT ? "default " : "") + "timeout period of " +
			number_to_string(p_milliseconds / 1000.0) + " seconds.";
}

} // namespace vm
} // namespace kas3
