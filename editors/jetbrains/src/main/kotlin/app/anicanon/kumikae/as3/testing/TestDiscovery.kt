package app.anicanon.kumikae.as3.testing

import com.google.gson.JsonParser
import com.intellij.openapi.fileEditor.FileDocumentManager
import com.intellij.openapi.vfs.VfsUtilCore
import com.intellij.openapi.vfs.VirtualFile
import com.intellij.openapi.vfs.VirtualFileManager
import com.intellij.psi.PsiDirectory
import com.intellij.psi.PsiElement
import com.intellij.psi.PsiFile
import com.intellij.psi.util.CachedValueProvider
import com.intellij.psi.util.CachedValuesManager
import com.intellij.psi.util.PsiModificationTracker

internal data class RegisteredTest(val manifest: VirtualFile, val name: String, val method: String? = null)

/**
 * A file's declarations and the cases registered for it. The gutter asks about every `class` and `function`
 * keyword of a file on each highlighting pass: the file is lexed and the manifest parsed once for each change
 * to either, not once for each keyword.
 */
internal class FileTests(
    val source: TestSource,
    private val manifest: VirtualFile?,
    private val cases: List<TestSelection>,
) {
    fun at(offset: Int): List<RegisteredTest> {
        val registered = manifest ?: return emptyList()
        return TestCaseMappings.at(cases, source.methodAt(offset)?.name)
            .map { RegisteredTest(registered, it.identity, it.method) }
    }
}

internal object TestDiscovery {
    fun forElement(element: PsiElement): List<RegisteredTest> = if (element is PsiDirectory) {
        directoryTests(element)
    } else {
        element.containingFile?.let { of(it).at(element.textOffset) }.orEmpty()
    }

    private fun directoryTests(directory: PsiDirectory): List<RegisteredTest> {
        val folder = directory.virtualFile
        val root = folder.parent ?: return emptyList()
        if (folder.name != "tests") return emptyList()
        val manifest = root.findChild("kas3-tests.json") ?: return emptyList()
        val text = FileDocumentManager.getInstance().getDocument(manifest)?.text ?: return emptyList()
        return runCatching {
            val rootObject = JsonParser.parseString(text).asJsonObject
            require(rootObject["version"].asInt == 1)
            rootObject.getAsJsonArray("tests").map {
                RegisteredTest(manifest, it.asJsonObject["name"].asString)
            }
        }.getOrDefault(emptyList())
    }

    /** Kept with the file until it, any other source, or a file on disk (the manifest) changes. */
    fun of(file: PsiFile): FileTests = CachedValuesManager.getCachedValue(file) {
        CachedValueProvider.Result.create(
            read(file),
            file,
            PsiModificationTracker.MODIFICATION_COUNT,
            VirtualFileManager.getInstance(),
        )
    }

    private fun read(file: PsiFile): FileTests {
        val source = TestSource.read(file.text)
        val none = FileTests(source, null, emptyList())
        val virtual = file.virtualFile ?: return none
        val root =
            generateSequence(virtual.parent) { it.parent }.firstOrNull { it.findChild("kas3-tests.json") != null }
                ?: return none
        val manifest = root.findChild("kas3-tests.json") ?: return none
        val text =
            FileDocumentManager.getInstance().getDocument(manifest)?.immutableCharSequence?.toString() ?: return none
        val relative = VfsUtilCore.getRelativePath(virtual, root, '/') ?: return none
        if (source.suite) {
            val cases = mutableListOf<TestSelection>()
            val directory = if (source.suiteFixtures.isEmpty()) virtual.parent else root
            VfsUtilCore.iterateChildrenRecursively(directory, {
                !it.name.startsWith('.') && it.name != "build" && it.name != "bin"
            }) { entry ->
                if (entry != virtual && entry.extension == "as") {
                    val declaration = FileDocumentManager.getInstance().getDocument(entry)?.let {
                        TestSource.read(it.text)
                    }
                    val path = VfsUtilCore.getRelativePath(entry, root)
                    val fixture = declaration?.let {
                        listOf(it.packageName, it.className).filter(String::isNotBlank).joinToString(".")
                    }
                    if (path != null && declaration != null && !declaration.suite &&
                        (source.suiteFixtures.isEmpty() || fixture in source.suiteFixtures)
                    ) {
                        cases +=
                            runCatching { TestCaseMappings.forFile(text, path, declaration) }.getOrDefault(emptyList())
                    }
                }
                true
            }
            return FileTests(source, manifest, cases.distinctBy { it.identity })
        }
        val cases = runCatching { TestCaseMappings.forFile(text, relative, source) }.getOrDefault(emptyList())
        return FileTests(source, manifest, cases)
    }
}
