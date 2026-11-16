package app.anicanon.kumikae.as3.panel

import app.anicanon.kumikae.as3.testing.As3TestConsole
import app.anicanon.kumikae.as3.testing.As3TestsConfiguration
import app.anicanon.kumikae.as3.testing.As3TestsType
import com.google.gson.Gson
import com.intellij.execution.configurations.GeneralCommandLine
import com.intellij.execution.executors.DefaultRunExecutor
import com.intellij.execution.process.KillableColoredProcessHandler
import com.intellij.execution.testframework.sm.SMTestRunnerConnectionUtil
import com.intellij.execution.testframework.sm.runner.ui.SMTRunnerConsoleView
import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.project.Project
import com.intellij.openapi.util.Disposer
import java.nio.file.Files
import java.nio.file.Path

internal object UiProbeTestResults {
    fun run(project: Project, destination: Path) {
        lateinit var console: SMTRunnerConsoleView
        lateinit var handler: KillableColoredProcessHandler
        val script = Files.createTempFile("kas3-result-probe", ".py")
        ApplicationManager.getApplication().invokeAndWait {
            val type = As3TestsType()
            val configuration = As3TestsConfiguration(project, type.configurationFactories.single(), "Probe tests")
            val properties = As3TestConsole(configuration, DefaultRunExecutor.getRunExecutorInstance())
            console = SMTestRunnerConnectionUtil.createConsole("AS3", properties) as SMTRunnerConsoleView
            val output = listOf(
                "##teamcity[testSuiteStarted name='AS3 Tests']",
                "##teamcity[testStarted name='passing case']",
                "##teamcity[testFinished name='passing case']",
                "##teamcity[testStarted name='failing case']",
                "##teamcity[testFailed name='failing case' message='Expected failure']",
                "##teamcity[testFinished name='failing case']",
                "##teamcity[testSuiteFinished name='AS3 Tests']",
            ).joinToString("\n")
            Files.writeString(script, "print(" + Gson().toJson(output) + ")")
            handler = KillableColoredProcessHandler(GeneralCommandLine("python3", script.toString()))
            console.attachToProcess(handler)
            handler.startNotify()
        }
        try {
            check(handler.waitFor(10_000)) { "Test-result probe process did not finish" }
            var verified = false
            val deadline = System.nanoTime() + 10_000_000_000
            while (!verified && System.nanoTime() < deadline) {
                ApplicationManager.getApplication().invokeAndWait {
                    val leaves = console.resultsViewer.testsRootNode.allTests.filter { !it.isSuite }
                    verified = leaves.size == 2 && leaves.any { it.name == "passing case" && it.isPassed } &&
                        leaves.any { it.name == "failing case" && it.isDefect }
                }
                if (!verified) Thread.sleep(30)
            }
            ApplicationManager.getApplication().invokeAndWait {
                Files.writeString(
                    destination.resolve("test-tree.txt"),
                    console.resultsViewer.testsRootNode.allTests.joinToString("\n") {
                        "${it.name}: suite=${it.isSuite} passed=${it.isPassed} defect=${it.isDefect} final=${it.isFinal}"
                    },
                )
            }
            check(verified) { "Native Rider test tree did not show one pass and one failure" }
            Files.writeString(
                destination.resolve("test-results.txt"),
                "passed: native result tree shows one pass and one failure\n",
            )
        } finally {
            if (!handler.isProcessTerminated) handler.destroyProcess()
            ApplicationManager.getApplication().invokeAndWait { Disposer.dispose(console) }
            Files.deleteIfExists(script)
        }
    }
}
