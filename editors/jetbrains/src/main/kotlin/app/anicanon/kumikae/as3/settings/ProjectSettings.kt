package app.anicanon.kumikae.as3.settings

internal data class ExportPreset(val section: String, val name: String, val platform: String) : BuildTargetChoice {
    override val label get() = if (name == platform) name else toString()
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

internal data class ExportFields(
    val identifier: String,
    val icon: String,
    val executionMode: IosExecutionMode,
    val signingTeam: String,
    val buildNumber: String,
    val releaseVersion: String,
)

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
    val executionMode: IosExecutionMode = IosExecutionMode.Automatic,
    val signingTeam: String = "",
    val buildNumber: String = "",
    val releaseVersion: String = "",
    val exportDrafts: Map<String, ExportFields> = emptyMap(),
    val propertyEdits: Map<ConfigKey, String> = emptyMap(),
) {
    fun allExportDrafts(): Map<String, ExportFields> = if (preset.isEmpty()) {
        exportDrafts
    } else {
        exportDrafts +
            (preset to ExportFields(identifier, exportIcon, executionMode, signingTeam, buildNumber, releaseVersion))
    }

    fun select(section: String): ProjectSettings {
        val selected = presets.firstOrNull { it.section == section }
        val exports = GodotConfig(exportText)
        val retained = allExportDrafts()
        val remembered = retained[section]
        return copy(
            exportDrafts = retained,
            executionMode =
            remembered?.executionMode ?: IosExecutionMode.read(exports.raw("$section.options", NATIVE_CODE)),
            signingTeam = remembered?.signingTeam ?: exports.string("$section.options", SIGNING_TEAM),
            buildNumber = remembered?.buildNumber ?: if (selected?.platform == "Android") {
                exports.raw("$section.options", "version/code") ?: "1"
            } else {
                exports.string("$section.options", "application/version")
            },
            releaseVersion = remembered?.releaseVersion ?: exports.string(
                "$section.options",
                if (selected?.platform ==
                    "Android"
                ) {
                    "version/name"
                } else {
                    "application/short_version"
                },
            ),
            preset = section,
            identifier =
            remembered?.identifier ?: selected?.identifierKey?.let { exports.string("$section.options", it) }.orEmpty(),
            exportIcon =
            remembered?.icon ?: selected?.iconKey?.let { exports.string("$section.options", it) }.orEmpty(),
        )
    }

    fun write(project: String, exports: String): Pair<String, String> {
        val parsedWidth = width.trim().toIntOrNull()
        val parsedHeight = height.trim().toIntOrNull()
        require(parsedWidth in 1..16384 && parsedHeight in 1..16384) {
            "Viewport dimensions must be between 1 and 16384 pixels."
        }
        val normalizedWidth = parsedWidth.toString()
        val normalizedHeight = parsedHeight.toString()
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
        if (normalizedWidth != base.width) changes[ConfigKey("display", "window/size/viewport_width")] = normalizedWidth
        if (normalizedHeight != base.height) {
            changes[ConfigKey("display", "window/size/viewport_height")] = normalizedHeight
        }
        if (orientation != base.orientation) {
            changes[ConfigKey("display", "window/handheld/orientation")] = orientation.toString()
        }
        val originalExports = GodotConfig(exportText)
        val exportChanges = linkedMapOf<ConfigKey, String>()
        for ((section, fields) in allExportDrafts()) {
            val selected = presets.firstOrNull { it.section == section } ?: continue
            val (applicationId, image) = fields
            if (selected.platform == "iOS" || selected.platform == "Android") {
                val options = "$section.options"
                if (fields.executionMode != IosExecutionMode.read(originalExports.raw(options, NATIVE_CODE))) {
                    exportChanges[ConfigKey(options, NATIVE_CODE)] = fields.executionMode.value.toString()
                }
            }
            if (selected.platform == "iOS") {
                val options = "$section.options"
                if (fields.signingTeam != originalExports.string(options, SIGNING_TEAM)) {
                    val team = fields.signingTeam.trim()
                    require(team.isEmpty() || SigningTeam.validId(team)) {
                        "Enter the 10-character Apple team ID for ${selected.name}."
                    }
                    exportChanges[ConfigKey(options, SIGNING_TEAM)] = GodotConfig.quote(team)
                }
            }
            if (selected.identifierKey != null) {
                val options = "$section.options"
                val android = selected.platform == "Android"
                val versionKey = if (android) "version/name" else "application/short_version"
                if (fields.releaseVersion != originalExports.string(options, versionKey)) {
                    require(
                        android || fields.releaseVersion.isBlank() || APPLE_VERSION.matches(fields.releaseVersion),
                    ) {
                        "Enter an iOS version such as 1.2.0."
                    }
                    exportChanges[ConfigKey(options, versionKey)] = GodotConfig.quote(fields.releaseVersion.trim())
                }
                if (android && fields.buildNumber != (originalExports.raw(options, "version/code") ?: "1")) {
                    require(fields.buildNumber.toIntOrNull() in 1..2100000000) {
                        "Enter a positive Android version code up to 2100000000."
                    }
                    exportChanges[ConfigKey(options, "version/code")] = fields.buildNumber.toInt().toString()
                } else if (!android && fields.buildNumber != originalExports.string(options, "application/version")) {
                    require(fields.buildNumber.isBlank() || APPLE_VERSION.matches(fields.buildNumber)) {
                        "Enter a numeric iOS build number such as 42."
                    }
                    exportChanges[ConfigKey(options, "application/version")] =
                        GodotConfig.quote(fields.buildNumber.trim())
                }
            }
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
                exportChanges[ConfigKey("$section.options", it)] = GodotConfig.quote(image)
            }
        }
        val mergedChanges = linkedMapOf<ConfigKey, String>().apply {
            putAll(propertyEdits)
            putAll(changes)
        }
        return GodotConfig(project).update(mergedChanges) to GodotConfig(exports).update(exportChanges)
    }

    companion object {
        private val APPLE_VERSION = Regex("""[0-9]+(\.[0-9]+){0,2}""")
        private const val NATIVE_CODE = "actionscript/native_code"
        private const val SIGNING_TEAM = "application/app_store_team_id"

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
            val relative = value.removePrefix("res://")
            require(
                value.isEmpty() ||
                    (value.startsWith("uid://") && value.length > 6 && value.none(Char::isISOControl)) ||
                    (
                        value.startsWith("res://") && relative.isNotEmpty() && !relative.contains('\\') &&
                            relative.none(Char::isISOControl) &&
                            relative.split('/').none { it == ".." || it == "." }
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
