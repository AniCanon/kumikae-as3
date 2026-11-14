package app.anicanon.kumikae.as3.run

import com.intellij.execution.RunManager
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
            val manager = RunManager.getInstance(project)
            val type = GodotConfigurationType()
            if (manager.allSettings.any { it.type.id == GodotConfigurationType.ID }) return@withContext
            val settings = manager.createConfiguration("Game", type.configurationFactories.first())
            manager.addConfiguration(settings)
            manager.selectedConfiguration = settings
        }
    }
}
