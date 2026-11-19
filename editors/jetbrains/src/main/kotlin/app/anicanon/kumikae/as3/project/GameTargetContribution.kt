package app.anicanon.kumikae.as3.project

import com.intellij.openapi.actionSystem.ActionManager
import com.intellij.openapi.actionSystem.CommonDataKeys
import com.intellij.openapi.actionSystem.ex.ActionUtil
import com.intellij.openapi.actionSystem.impl.SimpleDataContext
import com.intellij.openapi.extensions.ExtensionPointName
import com.intellij.openapi.project.Project
import com.intellij.util.xmlb.annotations.Attribute

/** A platform plugin contributes its own setup workflow through its existing IDE action. */
class GameTargetContribution {
    @Attribute("label")
    var label: String = ""

    @Attribute("setupAction")
    var setupAction: String = ""

    fun configure(project: Project) {
        require(setupAction.isNotBlank()) { "Missing setup action for $label." }
        val action =
            requireNotNull(ActionManager.getInstance().getAction(setupAction)) { "Missing setup action for $label." }
        val context = SimpleDataContext.getSimpleContext(CommonDataKeys.PROJECT, project)
        ActionUtil.invokeAction(action, context, "KumikaeGameTarget", null, null)
    }

    companion object {
        val EP = ExtensionPointName.create<GameTargetContribution>("app.anicanon.kumikae.as3.gameTarget")
    }
}
