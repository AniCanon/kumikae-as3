// The syntax tree as text: one node per line, children indented. Used by tests and `kas3c --dump-ast`.
#pragma once

#include "ast.h"
#include "source.h"

#include <string>

namespace kas3 {

std::string print_ast(const Program *p_program, const Source &p_source);
// Any single node and its subtree.
std::string print_node(const Node *p_node, const Source &p_source);

} // namespace kas3
