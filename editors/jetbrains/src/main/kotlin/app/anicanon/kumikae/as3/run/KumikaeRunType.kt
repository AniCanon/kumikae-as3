package app.anicanon.kumikae.as3.run

import app.anicanon.kumikae.as3.PluginFiles
import app.anicanon.kumikae.as3.testing.As3TestsConfiguration
import app.anicanon.kumikae.as3.testing.As3TestsOptions
import com.intellij.execution.configurations.ConfigurationFactory
import com.intellij.execution.configurations.ConfigurationTypeBase
import com.intellij.execution.configurations.ConfigurationTypeUtil
import com.intellij.execution.configurations.RunConfiguration
import com.intellij.execution.configurations.RunConfigurationOptions
import com.intellij.openapi.project.Project

class KumikaeRunType :
    ConfigurationTypeBase("KumikaeAS3", "Kumikae AS3", "Run a Godot game or its tests", PluginFiles.mark) {
    enum class Kind(val label: String) { GAME("Game"), SIMULATOR("iOS Simulator"), ANDROID("Android"), TESTS("Tests") }

    init {
        for (kind in Kind.entries) {
            addFactory(object : ConfigurationFactory(this) {
                override fun getId() = kind.label
                override fun getName() = if (kind == Kind.SIMULATOR) "iOS" else kind.label
                override fun getOptionsClass(): Class<out RunConfigurationOptions> = when (kind) {
                    Kind.GAME -> GodotRunOptions::class.java
                    Kind.SIMULATOR -> SimulatorRunOptions::class.java
                    Kind.ANDROID -> AndroidOptions::class.java
                    Kind.TESTS -> As3TestsOptions::class.java
                }
                override fun createTemplateConfiguration(project: Project): RunConfiguration = when (kind) {
                    Kind.GAME -> GodotRunConfiguration(project, this, kind.label)
                    Kind.SIMULATOR -> SimulatorRunConfiguration(project, this, name)
                    Kind.ANDROID -> AndroidRunConfiguration(project, this, kind.label)
                    Kind.TESTS -> As3TestsConfiguration(project, this, kind.label)
                }
            })
        }
    }

    companion object {
        fun factory(kind: Kind): ConfigurationFactory =
            ConfigurationTypeUtil.findConfigurationType(KumikaeRunType::class.java).configurationFactories.single {
                it.id ==
                    kind.label
            }
    }
}
