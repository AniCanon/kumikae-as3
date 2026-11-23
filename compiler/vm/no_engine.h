// The engine where there is none (`kas3c run`, a test that only prints): every entry fails and says so.
#pragma once

#include "engine_handles.h"
#include "engine_source.h"

namespace kas3 {
namespace vm {

// Not final: a host with a call or two of its own (the native runtime's bridge) answers those and leaves the rest.
class NoEngine : public EngineHandles, public EngineSource {
protected:
	static bool refuse(std::string *r_error) {
		*r_error = "there is no engine here";
		return false;
	}

public:
	static NoEngine &instance() {
		static NoEngine none;
		return none;
	}

	[[nodiscard]] void *construct(std::string_view, const Argument *, size_t, std::string *r_error) override {
		refuse(r_error);
		return nullptr;
	}
	[[nodiscard]] void *attach_new(uint32_t, Object *, std::string *r_error) override {
		refuse(r_error);
		return nullptr;
	}
	[[nodiscard]] void *collection(std::string_view, const ir::Class &, const Vector *, std::string *r_error) override {
		refuse(r_error);
		return nullptr;
	}
	[[nodiscard]] void *boxed(ir::Bare, Slot, std::string *r_error) override {
		refuse(r_error);
		return nullptr;
	}
	[[nodiscard]] bool unboxed(ir::Bare, void *, Slot *) override { return false; }
	[[nodiscard]] void *copy(void *p_value) override { return p_value; }
	void drop(void *) override {}
	[[nodiscard]] bool present(void *p_value) override { return p_value != nullptr; }
	[[nodiscard]] bool is_class(void *, std::string_view) override { return false; }
	[[nodiscard]] Str *text(void *) override { return nullptr; }
	[[nodiscard]] bool keep(Object *) override { return false; }
	[[nodiscard]] Object *script_of(void *, uint32_t, bool) override { return nullptr; }
	[[nodiscard]] bool operate(int, std::string_view, const Argument *, size_t, const Wanted &, Slot *, std::string *r_error) override { return refuse(r_error); }
	[[nodiscard]] bool index_get(void *, const Argument &, const Wanted &, Slot *, std::string *r_error) override { return refuse(r_error); }
	[[nodiscard]] bool index_set(void *, const Argument &, const Argument &, std::string *r_error) override { return refuse(r_error); }
	[[nodiscard]] uint64_t node(void *) override { return 0; }
	[[nodiscard]] bool node_edges(uint64_t, EngineNode *) override { return false; }
	[[nodiscard]] std::vector<uint64_t> let_go(const std::vector<uint64_t> &) override { return {}; }
	[[nodiscard]] bool constant(std::string_view, std::string_view, const Wanted &, Slot *, std::string *r_error) override { return refuse(r_error); }

	[[nodiscard]] bool call(void *, std::string_view, const Argument *, size_t, const Wanted &, Slot *, std::string *r_error) override { return refuse(r_error); }
	[[nodiscard]] bool get(void *, std::string_view, const Wanted &, Slot *, std::string *r_error) override { return refuse(r_error); }
	[[nodiscard]] bool set(void *, std::string_view, const Argument &, std::string *r_error) override { return refuse(r_error); }
	[[nodiscard]] bool member(void *, std::string_view, const Wanted &, Slot *, std::string *r_error) override { return refuse(r_error); }
	[[nodiscard]] bool evaluate(std::string_view, const Wanted &, Slot *, std::string *r_error) override { return refuse(r_error); }
	[[nodiscard]] void *singleton(std::string_view, std::string *r_error) override {
		refuse(r_error);
		return nullptr;
	}
	[[nodiscard]] bool call_static(std::string_view, std::string_view, const Argument *, size_t, const Wanted &, Slot *, std::string *r_error) override {
		return refuse(r_error);
	}
	[[nodiscard]] void *bind(Object *, uint32_t, void *, std::string *r_error) override {
		refuse(r_error);
		return nullptr;
	}
	[[nodiscard]] void *bind_method(void *, std::string_view, std::string *r_error) override {
		refuse(r_error);
		return nullptr;
	}
};

} // namespace vm
} // namespace kas3
