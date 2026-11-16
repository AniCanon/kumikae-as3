package app.anicanon.kumikae.as3.scene

import app.anicanon.kumikae.as3.settings.GodotConfig
import app.anicanon.kumikae.as3.settings.ProjectSettings

internal data class SceneNode(
    val path: String,
    val name: String,
    val type: String,
    val script: String?,
    val editable: Boolean,
    val offset: Int,
)

/** Reads node/resource headers without normalizing any unrelated scene text. */
internal class SceneDocument(private val source: String) {
    private val headers = Regex(
        "(?m)^\\[(gd_scene|ext_resource|sub_resource|node|connection)\\b([^\\r\\n]*)]\\r?$",
    ).findAll(source).toList()
    private val resources = linkedMapOf<String, String>()
    val nodes: List<SceneNode>

    init {
        require(headers.firstOrNull()?.groupValues?.get(1) == "gd_scene") { "Not a text Godot scene." }
        for (header in headers.filter { it.groupValues[1] == "ext_resource" }) {
            val attrs = attributes(header.groupValues[2])
            if (attrs["type"] == "Script") {
                val id = requireNotNull(attrs["id"]) { "Script resource has no id." }
                require(id !in resources) { "Duplicate script resource id: $id" }
                resources[id] = requireNotNull(attrs["path"]) { "Script resource has no path." }
            }
        }
        val inherited = headers.filter {
            it.groupValues[1] == "node"
        }.any { Regex("instance\\s*=").containsMatchIn(it.groupValues[2]) }
        nodes = headers.filter { it.groupValues[1] == "node" }.map { header ->
            val attrs = attributes(header.groupValues[2])
            val name = requireNotNull(attrs["name"]) { "Node has no name." }
            val parent = attrs["parent"]
            val path = when (parent) {
                null -> "."
                "." -> name
                else -> "$parent/$name"
            }
            val body = source.substring(header.range.last + 1, endOf(header.range.first))
            val binding = Regex("(?m)^script\\s*=\\s*ExtResource\\(\"([^\"]+)\"\\)").find(body)?.groupValues?.get(1)
            SceneNode(
                path,
                name,
                attrs["type"].orEmpty(),
                binding?.let { resources[it] ?: "Missing resource: $it" },
                !inherited && attrs["type"] != null,
                header.range.first,
            )
        }
        require(nodes.map { it.path }.distinct().size == nodes.size) { "Duplicate node paths." }
    }

    private fun endOf(start: Int) = headers.firstOrNull { it.range.first > start }?.range?.first ?: source.length

    fun attach(path: String, script: String?): String {
        val node = requireNotNull(nodes.singleOrNull { it.path == path }) { "Node not found: $path" }
        require(node.editable) { "Inherited and instanced scenes must be edited in Godot." }
        if (script != null) {
            ProjectSettings.validateResource(script)
            require(script.startsWith("res://") && script.endsWith(".as")) {
                "Choose an AS3 script inside this project."
            }
        }
        val end = endOf(node.offset)
        val header = headers.single { it.range.first == node.offset }
        val bodyStart = header.range.last + 1
        val body = source.substring(bodyStart, end)
        val existing = Regex("(?m)^script\\s*=.*(?:\\r?\\n|$)")
        require(existing.findAll(body).count() <= 1) { "Multiple script assignments; edit this node in Source." }
        val newline = if (source.contains("\r\n")) "\r\n" else "\n"
        var id = resources.entries.firstOrNull { it.value == script }?.key
        val isNew = script != null && id == null
        if (isNew) {
            var index = 1
            while (source.contains("\"kas3_$index\"")) index++
            id = "kas3_$index"
        }
        val assignment = if (script == null) "" else "script = ExtResource(\"$id\")$newline"
        val changedBody = if (existing.containsMatchIn(body)) {
            existing.replace(body, assignment)
        } else {
            body + (if (body.endsWith('\n')) "" else newline) + assignment
        }
        var result = source.replaceRange(bodyStart, end, changedBody)
        if (isNew) {
            val firstNode = nodes.first().offset
            result =
                result.substring(0, firstNode) +
                "[ext_resource type=\"Script\" path=${GodotConfig.quote(script!!)} id=\"$id\"]$newline" +
                result.substring(firstNode)
            val steps = Regex("load_steps=(\\d+)")
            val sceneHeader = headers.first().value
            val newHeader = if (steps.containsMatchIn(sceneHeader)) {
                steps.replace(sceneHeader) { "load_steps=${it.groupValues[1].toInt() + 1}" }
            } else {
                sceneHeader.replace("[gd_scene", "[gd_scene load_steps=${resources.size + 2}")
            }
            result = result.replaceRange(headers.first().range, newHeader)
        }
        return result
    }

    companion object {
        private fun attributes(header: String): Map<String, String> = Regex("(\\w+)=\"((?:\\\\.|[^\"\\\\])*)\"")
            .findAll(header).associate { it.groupValues[1] to GodotConfig.unquote("\"${it.groupValues[2]}\"") }
    }
}
