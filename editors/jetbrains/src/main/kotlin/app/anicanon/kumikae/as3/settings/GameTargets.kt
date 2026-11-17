package app.anicanon.kumikae.as3.settings

import app.anicanon.kumikae.as3.project.ProjectTemplate

internal data class IosTarget(val name: String, val section: String, val bundle: String, val template: String = "")

internal object GameTargets {
    fun validIdentifier(platform: String, value: String) = if (platform == "Android") {
        Regex("[A-Za-z][A-Za-z0-9_]*(\\.[A-Za-z][A-Za-z0-9_]*)+").matches(value)
    } else {
        ProjectTemplate.isBundle(value)
    }

    fun presets(config: GodotConfig): List<ExportPreset> =
        config.sections.filter { it.matches(Regex("preset\\.\\d+")) }.map {
            ExportPreset(it, config.string(it, "name"), config.string(it, "platform"))
        }

    fun ios(text: String, name: String): IosTarget {
        val config = GodotConfig(text)
        val matching = presets(config).filter { it.platform == "iOS" && (name.isEmpty() || it.name == name) }
        val preset = requireNotNull(matching.singleOrNull()) {
            if (matching.isEmpty()) {
                "Add an iOS target or choose an existing iOS preset."
            } else {
                "Choose one iOS export preset. More than one matches."
            }
        }
        val bundle = config.string("${preset.section}.options", requireNotNull(preset.identifierKey))
        require(ProjectTemplate.isBundle(bundle)) { "Set a valid bundle identifier in ${preset.name}." }
        return IosTarget(
            preset.name,
            preset.section,
            bundle,
            config.string("${preset.section}.options", "custom_template/debug"),
        )
    }

    fun add(text: String, platform: String, name: String, identifier: String): String {
        require(platform == "iOS" || platform == "Android") { "Unsupported platform: $platform" }
        require(name.isNotBlank() && name.none(Char::isISOControl)) { "Enter a single-line preset name." }
        val config = GodotConfig(text)
        val all = presets(config)
        val existing = all.filter { it.name == name }
        if (existing.isNotEmpty()) {
            require(existing.singleOrNull()?.platform == platform) { "Another preset already uses $name." }
            return text
        }
        require(validIdentifier(platform, identifier)) { "Enter a valid application identifier." }
        val next = generateSequence(0) { it + 1 }.first {
            "preset.$it" !in config.sections &&
                "preset.$it.options" !in config.sections
        }
        val section = "preset.$next"
        val options = "$section.options"
        val preset = ExportPreset(section, name, platform)
        val settings = linkedMapOf(
            ConfigKey(section, "name") to GodotConfig.quote(name),
            ConfigKey(section, "platform") to GodotConfig.quote(platform),
            ConfigKey(section, "runnable") to all.none { it.platform == platform }.toString(),
            ConfigKey(section, "export_filter") to "\"all_resources\"",
            ConfigKey(section, "include_filter") to "\"*.as\"",
            ConfigKey(section, "exclude_filter") to "\"tests/*\"",
            ConfigKey(options, requireNotNull(preset.identifierKey)) to GodotConfig.quote(identifier),
        )
        if (platform == "iOS") {
            settings[ConfigKey(options, "application/export_project_only")] = "true"
            settings[ConfigKey(options, "application/app_store_team_id")] = "\"\""
        }
        return config.update(settings)
    }
}
