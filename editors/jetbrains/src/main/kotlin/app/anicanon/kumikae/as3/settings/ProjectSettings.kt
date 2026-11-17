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
    val version: String = "",
    val splash: String = "",
    val mainScene: String = "",
    val icon: String = "",
    val width: String = "1152",
    val height: String = "648",
    val orientation: Int = 0,
    val aspect: String = "keep",
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
        require(aspect in aspects) { "Choose a supported stretch aspect." }
        for (path in listOf(mainScene, icon, splash, exportIcon)) validateResource(path)
        fun validateType(kind: GameResourceKind, path: String) {
            if (path.startsWith("res://")) {
                require(kind.accepts(path)) {
                    "Choose a supported resource for ${kind.title}."
                }
            }
        }
        validateType(GameResourceKind.Scene, mainScene)
        validateType(GameResourceKind.Icon, icon)
        validateType(GameResourceKind.Splash, splash)
        val base = read(projectText, exportText)
        val changes = linkedMapOf<ConfigKey, String>()
        fun string(section: String, key: String, before: String, after: String) {
            if (before != after) changes[ConfigKey(section, key)] = GodotConfig.quote(after)
        }
        string("application", "config/name", base.name, name)
        string("application", "config/version", base.version, version)
        string("application", "run/main_scene", base.mainScene, mainScene)
        string("application", "config/icon", base.icon, icon)
        string("application", "boot_splash/image", base.splash, splash)
        string("display", "window/stretch/mode", base.stretch, stretch)
        string("display", "window/stretch/aspect", base.aspect, aspect)
        if (width != base.width) changes[ConfigKey("display", "window/size/viewport_width")] = width
        if (height != base.height) changes[ConfigKey("display", "window/size/viewport_height")] = height
        if (orientation !=
            base.orientation
        ) {
            changes[ConfigKey("display", "window/handheld/orientation")] = orientation.toString()
        }
        val originalExports = GodotConfig(exportText)
        val exportChanges = linkedMapOf<ConfigKey, String>()
        val drafts = if (preset.isEmpty()) exportDrafts else exportDrafts + (preset to (identifier to exportIcon))
        for ((section, fields) in drafts) {
            val selected = presets.firstOrNull { it.section == section } ?: continue
            val (applicationId, image) = fields
            validateResource(image)
            validateType(GameResourceKind.ExportIcon, image)
            selected.identifierKey?.takeIf {
                originalExports.string("$section.options", it) != applicationId
            }?.let { key ->
                require(GameTargets.validIdentifier(selected.platform, applicationId)) {
                    "${selected.name}: identifier must contain dot-separated names, such as org.example.game."
                }
                exportChanges[ConfigKey("$section.options", key)] = GodotConfig.quote(applicationId)
            }
            selected.iconKey?.takeIf { originalExports.string("$section.options", it) != image }?.let {
                exportChanges[ConfigKey("$section.options", it)] =
                    GodotConfig.quote(image)
            }
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
        val aspects = listOf("keep", "keep_width", "keep_height", "expand", "ignore")
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
            val presets = GameTargets.presets(export)
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
                version = config.string("application", "config/version"),
                splash = config.string("application", "boot_splash/image"),
                name = config.string("application", "config/name"),
                mainScene = config.string("application", "run/main_scene"),
                icon = config.string("application", "config/icon"),
                width = config.raw("display", "window/size/viewport_width") ?: "1152",
                height = config.raw("display", "window/size/viewport_height") ?: "648",
                orientation = orientation,
                aspect = config.string("display", "window/stretch/aspect", "keep"),
                stretch = config.string("display", "window/stretch/mode", "disabled"),
                presets = presets,
            ).select(presets.firstOrNull()?.section.orEmpty())
        }
    }
}
