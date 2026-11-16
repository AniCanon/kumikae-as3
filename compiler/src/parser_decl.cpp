// Packages, classes, interfaces, members and metadata.
#include "parser.h"

namespace kas3 {

namespace {

struct Restore {
	bool &flag;
	bool saved;
	Restore(bool &p_flag, bool p_value) :
			flag(p_flag), saved(p_flag) {
		flag = p_value;
	}
	~Restore() { flag = saved; }
};

const uint16_t ACCESS_BITS = Modifiers::Public | Modifiers::Private | Modifiers::Protected | Modifiers::Internal;

} // namespace

// Zero or more `[Name(args)]` tags.
List<Metadata> Parser::parse_metadata_tags() {
	std::vector<Metadata> tags;
	while (at(TokenKind::LBracket)) {
		Metadata tag;
		tag.offset = advance().offset;
		if (!at(TokenKind::Identifier)) {
			error_expected("a metadata name");
			break;
		}
		tag.name = text(advance());
		std::vector<MetadataArg> args;
		if (at(TokenKind::LParen)) {
			Token open_paren = advance();
			while (!at(TokenKind::RParen) && !at(TokenKind::EndOfFile)) {
				MetadataArg arg;
				auto value = [this](std::string_view *r_value, bool *r_is_string) {
					*r_is_string = false;
					if (at(TokenKind::String)) {
						std::string_view quoted = text(advance());
						*r_value = quoted.substr(1, quoted.size() - 2);
						*r_is_string = true;
						return true;
					}
					if (at(TokenKind::Number) || is_identifier_like(current_.kind)) {
						*r_value = text(advance());
						return true;
					}
					error_expected("a metadata value");
					return false;
				};
				if (!value(&arg.value, &arg.value_is_string)) {
					break;
				}
				if (accept(TokenKind::Assign)) {
					arg.key = arg.value;
					if (!value(&arg.value, &arg.value_is_string)) {
						break;
					}
				}
				args.push_back(arg);
				if (!accept(TokenKind::Comma)) {
					break;
				}
			}
			expect_close(TokenKind::RParen, open_paren);
		}
		tag.args = to_list(args);
		expect(TokenKind::RBracket, "']'");
		tag.end = previous_end_;
		tags.push_back(tag);
	}
	return to_list(tags);
}

// Access words are reserved; `static final override dynamic native` are ordinary identifiers with a meaning
// only here. Which modifiers suit which definition is checked later, with the types.
// True at a name that is followed by the rest of a member definition: a member put in a namespace.
bool Parser::at_namespace_attribute() {
	if (!at(TokenKind::Identifier)) {
		return false;
	}
	const Token &next = peek2();
	switch (next.kind) {
		case TokenKind::KwVar:
		case TokenKind::KwConst:
		case TokenKind::KwFunction:
		case TokenKind::KwPublic:
		case TokenKind::KwPrivate:
		case TokenKind::KwProtected:
		case TokenKind::KwInternal:
			return true;
		case TokenKind::Identifier: {
			std::string_view word = text(next);
			return word == "static" || word == "final" || word == "override" || word == "native";
		}
		default:
			return false;
	}
}

Modifiers Parser::parse_modifiers() {
	Modifiers modifiers;
	for (;;) {
		uint16_t bit = 0;
		switch (current_.kind) {
			case TokenKind::KwPublic:
				bit = Modifiers::Public;
				break;
			case TokenKind::KwPrivate:
				bit = Modifiers::Private;
				break;
			case TokenKind::KwProtected:
				bit = Modifiers::Protected;
				break;
			case TokenKind::KwInternal:
				bit = Modifiers::Internal;
				break;
			case TokenKind::Identifier: {
				std::string_view word = text(current_);
				if (word == "static") {
					bit = Modifiers::Static;
				} else if (word == "final") {
					bit = Modifiers::Final;
				} else if (word == "override") {
					bit = Modifiers::Override;
				} else if (word == "dynamic") {
					bit = Modifiers::Dynamic;
				} else if (word == "native") {
					bit = Modifiers::Native;
				}
			} break;
			default:
				break;
		}
		if (!bit) {
			return modifiers;
		}
		if (modifiers.bits & bit) {
			std::string message = "duplicate ";
			message += describe(current_);
			error_at(current_.offset, std::move(message), false);
		} else if ((bit & ACCESS_BITS) && (modifiers.bits & ACCESS_BITS)) {
			error_at(current_.offset, "only one access modifier is allowed", false);
		}
		modifiers.bits |= bit;
		advance();
	}
}

ClassDecl *Parser::parse_class(uint32_t p_offset, Modifiers p_modifiers, List<Metadata> p_metadata) {
	ClassDecl *decl = node<ClassDecl>(p_offset);
	decl->modifiers = p_modifiers;
	decl->metadata = p_metadata;
	advance(); // 'class'
	if (at(TokenKind::Identifier)) {
		decl->name_offset = current_.offset;
		decl->name = text(advance());
	} else {
		error_expected("a class name");
	}
	if (accept(TokenKind::KwExtends)) {
		decl->extends = parse_type();
	}
	if (accept(TokenKind::KwImplements)) {
		std::vector<TypeRef *> interfaces;
		do {
			interfaces.push_back(parse_type());
		} while (accept(TokenKind::Comma));
		decl->implements = to_list(interfaces);
	}
	std::vector<Node *> members;
	Token open = expect(TokenKind::LBrace, "'{'");
	if (open.length) {
		int level = brace_depth_;
		while (!at(TokenKind::RBrace) && !at(TokenKind::EndOfFile)) {
			uint32_t start = current_.offset;
			panic_ = false;
			if (at(TokenKind::Semicolon)) {
				advance();
			} else if (at(TokenKind::KwImport)) {
				members.push_back(parse_import());
			} else if (!parse_unsupported_statement()) {
				List<Metadata> metadata = parse_metadata_tags();
				Modifiers modifiers = parse_modifiers();
				if (at(TokenKind::KwVar) || at(TokenKind::KwConst)) {
					// A `[` that starts the next line is the next member's metadata, not an index.
					Restore brackets(bracket_ends_initializer_, true);
					VarDecl *variable = parse_variable_declaration(modifiers, metadata, true);
					variable->offset = start;
					end_statement();
					members.push_back(variable);
				} else if (at(TokenKind::KwFunction)) {
					advance();
					bool is_native = (modifiers.bits & Modifiers::Native) != 0;
					FunctionDecl *function = parse_function_rest(start, modifiers, metadata, !is_native, true, true);
					if (!function->body) {
						end_statement();
					}
					members.push_back(function);
				} else if (!panic_ && !parse_unsupported_statement()) {
					// (If the metadata or modifiers already failed, that error is the one to show.)
					if (at_namespace_attribute()) {
						error_at(current_.offset, "namespaces are not supported"); // `mx_internal function f()`
					} else {
						error_at(start, "expected a member definition");
					}
				}
			}
			if (panic_) {
				// Metadata on the next line starts another class member, not the failed declaration.
				if (!at(TokenKind::LBracket) || !current_.newline_before) {
					synchronize(level);
				}
				panic_ = false;
			}
			if (current_.offset == start && !at(TokenKind::RBrace) && !at(TokenKind::EndOfFile)) {
				advance();
			}
		}
		expect_close(TokenKind::RBrace, open);
	}
	decl->members = to_list(members);
	return finish(decl);
}

InterfaceDecl *Parser::parse_interface(uint32_t p_offset, Modifiers p_modifiers, List<Metadata> p_metadata) {
	InterfaceDecl *decl = node<InterfaceDecl>(p_offset);
	decl->modifiers = p_modifiers;
	decl->metadata = p_metadata;
	advance(); // 'interface'
	if (at(TokenKind::Identifier)) {
		decl->name_offset = current_.offset;
		decl->name = text(advance());
	} else {
		error_expected("an interface name");
	}
	if (accept(TokenKind::KwExtends)) {
		std::vector<TypeRef *> bases;
		do {
			bases.push_back(parse_type());
		} while (accept(TokenKind::Comma));
		decl->extends = to_list(bases);
	}
	std::vector<FunctionDecl *> methods;
	Token open = expect(TokenKind::LBrace, "'{'");
	if (open.length) {
		int level = brace_depth_;
		while (!at(TokenKind::RBrace) && !at(TokenKind::EndOfFile)) {
			uint32_t start = current_.offset;
			panic_ = false;
			if (at(TokenKind::Semicolon)) {
				advance();
			} else {
				List<Metadata> metadata = parse_metadata_tags();
				uint32_t modifiers_at = current_.offset;
				if (parse_modifiers().bits) {
					error_at(modifiers_at, "interface methods cannot have modifiers", false);
				}
				if (at(TokenKind::KwFunction)) {
					advance();
					uint32_t body_at = 0;
					FunctionDecl *function = parse_function_rest(start, Modifiers(), metadata, false, true, true);
					if (function->body) {
						body_at = function->body->offset;
						error_at(body_at, "interface methods cannot have a body", false);
						function->body = nullptr;
					} else {
						end_statement();
					}
					methods.push_back(function);
				} else if (!panic_) {
					error_at(current_.offset, "expected a method signature");
				}
			}
			if (panic_) {
				synchronize(level);
				panic_ = false;
			}
			if (current_.offset == start && !at(TokenKind::RBrace) && !at(TokenKind::EndOfFile)) {
				advance();
			}
		}
		expect_close(TokenKind::RBrace, open);
	}
	decl->methods = to_list(methods);
	return finish(decl);
}

PackageDecl *Parser::parse_package() {
	PackageDecl *package = node<PackageDecl>(advance().offset);
	if (at(TokenKind::Identifier)) {
		package->name = parse_dotted_name("a package name", nullptr, nullptr);
	}
	std::vector<Import *> imports;
	std::vector<Node *> definitions;
	Token open = expect(TokenKind::LBrace, "'{'");
	if (open.length) {
		package->body_offset = open.offset + open.length;
		parse_definitions(true, imports, definitions, nullptr);
		expect_close(TokenKind::RBrace, open);
	}
	package->imports = to_list(imports);
	package->definitions = to_list(definitions);
	return finish(package);
}

// The contents of a file (p_in_package false) or of a package block (true, stopping at its `}`).
void Parser::parse_definitions(bool p_in_package, std::vector<Import *> &r_imports,
		std::vector<Node *> &r_definitions, PackageDecl **r_package) {
	int level = brace_depth_;
	while (!at(TokenKind::EndOfFile)) {
		if (p_in_package && at(TokenKind::RBrace)) {
			return;
		}
		uint32_t start = current_.offset;
		panic_ = false;
		if (at(TokenKind::Semicolon)) {
			advance();
		} else if (at(TokenKind::KwPackage)) {
			if (p_in_package || !r_package || *r_package) {
				error_at(start, "only one package block is allowed in a file", false);
				parse_package();
			} else {
				*r_package = parse_package();
			}
		} else if (at(TokenKind::KwImport)) {
			r_imports.push_back(parse_import());
		} else if (!parse_unsupported_statement()) {
			List<Metadata> metadata = parse_metadata_tags();
			Modifiers modifiers = parse_modifiers();
			if (at(TokenKind::KwClass)) {
				r_definitions.push_back(parse_class(start, modifiers, metadata));
			} else if (at(TokenKind::KwInterface)) {
				r_definitions.push_back(parse_interface(start, modifiers, metadata));
			} else if (at(TokenKind::KwFunction)) {
				advance();
				bool is_native = (modifiers.bits & Modifiers::Native) != 0;
				FunctionDecl *function = parse_function_rest(start, modifiers, metadata, !is_native, false, true);
				if (!function->body) {
					end_statement();
				}
				r_definitions.push_back(function);
			} else if (at(TokenKind::KwVar) || at(TokenKind::KwConst)) {
				Restore brackets(bracket_ends_initializer_, true);
				VarDecl *variable = parse_variable_declaration(modifiers, metadata, true);
				variable->offset = start;
				end_statement();
				r_definitions.push_back(variable);
			} else if (!panic_ && !parse_unsupported_statement()) {
				error_at(start, "expected a class, interface, function or variable definition");
			}
		}
		if (panic_) {
			synchronize(level);
			panic_ = false;
		}
		if (current_.offset == start && !at(TokenKind::EndOfFile) && !(p_in_package && at(TokenKind::RBrace))) {
			advance();
		}
	}
}

Program *Parser::parse_program() {
	Program *program = node<Program>(0);
	std::vector<Import *> imports;
	std::vector<Node *> definitions;
	parse_definitions(false, imports, definitions, &program->package);
	if (aborted_) {
		// Nesting ran past a cap part-way down. What was built around that point is not within the height
		// bound, so none of it is returned: the caller gets the diagnostic and an empty program.
		program->package = nullptr;
		imports.clear();
		definitions.clear();
	}
	program->imports = to_list(imports);
	program->definitions = to_list(definitions);
	program->end = (uint32_t)source_.text().size();
	return program;
}

Program *parse(const Source &p_source, Arena &p_arena, Diagnostics &p_diagnostics) {
	Parser parser(p_source, p_arena, p_diagnostics);
	return parser.parse_program();
}

} // namespace kas3
