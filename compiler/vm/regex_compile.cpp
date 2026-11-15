#include "regex_tree.h"

namespace kas3 {
namespace vm {
namespace regex {

namespace {

constexpr uint32_t NO_REGISTER = 0xFFFFFFFFu;

// Writes a tree's program. A quantifier's turns are copies of its atom, so the code can grow past
// MAX_CODE; then nothing more is written and the pattern is refused.
class Emitter {
	const Tree &tree_;
	Program &program_;
	bool full_ = false;

	uint32_t here() const { return static_cast<uint32_t>(program_.code.size()); }
	uint32_t add(Code p_code, uint32_t p_a = 0, uint32_t p_b = 0, bool p_flag = false) {
		if (program_.code.size() >= MAX_CODE) {
			full_ = true;
			return here();
		}
		Instruction made;
		made.code = p_code;
		made.a = p_a;
		made.b = p_b;
		made.flag = p_flag;
		program_.code.push_back(made);
		return here() - 1;
	}
	// Points an instruction already written at where the code is now.
	void point(uint32_t p_at, bool p_second = false) {
		if (p_at < program_.code.size()) {
			(p_second ? program_.code[p_at].b : program_.code[p_at].a) = here();
		}
	}

	void alternation(const Node &p_node);
	void group(const Node &p_node);
	void look(const Node &p_node);
	void repeat(const Node &p_node);
	void turn(const Node &p_node, bool p_optional, uint32_t p_register);

public:
	Emitter(const Tree &p_tree, Program &r_program) :
			tree_(p_tree), program_(r_program) {}
	void emit(uint32_t p_node);
	bool finish() {
		add(Code::Match);
		return !full_;
	}
};

void Emitter::emit(uint32_t p_index) {
	if (full_) {
		return;
	}
	const Node &node = tree_.nodes[p_index];
	const bool fold = (program_.flags & IGNORE_CASE) != 0;
	switch (node.kind) {
		case Node::Empty:
			break;
		case Node::Unit:
			add(Code::Unit, fold ? canonical(static_cast<char16_t>(node.value)) : node.value, 0, fold);
			break;
		case Node::Any:
			add(Code::Any, 0, 0, (program_.flags & DOTALL) != 0);
			break;
		case Node::Set:
			add(Code::Set, node.value, 0, node.flag);
			break;
		case Node::LineStart:
		case Node::LineEnd:
			add(node.kind == Node::LineStart ? Code::LineStart : Code::LineEnd, 0, 0, (program_.flags & MULTILINE) != 0);
			break;
		case Node::Boundary:
			add(Code::Boundary, 0, 0, node.flag);
			break;
		case Node::Backref:
			add(Code::Backref, node.value, 0, fold);
			break;
		case Node::Group:
			group(node);
			break;
		case Node::Look:
			look(node);
			break;
		case Node::Concat:
			for (const uint32_t child : node.children) {
				emit(child);
			}
			break;
		case Node::Alternation:
			alternation(node);
			break;
		case Node::Repeat:
			repeat(node);
			break;
	}
}

// Each alternative but the last is tried with a way to the next one kept.
void Emitter::alternation(const Node &p_node) {
	std::vector<uint32_t> to_end;
	for (size_t i = 0; i + 1 < p_node.children.size(); i++) {
		const uint32_t split = add(Code::Split);
		point(split);
		emit(p_node.children[i]);
		to_end.push_back(add(Code::Jump));
		point(split, true);
	}
	emit(p_node.children.back());
	for (const uint32_t jump : to_end) {
		point(jump);
	}
}

void Emitter::group(const Node &p_node) {
	if (p_node.value == NO_GROUP) {
		emit(p_node.children[0]);
		return;
	}
	add(Code::Save, 2 * p_node.value);
	emit(p_node.children[0]);
	add(Code::Save, 2 * p_node.value + 1);
}

void Emitter::look(const Node &p_node) {
	const uint32_t at = add(Code::Look, 0, p_node.value);
	if (at < program_.code.size()) {
		program_.code[at].c = p_node.min;
	}
	emit(p_node.children[0]);
	add(Code::Match);
	point(at);
}

// One turn of a quantifier. Its groups start unset, as ECMAScript says (15.10.2.5). A turn beyond the
// least number that takes nothing fails, so that `(a*)*` ends.
void Emitter::turn(const Node &p_node, bool p_optional, uint32_t p_register) {
	if (p_node.end_group > p_node.first_group) {
		add(Code::Clear, 2 * p_node.first_group, 2 * p_node.end_group);
	}
	if (p_optional && p_register != NO_REGISTER) {
		add(Code::Mark, p_register);
	}
	emit(p_node.children[0]);
	if (p_optional && p_register != NO_REGISTER) {
		add(Code::Progress, p_register);
	}
}

void Emitter::repeat(const Node &p_node) {
	const bool greedy = p_node.flag;
	const uint32_t mark = can_be_empty(tree_, p_node.children[0]) ? program_.registers++ : NO_REGISTER;
	for (uint32_t i = 0; i < p_node.min && !full_; i++) {
		turn(p_node, false, mark);
	}
	// A split goes into the turn first when greedy, past it first when not.
	const auto aim = [&](uint32_t p_split, uint32_t p_into, uint32_t p_past) {
		if (p_split < program_.code.size()) {
			program_.code[p_split].a = greedy ? p_into : p_past;
			program_.code[p_split].b = greedy ? p_past : p_into;
		}
	};
	if (p_node.max == UNBOUNDED) {
		const uint32_t split = add(Code::Split);
		turn(p_node, true, mark);
		add(Code::Jump, split);
		aim(split, split + 1, here());
		return;
	}
	std::vector<uint32_t> splits;
	for (uint32_t i = p_node.min; i < p_node.max && !full_; i++) {
		splits.push_back(add(Code::Split));
		turn(p_node, true, mark);
	}
	for (const uint32_t split : splits) {
		aim(split, split + 1, here());
	}
}

} // namespace

uint8_t read_flags(std::string_view p_text) {
	uint8_t flags = 0;
	for (const char letter : p_text) {
		switch (letter) {
			case 'g':
				flags |= GLOBAL;
				break;
			case 'i':
				flags |= IGNORE_CASE;
				break;
			case 'm':
				flags |= MULTILINE;
				break;
			case 's':
				flags |= DOTALL;
				break;
			case 'x':
				flags |= EXTENDED;
				break;
			default:
				break;
		}
	}
	return flags;
}

std::string flags_text(uint8_t p_flags) {
	std::string text;
	constexpr std::pair<uint8_t, char> letters[] = { { GLOBAL, 'g' }, { IGNORE_CASE, 'i' }, { MULTILINE, 'm' }, { DOTALL, 's' }, { EXTENDED, 'x' } };
	for (const auto &[flag, letter] : letters) {
		if (p_flags & flag) {
			text += letter;
		}
	}
	return text;
}

std::optional<Program> compile(std::u16string_view p_pattern, uint8_t p_flags, std::string *r_error) {
	Program program;
	program.flags = p_flags;
	const std::optional<Tree> tree = parse(p_pattern, program, r_error);
	if (!tree) {
		return std::nullopt;
	}
	Emitter emitter(*tree, program);
	emitter.emit(tree->root);
	if (!emitter.finish()) {
		*r_error = "the pattern is too large";
		return std::nullopt;
	}
	return program;
}

} // namespace regex
} // namespace vm
} // namespace kas3
