"""Write the Godot reference's brief class descriptions and first paragraphs as ASDoc."""
import re

import naming


class Documentation:
    def __init__(self, api, substitutes):
        header = api["header"]
        self.version = ("%s.%s" % (header["version_major"], header["version_minor"])
                        if "version_major" in header and "version_minor" in header else "stable")
        self.types = {entry["name"]: naming.class_name(entry["name"])
                      for group in ("classes", "builtin_classes") for entry in api[group]}
        self.types.update(substitutes)
        self.global_enums = {entry["name"] for entry in api["global_enums"]}
        self.constants = {}
        for enum in api["global_enums"]:
            names = [value["name"] for value in enum["values"]]
            for original, short in zip(names, naming.enum_values(names)):
                self.constants[original] = naming.class_name(enum["name"]) + "." + short

    def reference(self, match):
        kind, target = match.groups()
        if kind == "enum":
            enum = target.split("@GlobalScope.")[-1]
            if enum in self.global_enums:
                return naming.class_name(enum)
        owner, dot, name = target.rpartition(".")
        if owner == "@GlobalScope":
            if kind == "constant":
                return self.constants.get(name, name)
            owner = dot = ""
        if kind in ("method", "member", "signal", "param"):
            name = naming.member(name)
        return self.types.get(owner, owner) + dot + name

    def comment(self, entry, owner="", section="", indent=""):
        description = entry.get("brief_description") or entry.get("description", "")
        if not description.strip():
            return []
        paragraph, _, remainder = description.strip().partition("\n")

        # Code spans may contain literal bracketed text such as array[0] or BBCode examples.
        def prose(text):
            text = re.sub(r"\[(method|member|signal|constant|enum|param|theme_item|annotation) ([^\]]+)\]",
                          self.reference, text)
            text = re.sub(r"\[(/?(?:b|i|kbd|url)|url=[^\]]+)\]", "", text)
            text = re.sub(r"\[([\w.]+)\]", lambda m: self.types.get(m[1], m[0]), text)
            return re.sub(r"\b([Aa])n (?=Godot[A-Z])", r"\1 ", text)

        parts = re.split(r"\[code(?: skip-lint)?\](.*?)\[/code\]", paragraph)
        summary = "".join(part if index % 2 else prose(part) for index, part in enumerate(parts))
        summary = summary.replace("[lb]", "[").replace("[rb]", "]").replace("*/", "* /")
        if not owner or not (remainder or (entry.get("brief_description") and entry.get("description"))):
            return [indent + "/** " + summary + " */"]

        # Sphinx uses Godot's original spellings for the page and its anchors.
        page = owner.lower()
        url = "https://docs.godotengine.org/en/%s/classes/class_%s.html" % (self.version, page)
        if section:
            if section == "method" and entry["name"].startswith("_"):
                section = "private-method"
            url += "#" + re.sub(r"[^a-z0-9]+", "-", "class-%s-%s-%s" % (page, section, entry["name"].lower())).strip("-")
        return [indent + line for line in ["/** " + summary, " *", " * " + url, " */"]]
