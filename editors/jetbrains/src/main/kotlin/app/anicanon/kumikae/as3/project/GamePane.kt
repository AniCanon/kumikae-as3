package app.anicanon.kumikae.as3.project

import app.anicanon.kumikae.as3.PluginFiles
import app.anicanon.kumikae.as3.run.GodotLaunch
import com.intellij.ide.projectView.impl.ProjectViewPane
import com.intellij.openapi.project.Project
import java.nio.file.Path
import javax.swing.Icon

/**
 * "Game" in the project panel's list of views: the project as a person making the game sees it. It is
 * the IDE's own project tree with what Godot and the IDE keep for themselves left out (GameProjectView
 * does the leaving out). Offered only in a project that is an AS3 Godot game.
 */
class GamePane(project: Project) : ProjectViewPane(project) {
    override fun getId() = ID

    override fun getTitle() = "Game"

    override fun getIcon(): Icon = PluginFiles.mark

    // Before the IDE's own views, so that it is the first in the list.
    override fun getWeight() = -10

    override fun isInitiallyVisible(): Boolean =
        myProject.basePath?.let { runCatching { GodotLaunch.isAs3Game(Path.of(it)) }.getOrDefault(false) } ?: false

    companion object {
        const val ID = "KumikaeAs3Game"
    }
}
