#include "interpreter_internal.h"

#include <cstdlib>

namespace kas3 {
namespace vm {

using ir::Function;
using ir::Kind;
using ir::NONE;

// "\tat Main/update()[Main.as:12]"
// What an Error's getStackTrace() gives: "Name: message", then a line for each frame, innermost first,
// from p_skip frames below the top. Null if there is no memory for it.
Str *Interpreter::stack_text(const Object *p_error, size_t p_skip) {
	std::string text = error_heading(p_error);
	size_t lines = 0;
	for (size_t i = frames_.size() > p_skip ? frames_.size() - p_skip : 0; i-- > 0 && lines < MAX_TRACE; lines++) {
		text += "\n" + site(frames_[i]);
	}
	return new_string(text);
}

// A frame that an error leaves, added to its trace; the first one is where it was thrown.
void Interpreter::traced_frame(const Frame &p_frame, std::string *r_trace, size_t *r_lines, Site *r_site) const {
	if (p_frame.function == module_.library_function) {
		return; // The library's own frame, which is no place in the program.
	}
	if (*r_lines == 0) {
		const Function &function = module_.functions[p_frame.function];
		r_site->function = function.name + "()";
		r_site->file = function.file < module_.files.size() ? module_.files[function.file] : "?";
		r_site->line = (int32_t)line_at((size_t)(&p_frame - frames_.data()));
	}
	if (*r_lines < MAX_TRACE) {
		*r_trace += site(p_frame) + "\n";
	}
	(*r_lines)++;
}

std::string Interpreter::site(const Frame &p_frame) const {
	const Function &function = module_.functions[p_frame.function];
	size_t index = (size_t)(&p_frame - frames_.data());
	std::string file = function.file < module_.files.size() ? module_.files[function.file] : "?";
	return "\tat " + function.name + "()[" + file + ":" + std::to_string(line_at(index)) + "]";
}

// Starts an error on its way out: an object of the class with the message.
void Interpreter::raise_null() {
	library_.raise_null();
}

void Interpreter::raise(uint32_t p_class, const std::string &p_message) {
	if (failing_) {
		return;
	}
	// Not text(): with no memory, it would raise again. Then the error says that memory ran out.
	Str *message = new_string(p_message);
	if (!message && memory_text_) {
		retain(memory_text_);
		message = memory_text_;
	}
	throw_new(p_class, message, p_message);
}

// Error #1000, with nothing more allocated than the error object itself.
void Interpreter::stack_overflow() {
	library_.raise_overflow();
}

void Interpreter::out_of_memory() {
	if (failing_) {
		return;
	}
	retain(memory_text_);
	throw_new(module_.error_class, memory_text_, OUT_OF_MEMORY);
}

// p_message is the error's own, or null; p_words are what is said if not even the error can be made.
void Interpreter::throw_new(uint32_t p_class, Str *p_message, std::string_view p_words) {
	if (Object *error = new_error(heap_, p_class, p_message)) {
		own_field(error, ir::ERROR_STACK).p = stack_text(error, 0);
		pending_ = any::object(error);
		failing_ = true;
		return;
	}
	trace_.assign(p_words);
	trace_ += "\n";
	stopped_ = true; // Nothing to throw with: stop.
}

// A finally that catches again replaces what it kept: one left behind (the finally went out by `break`,
// `continue` or a throw of its own) is never taken by another, and never piles up.
void Interpreter::keep_trace(Kept p_kept) {
	for (size_t i = kept_traces_.size(); i-- > 0 && kept_traces_[i].depth == p_kept.depth;) {
		if (kept_traces_[i].reg == p_kept.reg) {
			kept_traces_[i] = std::move(p_kept);
			return;
		}
	}
	kept_traces_.push_back(std::move(p_kept));
}

void Interpreter::take_trace(uint32_t p_reg) {
	for (size_t i = kept_traces_.size(); i-- > 0 && kept_traces_[i].depth == frames_.size();) {
		if (kept_traces_[i].reg == p_reg) {
			trace_ = std::move(kept_traces_[i].trace);
			trace_lines_ = kept_traces_[i].lines;
			thrown_at_ = std::move(kept_traces_[i].site);
			kept_traces_.erase(kept_traces_.begin() + static_cast<std::ptrdiff_t>(i));
			frames_.back().traced = true; // Its line, where the error was thrown, is in.
			return;
		}
	}
}

// Catches the error, or leaves frames until p_depth remain, adding each to the stack trace.
bool Interpreter::catch_error(size_t p_depth) {
	bool innermost = true;
	while (frames_.size() > p_depth) {
		Frame &frame = frames_.back();
		if (!innermost && frame.pc > 0) {
			frame.pc--; // A caller: at its call, as for the trace.
		}
		innermost = false;
		const Function &function = module_.functions[frame.function];
		for (const ir::Handler &handler : function.handlers) {
			if (!failing_ || stopped_ || frame.pc < handler.start || frame.pc >= handler.end ||
					(handler.type != NONE && !any_is(pending_, handler.type))) {
				continue;
			}
			// Caught: the error goes to the catch's variable, as its kind holds it, and is no longer on
			// its way out. A finally's catch keeps the trace, this frame's line included, for when it
			// throws the error again.
			if (handler.rethrows) {
				std::string trace = trace_;
				size_t lines = trace_lines_;
				Site thrown = thrown_at_;
				if (!frame.traced) {
					traced_frame(frame, &trace, &lines, &thrown);
				}
				keep_trace(Kept{ frames_.size(), handler.reg, std::move(trace), lines, std::move(thrown) });
			}
			frame.traced = false;
			const Kind kind = function.registers[handler.reg];
			const Slot error = pending_;
			pending_.bits = 0;
			failing_ = false;
			trace_.clear();
			trace_lines_ = 0;
			Slot caught;
			caught.bits = 0;
			const bool converted = from_any(error, kind, NONE, &caught);
			drop_any(error);
			if (converted) {
				put(kind, stack_[frame.base + handler.reg], caught);
			}
			frame.pc = handler.target;
			return true;
		}
		leave_failed_frame();
	}
	return false;
}

void Interpreter::leave_failed_frame() {
	// A class whose static fields were being set when the error came is not ready: the next use of it
	// sets them again, and fails again, where carrying on would run with fields that were never set.
	const Function &leaving = module_.functions[frames_.back().function];
	if (leaving.owner != NONE && module_.classes[leaving.owner].static_init == frames_.back().function) {
		forget_class(leaving.owner);
	}
	// A runaway recursion has thousands of frames: the first few and a count say as much. A frame whose
	// line came with an error thrown again is in already.
	if (!frames_.back().traced) {
		traced_frame(frames_.back(), &trace_, &trace_lines_, &thrown_at_);
	}
	pop();
}

// The error carried back from a call the engine made: on its way out again, from where it was, in place of
// any the instruction raised of its own once the call failed. Stopped again if it stops the run (#1502).
void Interpreter::raise_carried() {
	if (failing_) {
		drop_any(pending_);
	}
	carried_.held = false;
	pending_ = carried_.error;
	failing_ = true;
	stopped_ = carried_.stopped;
	trace_ = std::move(carried_.trace);
	trace_lines_ = carried_.lines;
	thrown_at_ = carried_.at;
	carried_.error.bits = 0;
}

bool Interpreter::take_carried(NativeError *r_error) {
	// A call made within a running instruction leaves its error as that instruction's own, not carried.
	if (!carried_.held && failing_ && !stopped_) {
		r_error->error = pending_;
		r_error->trace = std::move(trace_);
		r_error->lines = trace_lines_;
		r_error->function = std::move(thrown_at_.function);
		r_error->file = std::move(thrown_at_.file);
		r_error->line = thrown_at_.line;
		pending_.bits = 0;
		failing_ = false;
		trace_.clear();
		trace_lines_ = 0;
		return true;
	}
	if (!carried_.held || carried_.stopped) {
		return false;
	}
	r_error->error = carried_.error;
	r_error->trace = std::move(carried_.trace);
	r_error->lines = carried_.lines;
	r_error->function = std::move(carried_.at.function);
	r_error->file = std::move(carried_.at.file);
	r_error->line = carried_.at.line;
	carried_ = Carried();
	stopped_ = false; // Set so that the instruction beneath would end and raise it: there is none.
	return true;
}

void Interpreter::report_carried() {
	const Slot error = pending_;
	const bool failing = failing_;
	std::string trace = std::move(trace_);
	const size_t lines = trace_lines_;
	const Site at = thrown_at_;
	const bool stopped = stopped_;
	failing_ = false; // So that raise_carried() does not let go of it.
	raise_carried();
	report();
	pending_ = error;
	failing_ = failing;
	trace_ = std::move(trace);
	trace_lines_ = lines;
	thrown_at_ = at;
	stopped_ = stopped;
}

std::string Interpreter::error_text() {
	if (any::tag(pending_) == AnyTag::Object) {
		return object_text(static_cast<Object *>(any::pointer(pending_)));
	}
	if (const std::optional<std::string> plain = any::text(pending_)) {
		return *plain;
	}
	// A Godot value or object: Godot's own text for it.
	Str *words = host_.handles.text(any::pointer(pending_));
	std::string out = words ? std::string(words->view()) : std::string(OUT_OF_MEMORY);
	if (release(words)) {
		free_string(words);
	}
	return out;
}

// Gives the host the error that ended an entry, and forgets it.
void Interpreter::report() {
	if (!(halted_ && !failing_)) { // Stopped on request: there is no error to report.
		if (trace_lines_ > MAX_TRACE) {
			trace_ += "\t... " + std::to_string(trace_lines_ - MAX_TRACE) + " more\n";
		}
		std::string text = failing_ ? error_text() + "\n" + trace_ : trace_;
		while (!text.empty() && text.back() == '\n') {
			text.pop_back();
		}
		if (trace_lines_ > 0) {
			host_.output.error_at(text, thrown_at_.function, thrown_at_.file, thrown_at_.line);
		} else {
			host_.output.error(text);
		}
	}
	if (failing_) {
		drop_any(pending_);
	}
	pending_.bits = 0;
	failing_ = false;
	trace_.clear();
	trace_lines_ = 0;
}

Interpreter::Left Interpreter::take_error(bool p_carried) {
	Left left;
	if (p_carried && carried_.held) {
		raise_carried();
	}
	if (failing_) {
		left.error = true;
		left.value = pending_;
		pending_.bits = 0;
		failing_ = false;
		trace_.clear();
		trace_lines_ = 0;
	} else if (stopped_ && !carried_.held) {
		report();
		stopped_ = false;
		left.stopped = true;
	}
	return left;
}

} // namespace vm
} // namespace kas3
