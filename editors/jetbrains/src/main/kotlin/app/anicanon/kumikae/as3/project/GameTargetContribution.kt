package app.anicanon.kumikae.as3.project

import com.intellij.openapi.actionSystem.ActionManager
import com.intellij.openapi.actionSystem.ActionUiKind
import com.intellij.openapi.actionSystem.AnActionEvent
import com.intellij.openapi.actionSystem.CommonDataKeys
import com.intellij.openapi.actionSystem.ex.ActionUtil
import com.intellij.openapi.actionSystem.impl.SimpleDataContext
import com.intellij.openapi.extensions.CustomLoadingExtensionPointBean
import com.intellij.openapi.extensions.ExtensionPointName
import com.intellij.openapi.project.Project
import com.intellij.openapi.ui.Messages
import com.intellij.openapi.vfs.VirtualFile
import com.intellij.util.xmlb.annotations.Attribute
import java.util.function.Predicate

/** A platform plugin contributes its own setup workflow through its existing IDE action. */
class GameTargetContribution : CustomLoadingExtensionPointBean<Predicate<VirtualFile>>() {
    @Attribute("label")
    var label: String = ""

    @Attribute("setupAction")
    var setupAction: String = ""

    @Attribute("settingsAction")
    var settingsAction: String = ""

    @Attribute("configuredWhen")
    var configuredWhen: String = ""

    override fun getImplementationClassName() = configuredWhen

    fun hasSettings(game: VirtualFile) =
        settingsAction.isNotBlank() && configuredWhen.isNotBlank() && instance.test(game)

    @JvmOverloads
    fun configure(project: Project, game: VirtualFile? = null) = invoke(setupAction, project, game)

    fun openSettings(project: Project, game: VirtualFile) = invoke(settingsAction, project, game)

    private fun invoke(id: String, project: Project, game: VirtualFile?) {
        val action = ActionManager.getInstance().getAction(id)
        if (action == null) {
            Messages.showErrorDialog(
                project,
                "The companion action for ${label.ifBlank { id }} is unavailable.",
                "Game Target",
            )
            return
        }
        val context = SimpleDataContext.builder().add(CommonDataKeys.PROJECT, project)
        if (game != null) context.add(CommonDataKeys.VIRTUAL_FILE, game)
        val event = AnActionEvent.createEvent(
            action,
            context.build(),
            null,
            "KumikaeGameTarget",
            ActionUiKind.NONE,
            null,
        )
        ActionUtil.performAction(action, event)
    }

    companion object {
        val EP = ExtensionPointName.create<GameTargetContribution>("app.anicanon.kumikae.as3.gameTarget")
    }
}
