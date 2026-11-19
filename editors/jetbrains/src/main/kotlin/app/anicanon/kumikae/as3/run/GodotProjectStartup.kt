package app.anicanon.kumikae.as3.run

import app.anicanon.kumikae.as3.project.GamePane
import app.anicanon.kumikae.as3.project.GameSourceRoots
import app.anicanon.kumikae.as3.testing.As3TestsConfiguration
import com.intellij.execution.RunManager
import com.intellij.ide.projectView.ProjectView
import com.intellij.ide.util.PropertiesComponent
import com.intellij.openapi.application.EDT
import com.intellij.openapi.project.Project
import com.intellij.openapi.startup.ProjectActivity
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.nio.file.Files
import java.nio.file.Path

/**
 * A project that is an AS3 Godot game gets a "Game" run configuration when it is opened, and it is the
 * one chosen, so that Run runs the game without anything being set up. One that was already made, or
 * removed and replaced by the person's own, is left alone.
 */
class GodotProjectStartup : ProjectActivity {
    override suspend fun execute(project: Project) {
        val base = project.basePath ?: return
        val basePath = Path.of(base)
        if (!GodotLaunch.isAs3Game(basePath)) return
        val hasTests = Files.isRegularFile(basePath.resolve("kas3-tests.json"))
        withContext(Dispatchers.EDT) {
            if (project.isDisposed) return@withContext
            GameSourceRoots.register(project, basePath)
            // The first time the project is opened, it opens in the Game view. After that the view is
            // whichever the person left it in.
            val remembered = PropertiesComponent.getInstance(project)
            if (!remembered.getBoolean(VIEW_CHOSEN)) {
                remembered.setValue(VIEW_CHOSEN, true)
                ProjectView.getInstance(project).changeView(GamePane.ID)
            }
            val manager = RunManager.getInstance(project)
            if (hasTests) ensureDefaultTests(manager)
            val factory = KumikaeRunType.factory(KumikaeRunType.Kind.GAME)
            if (manager.allSettings.any { it.configuration is GodotRunConfiguration }) return@withContext
            val settings = manager.createConfiguration("Game", factory)
            manager.addConfiguration(settings)
            if (manager.selectedConfiguration == null) manager.selectedConfiguration = settings
        }
    }

    private companion object {
        const val VIEW_CHOSEN = "kumikae.as3.gameViewChosen"
    }
}

internal fun ensureDefaultTests(manager: RunManager) {
    val exists = manager.allSettings.any {
        val config = it.configuration as? As3TestsConfiguration
        config != null && config.options.cases.isBlank() && config.options.manifest == "kas3-tests.json"
    }
    if (!exists) {
        val factory = KumikaeRunType.factory(KumikaeRunType.Kind.TESTS)
        manager.addConfiguration(
            manager.createConfiguration(manager.suggestUniqueName("Game tests", factory.type), factory),
        )
    }
}
