package app.anicanon.kumikae.as3.run

import app.anicanon.kumikae.as3.project.LocalGodotSetup
import com.intellij.ide.util.PropertiesComponent
import java.nio.file.Path

internal object IdeGodot {
    private const val KEY = "kumikae.as3.godotExecutable"
    private val home get() = Path.of(System.getProperty("user.home"))
    val installDirectory: Path get() = home.resolve(
        "Library/Application Support/Kumikae/Godot/${LocalGodotSetup.VERSION}",
    )

    fun remember(path: Path) = PropertiesComponent.getInstance().setValue(KEY, path.toString())

    fun find(configured: String = ""): Path? = candidates(configured).firstOrNull()

    fun candidates(configured: String = ""): List<Path> = GodotLaunch.candidates(
        listOfNotNull(configured, PropertiesComponent.getInstance().getValue(KEY)),
        System.getenv("PATH").orEmpty(),
        listOf(Path.of("/Applications"), home.resolve("Applications"), installDirectory),
    )
}
