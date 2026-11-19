package app.anicanon.kumikae.as3.panel

import app.anicanon.kumikae.as3.PluginFiles
import app.anicanon.kumikae.as3.project.NewAs3FileAction
import app.anicanon.kumikae.as3.project.ProjectTemplate
import app.anicanon.kumikae.as3.testing.As3TestLineMarker
import app.anicanon.kumikae.as3.testing.TestDiscovery
import com.intellij.execution.testframework.sm.runner.SMTestProxy
import com.intellij.execution.testframework.sm.runner.ui.SMTRunnerConsoleView
import com.intellij.execution.ui.RunContentManager
import com.intellij.ide.fileTemplates.FileTemplateManager
import com.intellij.openapi.actionSystem.AnAction
import com.intellij.openapi.actionSystem.AnActionEvent
import com.intellij.openapi.actionSystem.DataContext
import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.application.ReadAction
import com.intellij.openapi.command.WriteCommandAction
import com.intellij.openapi.fileEditor.FileDocumentManager
import com.intellij.openapi.fileEditor.OpenFileDescriptor
import com.intellij.openapi.project.Project
import com.intellij.openapi.vfs.LocalFileSystem
import com.intellij.psi.PsiDocumentManager
import com.intellij.psi.PsiManager
import com.intellij.psi.search.GlobalSearchScope
import java.nio.file.Files
import java.nio.file.Path
import java.util.concurrent.TimeUnit

internal object UiProbeAuthoring {
    fun run(project: Project, root: Path) {
        val folder = Files.createTempDirectory(root, "new-game")
        try {
            val library = PluginFiles.native()?.takeIf(ProjectTemplate::hasLibrary)
            ProjectTemplate("New Game", emptySet(), "games.qa").write(folder, library, link = false)
            lateinit var gutter: AnAction
            ApplicationManager.getApplication().invokeAndWait {
                val game = checkNotNull(LocalFileSystem.getInstance().refreshAndFindFileByNioFile(folder))
                game.refresh(false, true)
                val directory = checkNotNull(PsiManager.getInstance(project).findDirectory(game))
                val files = FileTemplateManager.getInstance(project)
                val action = NewAs3FileAction()
                WriteCommandAction.runWriteCommandAction(project) {
                    val fixture = action.createFileFromTemplate(
                        "ScoreTest",
                        files.getInternalTemplate("AS3 Test Case"),
                        directory,
                    )
                    check(fixture.virtualFile.path.endsWith("/tests/games/qa/ScoreTest.as"))
                    check("package games.qa {" in fixture.text && "class ScoreTest" in fixture.text)
                    val suite = action.createFileFromTemplate(
                        "SmokeSuite",
                        files.getInternalTemplate("AS3 Test Suite"),
                        directory,
                    )
                    val tests = TestDiscovery.forElement(suite)
                    check(
                        tests.any {
                            it.name == "Game tests::games.qa.ScoreTest::example"
                        },
                    ) { "Suite did not discover the new test: $tests" }
                    val model = action.createFileFromTemplate(
                        "Score",
                        files.getInternalTemplate("AS3 Class"),
                        directory,
                    )
                    check(model.virtualFile.path.endsWith("/src/games/qa/Score.as"))
                    check("package games.qa {" in model.text)
                    val node = action.createFileFromTemplate(
                        "Sprite",
                        files.getInternalTemplate("AS3 Node Script"),
                        model.containingDirectory,
                    )
                    check(node.virtualFile.path.endsWith("/src/games/qa/view/Sprite.as"))
                    val doc = checkNotNull(FileDocumentManager.getInstance().getDocument(fixture.virtualFile))
                    val at = doc.text.indexOf("assertTrue(true)")
                    check(at >= 0)
                    doc.replaceString(at, at + "assertTrue(true)".length, "assertTrue(false)")
                    PsiDocumentManager.getInstance(project).commitDocument(doc)
                    val keyword = checkNotNull(fixture.findElementAt(doc.text.indexOf("function example")))
                    gutter = checkNotNull(As3TestLineMarker().getInfo(keyword)).actions.single()
                    val testsDirectory = checkNotNull(directory.findSubdirectory("tests"))
                    check(TestDiscovery.forElement(testsDirectory).single().name == "Game tests")
                }
                FileDocumentManager.getInstance().saveAllDocuments()
                val event = AnActionEvent.createFromAnAction(
                    gutter,
                    null,
                    "EditorGutter",
                    DataContext.EMPTY_CONTEXT,
                )
                gutter.actionPerformed(event)
            }
            var navigated = false
            var detail = ""
            val expires = System.nanoTime() + TimeUnit.SECONDS.toNanos(30)
            while (!navigated && System.nanoTime() < expires) {
                var result: Pair<SMTestProxy, SMTRunnerConsoleView>? = null
                ApplicationManager.getApplication().invokeAndWait {
                    val descriptors = RunContentManager.getInstance(project).allDescriptors
                    detail = descriptors.joinToString { it.displayName }
                    val consoles = descriptors.mapNotNull { it.executionConsole as? SMTRunnerConsoleView }
                    for (console in consoles) {
                        val failed = console.resultsViewer.testsRootNode.allTests.singleOrNull {
                            !it.isSuite && (it.name == "example" || it.name.endsWith("::example")) &&
                                it.isDefect && !it.isInProgress
                        }
                        if (failed != null) result = failed to console
                    }
                }
                val target = result?.let { (failed, console) ->
                    ReadAction.compute<OpenFileDescriptor?, RuntimeException> {
                        val location = failed.getLocation(project, GlobalSearchScope.allScope(project))
                        failed.getDescriptor(location, console.properties) as? OpenFileDescriptor
                    }
                }
                if (target != null) {
                    val lines = Files.readAllLines(target.file.toNioPath())
                    check(lines[target.line].trim() == "Assert.assertTrue(false);")
                    ApplicationManager.getApplication().invokeAndWait { target.navigate(true) }
                    navigated = true
                }
                if (!navigated) Thread.sleep(100)
            }
            check(navigated) { "The gutter did not open the failing assertion: $detail" }
        } finally {
            folder.toFile().deleteRecursively()
        }
    }
}
