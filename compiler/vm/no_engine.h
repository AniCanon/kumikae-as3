// The engine where there is none (`kas3c run`, a test that only prints): every entry fails and says so.
#pragma once

#include "engine_handles.h"
#include "engine_source.h"

namespace kas3 {
namespace vm {

class NoEngine final : public EngineHandles, public EngineSource {
	static bool refuse(std::string *r_error) {
		*r_error = "there is no engine here";
		return false;
	}

public:
	static NoEngine &instance() {
		static NoEngine none;
		return none;
	}

	void *construct(std::string_view, const Argument *, size_t, std::string *r_error) override {
		refuse(r_error);
		return nullptr;
	}
	void *collection(std::string_view, ir::Kind, const Slot *, size_t, std::string *r_error) override {
		refuse(r_error);
		return nullptr;
	}
	void *copy(void *p_value) override { return p_value; }
	void drop(void *) override {}
	bool present(void *p_value) override { return p_value != nullptr; }
	bool is_class(void *, std::string_view) override { return false; }
	Str *text(void *) override { return nullptr; }
	bool operate(std::string_view, const Argument *, size_t, const Wanted &, Slot *, std::string *r_error) override { return refuse(r_error); }
	bool index_get(void *, const Argument &, const Wanted &, Slot *, std::string *r_error) override { return refuse(r_error); }
	bool index_set(void *, const Argument &, const Argument &, std::string *r_error) override { return refuse(r_error); }
	bool constant(std::string_view, std::string_view, const Wanted &, Slot *, std::string *r_error) override { return refuse(r_error); }

	bool call(void *, std::string_view, const Argument *, size_t, const Wanted &, Slot *, std::string *r_error) override { return refuse(r_error); }
	bool get(void *, std::string_view, const Wanted &, Slot *, std::string *r_error) override { return refuse(r_error); }
	bool set(void *, std::string_view, const Argument &, std::string *r_error) override { return refuse(r_error); }
	bool member(void *, std::string_view, const Wanted &, Slot *, std::string *r_error) override { return refuse(r_error); }
	bool evaluate(std::string_view, const Wanted &, Slot *, std::string *r_error) override { return refuse(r_error); }
	void *singleton(std::string_view, std::string *r_error) override {
		refuse(r_error);
		return nullptr;
	}
	bool call_static(std::string_view, std::string_view, const Argument *, size_t, const Wanted &, Slot *, std::string *r_error) override {
		return refuse(r_error);
	}
	void *bind(Object *, uint32_t, void *, std::string *r_error) override {
		refuse(r_error);
		return nullptr;
	}
};

} // namespace vm
} // namespace kas3
