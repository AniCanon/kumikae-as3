"""Write the Godot reference's brief class descriptions and first paragraphs as ASDoc."""
import re

import naming


def block(paragraphs, indent=""):
    if len(paragraphs) == 1:
        return [indent + "/** " + paragraphs[0] + " */"]
    lines = [indent + "/** " + paragraphs[0]]
    for paragraph in paragraphs[1:]:
        lines.extend([indent + " *", indent + " * " + paragraph])
    return lines + [indent + " */"]


class Documentation:
    def __init__(self, api, substitutes, accessors):
        header = api["header"]
        version = ("%s.%s" % (header["version_major"], header["version_minor"])
                   if "version_major" in header and "version_minor" in header else "stable")
        self.base_url = "https://docs.godotengine.org/en/" + version
        self.types = {entry["name"]: naming.class_name(entry["name"])
                      for group in ("classes", "builtin_classes") for entry in api[group]}
        self.types.update(substitutes)
        self.accessors = accessors
        self.parents = {entry["name"]: entry.get("inherits") for entry in api["classes"]}
        self.class_constants = {}
        for group in ("classes", "builtin_classes"):
            for entry in api[group]:
                constants = {c["name"] for c in entry.get("constants", [])}
                for enum in entry.get("enums", []):
                    constants.update(v["name"] for v in enum["values"])
                self.class_constants[entry["name"]] = constants
        self.global_enums = {entry["name"] for entry in api["global_enums"]}
        self.constants = {}
        for enum in api["global_enums"]:
            names = [value["name"] for value in enum["values"]]
            for original, short in zip(names, naming.enum_values(names)):
                self.constants[original] = naming.class_name(enum["name"]) + "." + short

    def reference(self, match, context):
        kind, target = match.groups()
        if kind == "enum":
            enum = target.split("@GlobalScope.")[-1]
            if enum in self.global_enums:
                return naming.class_name(enum)
        owner, dot, name = target.rpartition(".")
        if kind == "member" and target.startswith(("ProjectSettings.", "EditorSettings.")) and "/" in target:
            # ProjectSettings and EditorSettings keys are strings looked up verbatim.
            return '"' + target.partition(".")[2] + '"'
        if kind == "constant" and target == "@GDScript.INF":
            return "Infinity"
        scope = owner or context
        visited = set()
        while scope and scope not in visited and kind in ("constant", "method"):
            visited.add(scope)
            if kind == "constant" and name in self.class_constants.get(scope, ()):
                return naming.class_name(scope) + "." + name if owner or scope != context else name
            if kind == "method" and name in self.accessors.get(scope, {}):
                return self.types.get(owner, owner) + dot + self.accessors[scope][name]
            scope = self.parents.get(scope)
        if kind == "constant" and owner in ("", "@GlobalScope"):
            return self.constants.get(name, name)
        if owner == "@GlobalScope":
            owner = dot = ""
        if kind in ("method", "member", "signal", "param"):
            name = naming.member(name)
        return self.types.get(owner, owner) + dot + name

    def summary(self, description, owner):
        paragraph = description.strip().partition("\n")[0]
        address = ""

        def link(match):
            nonlocal address
            if match[1]:
                address = match[1].replace("$DOCS_URL", self.base_url)
                return ""
            suffix = " (%s)" % address if address else ""
            address = ""
            return suffix

        # Code spans may contain literal bracketed text such as array[0] or BBCode examples.
        def prose(text):
            text = re.sub(r"\[(method|member|signal|constant|enum|param|theme_item|annotation) ([^\]]+)\]",
                          lambda match: self.reference(match, owner), text)
            text = re.sub(r"\[url=([^\]]+)\]|\[/url\]", link, text)
            text = re.sub(r"\[(/?(?:b|i|kbd)|url)\]", "", text)
            text = re.sub(r"\[([\w.]+)\]", lambda m: self.types.get(m[1], m[0]), text)
            return re.sub(r"\b([Aa])n (?=Godot[A-Z])", r"\1 ", text)

        parts = re.split(r"\[code(?: skip-lint)?\](.*?)\[/code\]", paragraph)
        summary = "".join(part if index % 2 else prose(part) for index, part in enumerate(parts))
        return summary.replace("[lb]", "[").replace("[rb]", "]").replace("*/", "* /")

    def url(self, owner, section, name):
        # Sphinx uses Godot's original spellings for the page and its anchors.
        page = owner.lower()
        url = "%s/classes/class_%s.html" % (self.base_url, page)
        if section:
            if section == "method" and name.startswith("_"):
                section = "private-method"
            url += "#" + re.sub(r"[^a-z0-9]+", "-", "class-%s-%s-%s" % (page, section, name.lower())).strip("-")
        return url

    def comment(self, entry, owner="", section="", indent=""):
        description = entry.get("brief_description") or entry.get("description", "")
        if not description.strip():
            return []
        paragraphs = [self.summary(description, owner)]
        if owner and ("\n" in description.strip() or (entry.get("brief_description") and entry.get("description"))):
            paragraphs.append(self.url(owner, section, entry["name"]))
        return block(paragraphs, indent)
