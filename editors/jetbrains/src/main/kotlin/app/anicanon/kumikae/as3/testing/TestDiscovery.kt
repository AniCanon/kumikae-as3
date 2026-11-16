package app.anicanon.kumikae.as3.testing

import com.intellij.openapi.fileEditor.FileDocumentManager
import com.intellij.openapi.vfs.VfsUtilCore
import com.intellij.openapi.vfs.VirtualFile
import com.intellij.openapi.vfs.VirtualFileManager
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
    fun forElement(element: PsiElement): List<RegisteredTest> =
        element.containingFile?.let { of(it).at(element.textOffset) }.orEmpty()

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
        val cases = runCatching { TestCaseMappings.forFile(text, relative) }.getOrDefault(emptyList())
        return FileTests(source, manifest, cases)
    }
}
