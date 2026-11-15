#!/usr/bin/env python3
"""Writes vm/case_table.cpp: how toUpperCase() and toLowerCase() change a character.

Usage: case_table.py <path to compiler/vm/case_table.cpp>

AS3 changes case a character for a character, within the first 65,536 (a UTF-16 unit each). The mappings
are Unicode's, as the Python running this knows them: a character is mapped where Python's upper() or
lower() gives one other character of the same range. Where Unicode gives several ("ß" to "SS"), the
character stays as it is. The table is a list of runs: characters first to last, every `step` of them,
each moved by the same distance.
"""
import sys
import unicodedata


def runs(change):
    pairs = []
    for code in range(0x10000):
        if 0xD800 <= code < 0xE000:
            continue
        mapped = change(chr(code))
        if len(mapped) == 1 and ord(mapped) != code and ord(mapped) < 0x10000:
            pairs.append((code, ord(mapped) - code))
    out = []  # [first, last, distance, step]
    for code, distance in pairs:
        if out and out[-1][2] == distance:
            last = out[-1]
            step = code - last[1] if last[0] == last[1] else last[3]
            if step in (1, 2) and code - last[1] == step:
                last[1], last[3] = code, step
                continue
        out.append([code, code, distance, 1])
    return out


def table(name, entries):
    lines = ["constexpr std::array<CaseRun, %d> %s = { {" % (len(entries), name)]
    row = []
    for first, last, distance, step in entries:
        row.append("{ 0x%04X, 0x%04X, %d, %d }," % (first, last, distance, step))
        if len(row) == 3:
            lines.append("\t\t" + " ".join(row))
            row = []
    if row:
        lines.append("\t\t" + " ".join(row))
    lines.append("} };")
    return "\n".join(lines)


HEAD = """// Written by tools/case_table.py from Unicode %s, as Python knows it. Do not edit by hand.
#include "text_units.h"

#include <array>

namespace kas3 {
namespace vm {

namespace {

// Characters first to last, every `step` of them, each moved by `distance` to the other case.
struct CaseRun {
	uint16_t first;
	uint16_t last;
	int32_t distance;
	uint8_t step;
};

"""

TAIL = """

template <size_t Count>
uint32_t mapped(const std::array<CaseRun, Count> &p_runs, uint32_t p_code) {
	size_t low = 0, high = Count;
	while (low < high) {
		const size_t middle = (low + high) / 2;
		if (p_runs[middle].last < p_code) {
			low = middle + 1;
		} else {
			high = middle;
		}
	}
	if (low == Count || p_code < p_runs[low].first || (p_code - p_runs[low].first) % p_runs[low].step != 0) {
		return p_code;
	}
	return static_cast<uint32_t>(static_cast<int32_t>(p_code) + p_runs[low].distance);
}

} // namespace

uint32_t to_upper(uint32_t p_code) {
	if (p_code < 0x80) {
		return p_code >= 'a' && p_code <= 'z' ? p_code - 32 : p_code;
	}
	return mapped(UPPER, p_code);
}

uint32_t to_lower(uint32_t p_code) {
	if (p_code < 0x80) {
		return p_code >= 'A' && p_code <= 'Z' ? p_code + 32 : p_code;
	}
	return mapped(LOWER, p_code);
}

} // namespace vm
} // namespace kas3
"""

if __name__ == "__main__":
    with open(sys.argv[1], "w") as out:
        out.write(HEAD % unicodedata.unidata_version)
        out.write(table("UPPER", runs(str.upper)) + "\n\n" + table("LOWER", runs(str.lower)))
        out.write(TAIL)
