// Binary form of a typed IR module, so a compiled program can be packed into an export and run without
// linking the AS3 front end or Godot's declaration tables.
#pragma once

#include "ir_module.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace kas3 {
namespace ir {

constexpr uint32_t BINARY_VERSION = 2;

// Encodes every field of a module into a compact little-endian byte stream.
std::vector<uint8_t> write_binary(const Module &p_module);

// Decodes a byte stream written by write_binary() into r_module. Returns an error message on failure.
std::optional<std::string> read_binary(const uint8_t *p_data, size_t p_size, Module *r_module);

} // namespace ir
} // namespace kas3
