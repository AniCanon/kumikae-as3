package app.anicanon.kumikae.as3.settings

internal data class ExportPreset(val section: String, val name: String, val platform: String) {
    val identifierKey: String? get() = when (platform) {
        "iOS" -> "application/bundle_identifier"
        "Android" -> "package/unique_name"
        else -> null
    }
    val iconKey: String? get() = when (platform) {
        "iOS" -> "icons/icon_1024x1024"
        "Android" -> "launcher_icons/main_192x192"
        else -> null
    }
    override fun toString() = "$name · $platform"
}

internal data class ProjectSettings(
    val projectText: String,
    val exportText: String,
    val name: String = "",
    val mainScene: String = "",
    val icon: String = "",
    val width: String = "1152",
    val height: String = "648",
    val orientation: Int = 0,
    val stretch: String = "disabled",
    val presets: List<ExportPreset> = emptyList(),
    val preset: String = "",
    val identifier: String = "",
    val exportIcon: String = "",
    val exportDrafts: Map<String, Pair<String, String>> = emptyMap(),
) {
    fun select(section: String): ProjectSettings {
        val selected = presets.firstOrNull { it.section == section }
        val exports = GodotConfig(exportText)
        val retained = if (preset.isEmpty()) exportDrafts else exportDrafts + (preset to (identifier to exportIcon))
        val remembered = retained[section]
        return copy(
            exportDrafts = retained,
            preset = section,
            identifier =
            remembered?.first ?: selected?.identifierKey?.let { exports.string("$section.options", it) }.orEmpty(),
            exportIcon =
            remembered?.second ?: selected?.iconKey?.let { exports.string("$section.options", it) }.orEmpty(),
        )
    }

    fun write(project: String, exports: String): Pair<String, String> {
        require(width.toIntOrNull() in 1..16384 && height.toIntOrNull() in 1..16384) {
            "Viewport dimensions must be between 1 and 16384 pixels."
        }
        require(orientation in orientations.indices) { "Choose a supported orientation." }
        require(stretch in stretches) { "Unsupported stretch mode; edit it in Source." }
        for (path in listOf(mainScene, icon, exportIcon)) validateResource(path)
        val changes = linkedMapOf(
            ConfigKey("application", "config/name") to GodotConfig.quote(name),
            ConfigKey("application", "run/main_scene") to GodotConfig.quote(mainScene),
            ConfigKey("application", "config/icon") to GodotConfig.quote(icon),
            ConfigKey("display", "window/size/viewport_width") to width,
            ConfigKey("display", "window/size/viewport_height") to height,
            ConfigKey("display", "window/handheld/orientation") to orientation.toString(),
            ConfigKey("display", "window/stretch/mode") to GodotConfig.quote(stretch),
        )
        val exportChanges = linkedMapOf<ConfigKey, String>()
        val drafts = if (preset.isEmpty()) exportDrafts else exportDrafts + (preset to (identifier to exportIcon))
        for ((section, fields) in drafts) {
            val selected = presets.firstOrNull { it.section == section } ?: continue
            val (applicationId, image) = fields
            validateResource(image)
            selected.identifierKey?.let { key ->
                require(applicationId.matches(Regex("[A-Za-z][A-Za-z0-9_]*(\\.[A-Za-z][A-Za-z0-9_]*)+"))) {
                    "${selected.name}: identifier must contain dot-separated names, such as org.example.game."
                }
                exportChanges[ConfigKey("$section.options", key)] = GodotConfig.quote(applicationId)
            }
            selected.iconKey?.let { exportChanges[ConfigKey("$section.options", it)] = GodotConfig.quote(image) }
        }
        return GodotConfig(project).update(changes) to GodotConfig(exports).update(exportChanges)
    }

    companion object {
        val orientations =
            listOf(
                "Landscape",
                "Portrait",
                "Reverse landscape",
                "Reverse portrait",
                "Sensor landscape",
                "Sensor portrait",
                "Sensor",
            )
        val stretches = listOf("disabled", "canvas_items", "viewport")

        fun validateResource(value: String) {
            require(
                value.isEmpty() || value.startsWith("uid://") ||
                    (
                        value.startsWith("res://") && value.removePrefix("res://").split('/').none { it == ".." } &&
                            !value.contains('\\')
                        ),
            ) {
                "Choose a resource inside this project (res://…)."
            }
        }

        fun read(project: String, exports: String): ProjectSettings {
            val config = GodotConfig(project)
            val export = GodotConfig(exports)
            val presets = export.sections.filter { it.matches(Regex("preset\\.\\d+")) }.map {
                ExportPreset(it, export.string(it, "name"), export.string(it, "platform"))
            }
            val orientationValue = config.raw("display", "window/handheld/orientation")
            val orientation = if (orientationValue == null) {
                0
            } else {
                requireNotNull(orientationValue.toIntOrNull()) {
                    "Unsupported orientation. Correct the value in Source before editing settings."
                }
            }
            require(orientation in orientations.indices) { "Unsupported orientation: $orientation" }
            return ProjectSettings(
                project, exports,
                name = config.string("application", "config/name"),
                mainScene = config.string("application", "run/main_scene"),
                icon = config.string("application", "config/icon"),
                width = config.raw("display", "window/size/viewport_width") ?: "1152",
                height = config.raw("display", "window/size/viewport_height") ?: "648",
                orientation = orientation,
                stretch = config.string("display", "window/stretch/mode", "disabled"),
                presets = presets,
            ).select(presets.firstOrNull()?.section.orEmpty())
        }
    }
}
