// The time limit on a call from outside: Flash's Error #1502 for a run that goes on too long.
#include "interpreter_internal.h"

namespace kas3 {
namespace vm {

// A statement is about to run: once in a while the clock is looked at, and the debugger's hook, if there is
// one, sees it and may stop the program.
void Interpreter::at_statement() {
	if (countdown_ == 0) {
		look_at_clock();
	}
	if (!hook_) {
		return;
	}
	frames_.back().pc--; // The hook sees the statement about to run.
	const bool carry_on = hook_(*this, hook_data_);
	frames_.back().pc++;
	if (time_limit_) {
		entered_ = host_.clock.elapsed(); // A debugger's stop is not the program's time.
	}
	if (!carry_on) {
		stopped_ = true;
		halted_ = true;
	}
}

// A run that has gone on too long ends with Flash's error, which nothing catches.
void Interpreter::look_at_clock() {
	countdown_ = STATEMENTS_PER_LOOK;
	if (!time_limit_ || failing_ || stopped_ || host_.clock.elapsed() - entered_ <= time_limit_) {
		return;
	}
	raise(module_.error_class, timeout_message(time_limit_));
	stopped_ = true;
}

void Interpreter::entered(bool p_timed) {
	// The clock is started by the first call from outside alone: one made inside another asks nothing.
	if (entries_ == 0 && !p_timed) {
		entering();
	}
	entries_++;
}

void Interpreter::entering() {
	if (entries_ == 0 && native_) {
		native_->call_began();
	}
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
