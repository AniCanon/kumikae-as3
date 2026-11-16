package app.anicanon.kumikae.as3.testing

import com.google.gson.JsonParser
import com.intellij.openapi.fileEditor.FileDocumentManager
import com.intellij.openapi.vfs.VfsUtilCore
import com.intellij.openapi.vfs.VirtualFile

internal data class RegisteredTest(val manifest: VirtualFile, val name: String)

internal object TestDiscovery {
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
            val objectValue = JsonParser.parseString(source).asJsonObject
            if (objectValue["version"].asInt != 1) return emptyList()
            objectValue.getAsJsonArray("tests").mapNotNull { entry ->
                val test = entry.asJsonObject
                if (test["source"]?.asString == relative) RegisteredTest(manifest, test["name"].asString) else null
            }
        }.getOrDefault(emptyList())
    }
}
