package app.anicanon.kumikae.as3.run

import app.anicanon.kumikae.as3.project.GamePane
import com.intellij.execution.RunManager
import com.intellij.ide.projectView.ProjectView
import com.intellij.ide.util.PropertiesComponent
import com.intellij.openapi.application.EDT
import com.intellij.openapi.project.Project
import com.intellij.openapi.startup.ProjectActivity
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.nio.file.Path

/**
 * A project that is an AS3 Godot game gets a "Game" run configuration when it is opened, and it is the
 * one chosen, so that Run runs the game without anything being set up. One that was already made, or
 * removed and replaced by the person's own, is left alone.
 */
class GodotProjectStartup : ProjectActivity {
    override suspend fun execute(project: Project) {
        val base = project.basePath ?: return
        if (!GodotLaunch.isAs3Game(Path.of(base))) return
        withContext(Dispatchers.EDT) {
            // The first time the project is opened, it opens in the Game view. After that the view is
            // whichever the person left it in.
            val remembered = PropertiesComponent.getInstance(project)
            if (!remembered.getBoolean(VIEW_CHOSEN)) {
                remembered.setValue(VIEW_CHOSEN, true)
                ProjectView.getInstance(project).changeView(GamePane.ID)
            }
            val manager = RunManager.getInstance(project)
            val type = GodotConfigurationType()
            if (manager.allSettings.any { it.type.id == GodotConfigurationType.ID }) return@withContext
            val settings = manager.createConfiguration("Game", type.configurationFactories.first())
            manager.addConfiguration(settings)
            manager.selectedConfiguration = settings
        }
    }

    private companion object {
        const val VIEW_CHOSEN = "kumikae.as3.gameViewChosen"
    }
}
