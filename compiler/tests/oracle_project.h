// Projects and edits for the incremental-checking oracle (test_incremental.cpp): a generated project with
// packages, inheritance, interfaces, accessors and imports, and random edits that a person typing could make.
// Everything is drawn from one seeded generator, so a failing sequence is repeated by its seed.
#pragma once

#include <cstdint>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace oracle {

class Random {
	std::mt19937_64 engine_;

public:
	explicit Random(uint64_t p_seed) :
			engine_(p_seed) {}
	size_t below(size_t p_count) { return p_count == 0 ? 0 : (size_t)(engine_() % p_count); }
	bool chance(int p_percent) { return (int)below(100) < p_percent; }
	template <class T>
	const T &pick(const std::vector<T> &p_items) { return p_items[below(p_items.size())]; }
};

using Files = std::map<std::string, std::string>; // Path to text.

inline const std::vector<std::string> &member_names() {
	static const std::vector<std::string> names = { "update", "value", "size", "label", "area", "next", "count",
		"step", "owner", "reset", "speed", "items", "name", "total" };
	return names;
}

inline const std::vector<std::string> &value_types() {
	static const std::vector<std::string> types = { "int", "Number", "String", "Boolean", "Object", "Array", "*" };
	return types;
}

struct Model {
	std::vector<std::string> packages = { "", "game", "game.util" };
	std::vector<std::string> interfaces; // Qualified "game.IShape".
	std::vector<std::string> classes; // Qualified, in the order declared: a class extends only earlier ones.
};

inline std::string short_name(const std::string &p_qualified) {
	const size_t dot = p_qualified.rfind('.');
	return dot == std::string::npos ? p_qualified : p_qualified.substr(dot + 1);
}

inline std::string package_of(const std::string &p_qualified) {
	const size_t dot = p_qualified.rfind('.');
	return dot == std::string::npos ? std::string() : p_qualified.substr(0, dot);
}

inline std::string path_of(const std::string &p_qualified) {
	std::string path = "/project/";
	for (char c : package_of(p_qualified)) {
		path += c == '.' ? '/' : c;
	}
	if (!package_of(p_qualified).empty()) {
		path += '/';
	}
	return path + short_name(p_qualified) + ".as";
}

inline std::string imports(Random &p_random, const Model &p_model, const std::string &p_package) {
	std::string out;
	for (const std::string &package : p_model.packages) {
		if (package.empty() || package == p_package || !p_random.chance(70)) {
			continue;
		}
		out += "\timport " + package + ".*;\n";
	}
	if (p_random.chance(30) && !p_model.classes.empty()) {
		const std::string &one = p_random.pick(p_model.classes);
		if (!package_of(one).empty()) {
			out += "\timport " + one + ";\n";
		}
	}
	return out;
}

inline std::string a_type(Random &p_random, const Model &p_model) {
	if (!p_model.classes.empty() && p_random.chance(45)) {
		return short_name(p_random.pick(p_model.classes));
	}
	if (!p_model.interfaces.empty() && p_random.chance(20)) {
		return short_name(p_random.pick(p_model.interfaces));
	}
	return p_random.pick(value_types());
}

inline std::string interface_text(Random &p_random, Model &p_model, const std::string &p_name) {
	const std::string package = package_of(p_name);
	std::string text = "package " + package + (package.empty() ? "{\n" : " {\n") + imports(p_random, p_model, package);
	text += "\tpublic interface " + short_name(p_name);
	if (!p_model.interfaces.empty() && p_random.chance(40)) {
		text += " extends " + short_name(p_random.pick(p_model.interfaces));
	}
	text += " {\n";
	for (int i = 0, count = 1 + (int)p_random.below(3); i < count; ++i) {
		const std::string &member = p_random.pick(member_names());
		const std::string type = a_type(p_random, p_model);
		switch (p_random.below(3)) {
			case 0:
				text += "\t\tfunction " + member + "(a:" + type + "):" + a_type(p_random, p_model) + ";\n";
				break;
			case 1:
				text += "\t\tfunction get " + member + "():" + type + ";\n";
				break;
			default:
				text += "\t\tfunction set " + member + "(v:" + type + "):void;\n";
				break;
		}
	}
	text += "\t}\n}\n";
	p_model.interfaces.push_back(p_name);
	return text;
}

inline std::string body(Random &p_random, const Model &p_model) {
	std::string text;
	for (int i = 0, count = 1 + (int)p_random.below(4); i < count; ++i) {
		const std::string &a = p_random.pick(member_names());
		const std::string &b = p_random.pick(member_names());
		switch (p_random.below(6)) {
			case 0:
				text += "\t\t\tvar local" + std::to_string(i) + ":" + a_type(p_random, p_model) + " = " + a + ";\n";
				break;
			case 1:
				text += "\t\t\tif (" + a + " != null) { " + a + "." + b + "(" + std::to_string(i) + "); }\n";
				break;
			case 2:
				text += "\t\t\t" + a + " = " + b + ";\n";
				break;
			case 3:
				text += "\t\t\tthis." + a + "." + b + " += 1;\n";
				break;
			case 4:
				if (!p_model.classes.empty()) {
					text += "\t\t\tvar made" + std::to_string(i) + ":" + short_name(p_random.pick(p_model.classes)) + " = new " +
							short_name(p_random.pick(p_model.classes)) + "();\n";
				}
				break;
			default:
				text += "\t\t\ttrace(" + a + ", " + b + ".length);\n";
				break;
		}
	}
	return text;
}

inline std::string class_text(Random &p_random, Model &p_model, const std::string &p_name) {
	const std::string package = package_of(p_name);
	std::string text = "package " + package + (package.empty() ? "{\n" : " {\n") + imports(p_random, p_model, package);
	text += "\tpublic class " + short_name(p_name);
	if (!p_model.classes.empty() && p_random.chance(60)) {
		text += " extends " + short_name(p_random.pick(p_model.classes));
	}
	if (!p_model.interfaces.empty() && p_random.chance(40)) {
		text += " implements " + short_name(p_random.pick(p_model.interfaces));
		if (p_random.chance(25)) {
			text += ", " + short_name(p_random.pick(p_model.interfaces));
		}
	}
	text += " {\n";
	static const std::vector<std::string> access = { "public ", "private ", "protected ", "internal ", "" };
	for (int i = 0, count = 2 + (int)p_random.below(5); i < count; ++i) {
		const std::string &member = p_random.pick(member_names());
		const std::string type = a_type(p_random, p_model);
		const std::string &visibility = p_random.pick(access);
		const std::string prefix = (p_random.chance(15) ? "override " : "") + visibility + (p_random.chance(10) ? "static " : "");
		switch (p_random.below(5)) {
			case 0:
				text += "\t\t" + prefix + (p_random.chance(20) ? "const " : "var ") + member + ":" + type + ";\n";
				break;
			case 1:
				text += "\t\t" + prefix + "function get " + member + "():" + type + " { return null; }\n";
				break;
			case 2:
				text += "\t\t" + prefix + "function set " + member + "(v:" + type + "):void {\n" + body(p_random, p_model) + "\t\t}\n";
				break;
			default:
				text += "\t\t" + prefix + "function " + member + "(a:" + type + (p_random.chance(30) ? " = null" : "") + "):" +
						a_type(p_random, p_model) + " {\n" + body(p_random, p_model) + "\t\t\treturn a;\n\t\t}\n";
				break;
		}
	}
	if (p_random.chance(50)) {
		text += "\t\tpublic function " + short_name(p_name) + "(start:int = 0) {\n" + body(p_random, p_model) + "\t\t}\n";
	}
	text += "\t}\n}\n";
	if (p_random.chance(20)) {
		text += "class Helper { public var " + p_random.pick(member_names()) + ":int; }\n";
	}
	p_model.classes.push_back(p_name);
	return text;
}

// A project of about p_classes classes and a third as many interfaces, spread over three packages.
inline Files generate(Random &p_random, int p_classes) {
	Model model;
	Files files;
	int interfaces = 0;
	int classes = 0;
	while (classes < p_classes) {
		const std::string &package = p_random.pick(model.packages);
		const std::string prefix = package.empty() ? std::string() : package + ".";
		if (interfaces * 3 < classes + 1 && p_random.chance(50)) {
			const std::string name = prefix + "I" + std::to_string(interfaces++);
			files[path_of(name)] = interface_text(p_random, model, name);
		} else {
			// Short names repeat across packages, so that two wildcard imports can make a name ambiguous.
			const int index = classes++;
			std::string name = prefix + "C" + std::to_string(index % (p_classes / 2 + 1));
			if (files.count(path_of(name))) {
				name = prefix + "C" + std::to_string(index);
			}
			files[path_of(name)] = class_text(p_random, model, name);
		}
	}
	return files;
}

} // namespace oracle
