package app.anicanon.kumikae.as3.panel

import app.anicanon.kumikae.as3.project.NewAs3FileAction
import app.anicanon.kumikae.as3.project.ProjectTemplate
import app.anicanon.kumikae.as3.testing.TestDiscovery
import com.intellij.ide.fileTemplates.FileTemplateManager
import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.command.WriteCommandAction
import com.intellij.openapi.fileEditor.FileDocumentManager
import com.intellij.openapi.project.Project
import com.intellij.openapi.vfs.LocalFileSystem
import com.intellij.psi.PsiManager
import java.nio.file.Files
import java.nio.file.Path

internal object UiProbeAuthoring {
    fun run(project: Project, root: Path) {
        val folder = Files.createTempDirectory(root, "new-game")
        try {
            ProjectTemplate("New Game", emptySet(), "games.qa").write(folder, null)
            lateinit var gutter: com.intellij.openapi.actionSystem.AnAction
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
                    com.intellij.psi.PsiDocumentManager.getInstance(project).commitDocument(doc)
                    val keyword = checkNotNull(fixture.findElementAt(doc.text.indexOf("function example")))
                    gutter =
                        checkNotNull(
                            app.anicanon.kumikae.as3.testing.As3TestLineMarker().getInfo(keyword),
                        ).actions.single()
                    val testsDirectory = checkNotNull(directory.findSubdirectory("tests"))
                    check(TestDiscovery.forElement(testsDirectory).single().name == "Game tests")
                }
                FileDocumentManager.getInstance().saveAllDocuments()
                val event = com.intellij.openapi.actionSystem.AnActionEvent.createFromAnAction(
                    gutter,
                    null,
                    "EditorGutter",
                    com.intellij.openapi.actionSystem.DataContext.EMPTY_CONTEXT,
                )
                gutter.actionPerformed(event)
            }
            var navigated = false
            var detail = ""
            val expires = System.nanoTime() + java.util.concurrent.TimeUnit.SECONDS.toNanos(30)
            while (!navigated && System.nanoTime() < expires) {
                ApplicationManager.getApplication().invokeAndWait {
                    val descriptors = com.intellij.execution.ui.RunContentManager.getInstance(project).allDescriptors
                    detail = descriptors.joinToString { it.displayName }
                    val console = descriptors.mapNotNull {
                        it.executionConsole as? com.intellij.execution.testframework.sm.runner.ui.SMTRunnerConsoleView
                    }.firstOrNull { view ->
                        view.resultsViewer.testsRootNode.allTests.any { it.name.endsWith("::example") }
                    }
                    val failed = console?.resultsViewer?.testsRootNode?.allTests?.singleOrNull {
                        it.name.endsWith("::example") && it.isDefect && !it.isInProgress
                    } ?: return@invokeAndWait
                    val location = failed.getLocation(
                        project,
                        com.intellij.psi.search.GlobalSearchScope.allScope(project),
                    )
                    val target = failed.getDescriptor(location, console.properties)
                        as? com.intellij.openapi.fileEditor.OpenFileDescriptor ?: return@invokeAndWait
                    val lines = Files.readAllLines(target.file.toNioPath())
                    check(lines[target.line].trim() == "Assert.assertTrue(false);")
                    target.navigate(true)
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
