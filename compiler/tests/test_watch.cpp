// The watch of a project's files (godot_ext/watch.h), with a clock and a file of the test's own: a save is
// never missed, however long the watch takes to look again, and a file that can't have changed is not read.
#include "check.h"

#include "../godot_ext/watch.h"

using namespace kas3;

namespace {

// A file whose time is in whole seconds, as a file system gives it.
struct File {
	uint64_t written = 0;
	uint64_t content = 0;
	int reads = 0;
	void save(uint64_t p_second, uint64_t p_content) {
		written = p_second;
		content = p_content;
	}
};

uint64_t looked_at(Seen &r_seen, File &r_file, uint64_t p_now) {
	return look(r_seen, r_file.written, p_now, [&r_file]() {
		r_file.reads++;
		return r_file.content;
	});
}

} // namespace

// Two saves in one second, and the watch (the machine being busy) looks again only seconds later.
static void test_second_save_in_one_second() {
	File file;
	Seen seen;
	file.save(100, 1);
	CHECK_EQ(looked_at(seen, file, 100), (uint64_t)1);
	file.save(100, 2); // The same second: the same time.
	CHECK_EQ(looked_at(seen, file, 105), (uint64_t)2);
	// And the other way about: saved, looked at late, never saved again.
	File other;
	Seen other_seen;
	other.save(200, 7);
	CHECK_EQ(looked_at(other_seen, other, 209), (uint64_t)7);
	CHECK_EQ(looked_at(other_seen, other, 210), (uint64_t)7);
}

// A look within the file's own second, or the one after, may have come before another write with that
// time: the next look reads again. A look later than that has seen all there can be under that time.
static void test_reads_only_while_it_may_change() {
	File file;
	Seen seen;
	file.save(100, 1);
	looked_at(seen, file, 100);
	looked_at(seen, file, 100);
	looked_at(seen, file, 101);
	CHECK_EQ(file.reads, 3);
	looked_at(seen, file, 102); // The look at 101 may have come before a write at 100.9: read once more.
	CHECK_EQ(file.reads, 4);
	for (uint64_t now = 102; now < 140; now++) {
		looked_at(seen, file, now);
	}
	CHECK_EQ(file.reads, 4);
	// What an old file holds is the same number whenever it is first looked at: the signature of the
	// project does not depend on when the watch began.
	File old;
	Seen old_seen;
	old.save(50, 9);
	CHECK_EQ(looked_at(old_seen, old, 100), (uint64_t)9);
	CHECK_EQ(looked_at(old_seen, old, 101), (uint64_t)9);
	CHECK_EQ(old.reads, 1);
	// A new time is a new file to read, once it is old too.
	old.save(120, 10);
	CHECK_EQ(looked_at(old_seen, old, 130), (uint64_t)10);
	CHECK_EQ(looked_at(old_seen, old, 131), (uint64_t)10);
	CHECK_EQ(old.reads, 2);
	// A clock that steps back (the system's was set) never leaves a file unread.
	File stepped;
	Seen stepped_seen;
	stepped.save(300, 1);
	looked_at(stepped_seen, stepped, 290);
	stepped.save(300, 2);
	CHECK_EQ(looked_at(stepped_seen, stepped, 295), (uint64_t)2);
}

int main() {
	test_second_save_in_one_second();
	test_reads_only_while_it_may_change();
	return check::finish();
}
