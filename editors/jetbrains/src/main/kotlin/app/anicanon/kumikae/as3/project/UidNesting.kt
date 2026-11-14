package app.anicanon.kumikae.as3.project

import com.intellij.ide.projectView.ProjectViewNestingRulesProvider

/** Godot writes a `.uid` file beside every script. In the project view it sits under its script, not beside it. */
class UidNesting : ProjectViewNestingRulesProvider {
    override fun addFileNestingRules(consumer: ProjectViewNestingRulesProvider.Consumer) {
        consumer.addNestingRule(".as", ".as.uid")
        consumer.addNestingRule(".gdextension", ".gdextension.uid")
    }
}
