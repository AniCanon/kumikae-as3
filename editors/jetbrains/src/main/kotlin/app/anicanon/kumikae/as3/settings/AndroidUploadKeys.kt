package app.anicanon.kumikae.as3.settings

import com.google.gson.Gson
import com.intellij.credentialStore.CredentialAttributes
import com.intellij.credentialStore.Credentials
import com.intellij.ide.passwordSafe.PasswordSafe
import com.intellij.ide.util.PropertiesComponent
import com.intellij.openapi.project.Project
import java.nio.file.Files
import java.nio.file.Path
import java.security.KeyStore
import java.security.PrivateKey
import java.util.concurrent.TimeUnit

internal class UploadKey(val path: String, val user: String, val password: String) {
    fun validate() {
        require(path.isNotBlank() && Path.of(path).isAbsolute && Files.isRegularFile(Path.of(path))) {
            "Choose an existing keystore file."
        }
        val chars = password.toCharArray()
        try {
            val store = KeyStore.getInstance(Path.of(path).toFile(), chars)
            require(store.getKey(user, chars) is PrivateKey) { "Choose a private key alias." }
        } catch (_: Exception) {
            throw IllegalArgumentException(
                "Check the keystore, alias and password. Godot uses the same password for the keystore and key.",
            )
        } finally {
            chars.fill('\u0000')
        }
    }

    fun create() {
        val target = Path.of(path)
        require(target.isAbsolute && !Files.exists(target)) { "Choose a new keystore filename." }
        require(password.length >= 6 && user.isNotBlank()) {
            "Enter an alias and a password of at least six characters."
        }
        val folder = Files.createTempDirectory(target.parent, ".kumikae-key-")
        val key = folder.resolve("upload.p12")
        try {
            val windows = System.getProperty("os.name").startsWith("Windows")
            val tool = Path.of(System.getProperty("java.home"), "bin", if (windows) "keytool.exe" else "keytool")
            val builder = ProcessBuilder(
                tool.toString(), "-genkeypair", "-noprompt", "-storetype", "PKCS12",
                "-keystore", key.toString(), "-alias", user, "-keyalg", "RSA", "-keysize", "3072",
                "-validity", "10000", "-dname", "CN=Android Upload", "-storepass:env", "KAS3_KEY_PASSWORD",
            )
            builder.environment()["KAS3_KEY_PASSWORD"] = password
            builder.redirectOutput(ProcessBuilder.Redirect.DISCARD).redirectError(ProcessBuilder.Redirect.DISCARD)
            val process = builder.start()
            try {
                process.outputStream.close()
                check(process.waitFor(45, TimeUnit.SECONDS) && process.exitValue() == 0) {
                    "Could not create the upload key with the IDE’s keytool."
                }
            } finally {
                if (process.isAlive) process.destroyForcibly().waitFor()
            }
            if (Files.getFileStore(key).supportsFileAttributeView("posix")) {
                Files.setPosixFilePermissions(key, java.nio.file.attribute.PosixFilePermissions.fromString("rw-------"))
            }
            Files.move(key, target)
        } finally {
            Files.deleteIfExists(key)
            Files.deleteIfExists(folder)
        }
    }
}

internal fun hasGodotUploadKey(
    section: String,
    presets: String,
    credentials: String,
    env: Map<String, String>,
): Boolean {
    val saved = GodotConfig(credentials)
    val config = GodotConfig(presets)
    return listOf(
        "PATH" to "release",
        "USER" to "release_user",
        "PASSWORD" to "release_password",
    ).all { (variable, option) ->
        val key = "keystore/$option"
        !env["GODOT_ANDROID_KEYSTORE_RELEASE_$variable"].isNullOrBlank() ||
            saved.string("$section.options", key).isNotBlank() || config.string("$section.options", key).isNotBlank()
    }
}

/** The project file and export command contain no key paths or passwords from this store. */
internal class AndroidUploadKeys(project: Project, root: String, preset: String) {
    private val reference = "kumikae.upload." + java.util.UUID.nameUUIDFromBytes("$root\u0000$preset".toByteArray())
    private val attributes = CredentialAttributes("Kumikae AS3 • $reference")
    private val local = PropertiesComponent.getInstance(project)
    val configured get() = local.getBoolean(reference)

    fun read(): UploadKey? {
        if (!configured) return null
        val saved = requireNotNull(PasswordSafe.instance.get(attributes)) {
            "Unlock Rider’s password store or configure the upload key again."
        }
        val fields = Gson().fromJson(saved.userName, Array<String>::class.java)
        return UploadKey(fields[0], fields[1], requireNotNull(saved.getPasswordAsString()))
    }

    fun save(key: UploadKey?) {
        val credentials = key?.let { Credentials(Gson().toJson(arrayOf(it.path, it.user)), it.password) }
        PasswordSafe.instance.set(attributes, credentials)
        local.setValue(reference, key != null, false)
    }
}
