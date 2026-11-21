package app.anicanon.kumikae.as3.project

import com.intellij.openapi.actionSystem.ActionManager
import com.intellij.openapi.actionSystem.CommonDataKeys
import com.intellij.openapi.actionSystem.ex.ActionUtil
import com.intellij.openapi.actionSystem.impl.SimpleDataContext
import com.intellij.openapi.extensions.ExtensionPointName
import com.intellij.openapi.project.Project
import com.intellij.openapi.vfs.VirtualFile
import com.intellij.util.xmlb.annotations.Attribute

/** A platform plugin contributes its own setup workflow through its existing IDE action. */
class GameTargetContribution {
    @Attribute("label")
    var label: String = ""

    @Attribute("setupAction")
    var setupAction: String = ""

    @Attribute("settingsAction")
    var settingsAction: String = ""

    fun configure(project: Project) = invoke(setupAction, project, null)

    fun openSettings(project: Project, game: VirtualFile) = invoke(settingsAction, project, game)

    private fun invoke(id: String, project: Project, game: VirtualFile?) {
        val action = requireNotNull(ActionManager.getInstance().getAction(id)) { "Missing action for $label." }
        val context = SimpleDataContext.builder().add(CommonDataKeys.PROJECT, project)
        if (game != null) context.add(CommonDataKeys.VIRTUAL_FILE, game)
        ActionUtil.invokeAction(action, context.build(), "KumikaeGameTarget", null, null)
    }

    companion object {
        val EP = ExtensionPointName.create<GameTargetContribution>("app.anicanon.kumikae.as3.gameTarget")
    }
}
