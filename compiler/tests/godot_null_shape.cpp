// A boxed AS3 null is normalized before it reaches the host. Exercise the host entry directly.
#include "godot_host.h"
#include "check.h"

int main() {
	CHECK(!kas3::GodotHost::shape_of(nullptr).has_value());
	return check::finish();
}
