package app.anicanon.kumikae.as3.panel

import app.anicanon.kumikae.as3.PluginFiles
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
import com.intellij.openapi.application.ReadAction
import com.intellij.openapi.fileEditor.FileEditorManager
import com.intellij.openapi.fileEditor.OpenFileDescriptor
import com.intellij.openapi.project.Project
import com.intellij.openapi.util.Disposer
import com.intellij.openapi.vfs.LocalFileSystem
import com.intellij.psi.search.GlobalSearchScope
import java.nio.file.Files
import java.nio.file.Path

internal object UiProbeTestResults {
    fun run(project: Project, destination: Path) {
        lateinit var properties: As3TestConsole
        lateinit var console: SMTRunnerConsoleView
        lateinit var handler: KillableColoredProcessHandler
        val root = Files.createTempDirectory(Path.of(checkNotNull(project.basePath)), "case-results-")
        val native = checkNotNull(PluginFiles.native())
        val fixture = root.resolve("ProbeCase.as")
        Files.writeString(
            fixture,
            """
            package {
                import kumikae.testing.TestCase;
                import kumikae.testing.Assert;
                public class ProbeCase extends TestCase {
                    private var succeeds:Boolean;
                    public function ProbeCase(name:String, succeeds:Boolean) { super(name); this.succeeds = succeeds; }
                    override public function run():void {
                        Assert.assertTrue(succeeds, "Expected failure");
                    }
                }
            }
            """.trimIndent(),
        )
        val failureLine = Files.readAllLines(fixture).indexOfFirst { it.contains("Assert.assertTrue") }
        Files.writeString(
            root.resolve("ProbeSuite.as"),
            """
            package {
                import kumikae.testing.Suite;
                public class ProbeSuite {
                    public function ProbeSuite() {
                        var suite:Suite = new Suite("Probe");
                        suite.add(new ProbeCase("passing case", true));
                        suite.add(new ProbeCase("failing case", false));
                        suite.run();
                        trace("DONE");
                    }
                }
            }
            """.trimIndent(),
        )
        val manifest = root.resolve("kas3-tests.json")
        Files.writeString(
            manifest,
            Gson().toJson(
                mapOf(
                    "version" to 1,
                    "tests" to listOf(
                        mapOf(
                            "name" to "Probe",
                            "source" to "ProbeCase.as",
                            "events" to true,
                            "marker" to "DONE",
                            "command" to listOf("{compiler}", "run", "ProbeSuite.as", "ProbeCase.as", "{sources}"),
                            "sources" to listOf("**/*.as"),
                        ),
                    ),
                ),
            ),
        )
        // The fixture uses exactly the library bundled with this plugin.
        Files.walk(native.resolve("testing/as3")).use { paths ->
            paths.filter { it.toString().endsWith(".as") }.forEach {
                val target = root.resolve("framework").resolve(native.resolve("testing/as3").relativize(it))
                Files.createDirectories(target.parent)
                Files.copy(it, target)
            }
        }
        LocalFileSystem.getInstance().refreshAndFindFileByNioFile(fixture)
        ApplicationManager.getApplication().invokeAndWait {
            val type = As3TestsType()
            val configuration = As3TestsConfiguration(project, type.configurationFactories.single(), "Probe tests")
            configuration.options.manifest = manifest.toString()
            properties = As3TestConsole(configuration, DefaultRunExecutor.getRunExecutorInstance())
            console = SMTestRunnerConnectionUtil.createConsole("AS3", properties) as SMTRunnerConsoleView
            val command = GeneralCommandLine(
                "python3",
                native.resolve("testing/runner.py").toString(),
                manifest.toString(),
                "--compiler",
                native.resolve("kas3c").toString(),
            )
            handler = KillableColoredProcessHandler(command)
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
                    verified = leaves.size == 2 && leaves.any { it.name.endsWith("::passing case") && it.isPassed } &&
                        leaves.any { it.name.endsWith("::failing case") && it.isDefect }
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
            val descriptor = ReadAction.compute<OpenFileDescriptor?, RuntimeException> {
                val failed = console.resultsViewer.testsRootNode.allTests.single { it.name.endsWith("::failing case") }
                check(failed.stacktrace.orEmpty().contains("ProbeCase.as:" + (failureLine + 1)))
                failed.getDescriptor(
                    failed.getLocation(project, GlobalSearchScope.allScope(project)),
                    properties,
                ) as? OpenFileDescriptor
            }
            ApplicationManager.getApplication().invokeAndWait {
                check(descriptor?.file?.path == fixture.toString() && descriptor.line == failureLine) {
                    "Failed result location: ${descriptor?.file?.path}:${descriptor?.line}; expected $fixture:$failureLine"
                }
                descriptor.navigate(true)
                val editor = checkNotNull(FileEditorManager.getInstance(project).selectedTextEditor)
                check(editor.document.getLineNumber(editor.caretModel.offset) == failureLine) {
                    "Opening the failure did not place the caret on the assertion"
                }
                FileEditorManager.getInstance(project).closeFile(descriptor.file)
            }
            UiProbeTestLinks.run(project, properties, fixture, failureLine)
            check(verified) { "Native Rider test tree did not show one pass and one failure" }
            Files.writeString(
                destination.resolve("test-results.txt"),
                "passed: bundled runner and compiler show one pass, one failure and assertion-line navigation\n",
            )
        } finally {
            if (!handler.isProcessTerminated) handler.destroyProcess()
            ApplicationManager.getApplication().invokeAndWait { Disposer.dispose(console) }
            root.toFile().deleteRecursively()
        }
    }
}
