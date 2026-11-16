package app.anicanon.kumikae.as3.project

import com.intellij.ide.projectView.ProjectView
import com.intellij.ide.util.PropertiesComponent
import com.intellij.openapi.actionSystem.ActionUpdateThread
import com.intellij.openapi.actionSystem.AnActionEvent
import com.intellij.openapi.actionSystem.ToggleAction
import com.intellij.openapi.project.Project

internal object GameViewOptions {
    fun compact(project: Project) = PropertiesComponent.getInstance(project).getBoolean(COMPACT, true)
    fun generated(project: Project) = PropertiesComponent.getInstance(project).getBoolean(GENERATED, false)
    const val COMPACT = "kumikae.as3.compactPackages"
    const val GENERATED = "kumikae.as3.showGenerated"
}

abstract class GameViewToggle(private val key: String, private val default: Boolean) : ToggleAction() {
    override fun getActionUpdateThread() = ActionUpdateThread.BGT
    override fun isSelected(event: AnActionEvent) = event.project?.let {
        PropertiesComponent.getInstance(it).getBoolean(key, default)
    } ?: default

    override fun setSelected(event: AnActionEvent, state: Boolean) {
        val project = event.project ?: return
        PropertiesComponent.getInstance(project).setValue(key, state, default)
        ProjectView.getInstance(project).currentProjectViewPane?.updateFromRoot(true)
    }

    override fun update(event: AnActionEvent) {
        super.update(event)
        event.presentation.isEnabledAndVisible = event.project?.let {
            ProjectView.getInstance(it).currentViewId == GamePane.ID
        } == true
    }
}

class CompactPackagesAction : GameViewToggle(GameViewOptions.COMPACT, true)
class ShowGeneratedAction : GameViewToggle(GameViewOptions.GENERATED, false)
