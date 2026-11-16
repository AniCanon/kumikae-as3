package app.anicanon.kumikae.as3.testing

import com.intellij.openapi.fileEditor.FileDocumentManager
import com.intellij.openapi.vfs.VfsUtilCore
import com.intellij.openapi.vfs.VirtualFile

internal data class RegisteredTest(val manifest: VirtualFile, val name: String, val method: String? = null)

internal object TestDiscovery {
    fun forElement(element: com.intellij.psi.PsiElement): List<RegisteredTest> {
        val file = element.containingFile ?: return emptyList()
        val tests = forFile(file.virtualFile ?: return emptyList())
        val method = TestSource.read(file.text).methodAt(element.textOffset)?.name
        if (method == null) return tests
        val exact = tests.filter { it.method == method }
        return exact.ifEmpty { tests.filter { it.method == null } }
    }

    fun forFile(file: VirtualFile): List<RegisteredTest> {
        val root =
            generateSequence(file.parent) { it.parent }.firstOrNull { it.findChild("kas3-tests.json") != null }
                ?: return emptyList()
        val manifest = root.findChild("kas3-tests.json") ?: return emptyList()
        val source =
            FileDocumentManager.getInstance().getDocument(manifest)?.immutableCharSequence?.toString()
                ?: return emptyList()
        val relative = VfsUtilCore.getRelativePath(file, root, '/') ?: return emptyList()
        return runCatching {
            TestCaseMappings.forFile(source, relative).map { RegisteredTest(manifest, it.identity, it.method) }
        }.getOrDefault(emptyList())
    }
}
