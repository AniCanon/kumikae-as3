package app.anicanon.kumikae.as3.settings

import app.anicanon.kumikae.as3.PluginFiles
import com.google.gson.Gson
import com.google.gson.JsonParseException
import com.intellij.credentialStore.CredentialAttributes
import com.intellij.credentialStore.Credentials
import com.intellij.ide.passwordSafe.PasswordSafe
import com.intellij.util.concurrency.AppExecutorUtil
import java.nio.file.Path
import java.util.UUID

internal data class TesterBuild(
    val platform: String,
    val identifier: String,
    val version: String,
    val build: String,
    val team: String,
) {
    companion object {
        fun from(settings: ProjectSettings, preset: ExportPreset): TesterBuild {
            val value = settings.select(preset.section)
            require(GameTargets.validIdentifier(preset.platform, value.identifier)) {
                "Set the application identifier before distributing."
            }
            val parts = value.version.split('.').filter(String::isNotBlank)
            val projectVersion = if (parts.isEmpty() ||
                (preset.platform == "iOS" && parts.any { !it.all(Char::isDigit) })
            ) {
                "1.0.0"
            } else {
                parts.joinToString(".")
            }
            val version = value.releaseVersion.ifBlank { projectVersion }
            return TesterBuild(
                preset.platform,
                value.identifier,
                version,
                value.buildNumber.ifBlank {
                    if (preset.platform == "iOS") projectVersion else "1"
                },
                value.signingTeam,
            )
        }
    }
}

internal class TesterAccount(
    val file: String,
    val issuer: String = "",
    val keyId: String = "",
    val refresh: String = "",
)
internal data class TesterGroup(val id: String, val name: String) {
    override fun toString() = name
}
internal class TesterConnection(
    val account: TesterAccount,
    val label: String,
    val targets: List<TesterGroup>,
    var selected: String? = null,
)
internal interface TesterAccounts {
    fun read(): TesterConnection?
    fun remember(connection: TesterConnection?)
    fun connect(account: TesterAccount): TesterConnection
}

internal class TesterAccess(private val root: Path, preset: ExportPreset, private val spec: TesterBuild) :
    TesterAccounts {
    private val id = UUID.nameUUIDFromBytes(
        "$root\u0000${preset.name}\u0000${spec.identifier}\u0000${spec.platform}".toByteArray(),
    ).toString()
    private val credentials = CredentialAttributes("Kumikae AS3 tester distribution · $id")

    override fun read(): TesterConnection? = PasswordSafe.instance.get(credentials)?.getPasswordAsString()?.let {
        parseTesterConnection(it)
    }

    override fun remember(connection: TesterConnection?) {
        PasswordSafe.instance.set(
            credentials,
            connection?.let {
                Credentials("tester distribution", Gson().toJson(it))
            },
        )
    }

    override fun connect(account: TesterAccount): TesterConnection {
        val runtime = requireNotNull(PluginFiles.native()) { "Install the plugin with its mobile runtime." }
        val process = ProcessBuilder(
            "python3",
            "-I",
            runtime.resolve("tools/tester_distribution.py").toString(),
            "connect",
        )
            .start()
        try {
            process.outputStream.bufferedWriter(Charsets.UTF_8).use {
                it.write(Gson().toJson(mapOf("account" to account, "spec" to spec, "root" to root.toString())))
            }
            val output = AppExecutorUtil.getAppExecutorService().submit<String> {
                process.inputStream.bufferedReader(Charsets.UTF_8).use { it.readText() }
            }
            val errors = AppExecutorUtil.getAppExecutorService().submit<String> {
                process.errorStream.bufferedReader(Charsets.UTF_8).use { it.readText() }
            }
            val exit = process.waitFor()
            val reply = output.get()
            check(exit == 0) { errors.get().trim().takeLast(1500).ifEmpty { "Could not connect to the store." } }
            return requireNotNull(parseTesterConnection(reply)) {
                "Store connection returned an unreadable response."
            }
        } finally {
            if (process.isAlive) process.destroyForcibly()
        }
    }

    companion object {
        internal fun parseTesterConnection(raw: String?): TesterConnection? {
            if (raw.isNullOrBlank()) return null
            val parsed = try {
                Gson().fromJson(raw, TesterConnection::class.java)
            } catch (_: JsonParseException) {
                return null
            } ?: return null
            if (parsed.account == null || parsed.label == null || parsed.targets == null) return null
            return parsed
        }
    }
}
