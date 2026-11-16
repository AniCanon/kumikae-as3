// What the lowerer keeps of the loops and `try` statements around the code it is lowering: where a `break`,
// `continue` or `return` goes, and the `finally` blocks it runs on the way.
#pragma once

#include "ast.h"
#include "ir.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace kas3 {
namespace ir {

struct Loop {
	std::vector<uint32_t> breaks; // Jumps to patch to the loop's end.
	std::vector<uint32_t> continues; // Jumps to patch to where the loop continues.
	bool is_switch = false; // A `switch`: `break` leaves it, `continue` belongs to the loop around it.
	bool is_block = false; // A labelled block: only `break label` leaves it.
	std::vector<std::string_view> labels; // The labels written before it; one statement may have several.
	size_t tries = 0; // How many `try` statements were open when it began.
};

// A way out of a `try` that has a `finally`: the finally runs, then this.
struct TryExit {
	enum Kind { Break, Continue, Return } kind = Break;
	size_t loop = 0; // For Break and Continue: the loop it leaves or goes round.
	uint32_t position = 0;
};

// A `try` statement being lowered. Its `finally` is written once; each way out sets `exit_reg` to say
// where to go after it (0 on past the try, 1 the error goes on, 2 and up one of `exits`) and jumps there.
struct TryBlock {
	Block *finally_block = nullptr;
	uint32_t exit_reg = 0;
	// The value of a `return` that leads out through this finally, kept here so that a return cancelled
	// inside the finally (by `break`, `continue` or a caught error) does not change an outer one.
	uint32_t return_reg = NONE;
	std::vector<uint32_t> to_finally; // Jumps to the finally, patched once it is written.
	std::vector<TryExit> exits;
};

} // namespace ir
} // namespace kas3
