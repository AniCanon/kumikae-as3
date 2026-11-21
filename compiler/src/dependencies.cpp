#include "dependencies.h"

#include <algorithm>
#include <cstring>
#include <functional>
#include <tuple>

namespace kas3 {

uint64_t name_hash(std::string_view p_name) {
	// Eight bytes at a time, each mixed in by a multiply: the same name gives the same key in every file.
	// Two names that collide only make a file checked again that need not be.
	uint64_t hash = 0x9E3779B97F4A7C15ull ^ p_name.size();
	size_t at = 0;
	for (; at + 8 <= p_name.size(); at += 8) {
		uint64_t word;
		std::memcpy(&word, p_name.data() + at, 8);
		hash = (hash ^ word) * 0xFF51AFD7ED558CCDull;
		hash ^= hash >> 32;
	}
	uint64_t tail = 0;
	if (at < p_name.size()) {
		std::memcpy(&tail, p_name.data() + at, p_name.size() - at);
	}
	hash = (hash ^ tail) * 0xC4CEB9FE1A85EC53ull;
	return hash ^ (hash >> 29);
}

bool Dependency::operator<(const Dependency &p_other) const {
	return std::tie(kind, subject, name) < std::tie(p_other.kind, p_other.subject, p_other.name);
}

size_t DependencyHash::operator()(const Dependency &p_key) const noexcept {
	const uint64_t subject = reinterpret_cast<uintptr_t>(p_key.subject);
	return (size_t)((subject * 0x9E3779B97F4A7C15ull) ^ (p_key.name + (uint64_t)p_key.kind * 0xC2B2AE3D27D4EB4Full));
}

void DependencyIndex::set(uint32_t p_unit, Part p_part, std::vector<Dependency> p_keys) {
	for (std::vector<uint32_t> &epochs : epochs_) {
		if (epochs.size() <= p_unit) {
			epochs.resize(p_unit + 1, 0);
		}
	}
	for (std::vector<size_t> &counts : counts_) {
		if (counts.size() <= p_unit) {
			counts.resize(p_unit + 1, 0);
		}
	}
	for (std::vector<Digest> &digests : digests_) {
		if (digests.size() <= p_unit) {
			digests.resize(p_unit + 1);
		}
	}
	std::sort(p_keys.begin(), p_keys.end());
	p_keys.erase(std::unique(p_keys.begin(), p_keys.end()), p_keys.end());
	// Most checks of a file read what the last one read: the list in the index then stays as it is. Two
	// independent 64-bit sums tell lists apart.
	Digest digest{ p_keys.size(), 0x9E3779B97F4A7C15ull };
	for (const Dependency &key : p_keys) {
		const uint64_t hash = DependencyHash()(key);
		digest.first = (digest.first ^ hash) * 0x100000001B3ull;
		digest.second = (digest.second + (hash ^ (hash >> 31))) * 0xFF51AFD7ED558CCDull;
	}
	if (epochs_[p_part][p_unit] != 0 && digests_[p_part][p_unit] == digest) {
		return;
	}
	digests_[p_part][p_unit] = digest;
	const uint32_t epoch = ++epochs_[p_part][p_unit];
	live_ -= counts_[p_part][p_unit];
	for (const Dependency &key : p_keys) {
		readers_[key].push_back({ p_unit, epoch, p_part });
	}
	entries_ += p_keys.size();
	live_ += p_keys.size();
	counts_[p_part][p_unit] = p_keys.size();
	// Replaced lists stay in the index until a key is looked up, or until they outnumber the live ones.
	if (entries_ > 4096 && entries_ > 3 * live_) {
		compact();
	}
}

void DependencyIndex::readers(const Dependency &p_key, std::vector<uint32_t> &r_declarations, std::vector<uint32_t> &r_bodies) {
	auto found = readers_.find(p_key);
	if (found == readers_.end()) {
		return;
	}
	std::vector<Reader> &list = found->second;
	size_t kept = 0;
	for (const Reader &reader : list) {
		if (epochs_[reader.part][reader.unit] == reader.epoch) {
			(reader.part == Declarations ? r_declarations : r_bodies).push_back(reader.unit);
			list[kept++] = reader;
		}
	}
	entries_ -= list.size() - kept;
	list.resize(kept);
	if (list.empty()) {
		readers_.erase(found);
	}
}

void DependencyIndex::compact() {
	for (auto entry = readers_.begin(); entry != readers_.end();) {
		std::vector<Reader> &list = entry->second;
		list.erase(std::remove_if(list.begin(), list.end(), [&](const Reader &p_reader) {
			return epochs_[p_reader.part][p_reader.unit] != p_reader.epoch;
		}),
				list.end());
		entry = list.empty() ? readers_.erase(entry) : std::next(entry);
	}
	entries_ = live_;
}

} // namespace kas3
