package app.anicanon.kumikae.as3.settings

import app.anicanon.kumikae.as3.simulator.CommandRunner
import app.anicanon.kumikae.as3.simulator.SimulatorException
import com.google.gson.JsonParser
import java.nio.file.Files
import java.nio.file.Path

internal data class SigningTeam(val id: String, val name: String, val personal: Boolean = false) {
    val label get() = if (personal) "$name (Personal Team) · $id" else "$name · $id"

    companion object {
        private val identifier = Regex("[A-Z0-9]{10}")
        fun validId(value: String) = identifier.matches(value)
    }
}

internal object XcodeTeams {
    fun load(runner: CommandRunner, preferences: Path): List<SigningTeam> {
        if (Files.notExists(preferences)) return emptyList()
        val teams = try {
            runner.run(
                listOf(
                    "/usr/bin/plutil",
                    "-extract",
                    "IDEProvisioningTeamByIdentifier",
                    "json",
                    "-o",
                    "-",
                    preferences.toString(),
                ),
                5000,
            )
        } catch (error: SimulatorException) {
            if (error.message.orEmpty().contains("No value at that key path")) return emptyList()
            throw error
        }
        return parse(teams)
    }

    fun parse(cache: String): List<SigningTeam> = JsonParser.parseString(cache).asJsonObject.entrySet()
        .flatMap { (_, group) -> if (group.isJsonArray) group.asJsonArray.toList() else emptyList() }
        .filter { it.isJsonObject }
        .mapNotNull {
            val record = it.asJsonObject
            fun field(key: String) = record[key]?.takeIf { item -> item.isJsonPrimitive }?.asString
            val id = field("teamID")?.takeIf(SigningTeam::validId) ?: return@mapNotNull null
            SigningTeam(id, field("teamName")?.ifBlank { null } ?: id, field("isFreeProvisioningTeam") == "true")
        }
        .distinctBy { it.id }
        .sortedWith(compareBy(String.CASE_INSENSITIVE_ORDER, SigningTeam::name).thenBy(SigningTeam::id))
}
