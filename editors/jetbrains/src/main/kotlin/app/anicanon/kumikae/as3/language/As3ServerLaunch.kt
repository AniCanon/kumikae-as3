package app.anicanon.kumikae.as3.language

import app.anicanon.kumikae.as3.PluginFiles
import com.intellij.execution.configurations.GeneralCommandLine
import com.intellij.openapi.diagnostic.Logger
import com.intellij.openapi.project.Project
import java.io.IOException
import java.nio.file.Files
import java.nio.file.Path

/** Shared wire options do not load either IDE's language-client classes. */
internal fun serverOptions(): Map<String, String> = mapOf("codeLanguage" to As3Language.id)

internal fun serverCommandLine(compiler: Path, project: Project): GeneralCommandLine {
    val args = mutableListOf(compiler.toString(), "lsp")
    val projectRoot = project.basePath?.let(Path::of)
    for (declaration in bundledTestDeclarations(PluginFiles.native(), projectRoot)) {
        args.addAll(listOf("--declarations", declaration.toString()))
    }
    val trace = System.getenv("KUMIKAE_AS3_LSP_TRACE")?.takeIf { it.isNotBlank() } ?: return GeneralCommandLine(args)
    val script = "trace=\$1; shift; tee \"\$trace/to-server.log\" | \"\$@\" | tee \"\$trace/from-server.log\""
    return GeneralCommandLine(listOf("/bin/sh", "-c", script, "sh", trace) + args)
}

internal fun bundledTestDeclarations(nativeRoot: Path?, projectRoot: Path?): List<Path> {
    if (nativeRoot == null || projectRoot == null) return emptyList()
    val relative = "app/anicanon/kumikae/testing"
    if (Files.exists(projectRoot.resolve("tests/framework/$relative/TestCase.as"))) return emptyList()
    return listOf("as3", "godot").flatMap { tier ->
        val library = nativeRoot.resolve("testing/$tier/$relative")
        if (!Files.isDirectory(library)) return@flatMap emptyList()
        try {
            Files.list(library).use { files ->
                files.filter { Files.isRegularFile(it) && it.fileName.toString().endsWith(".as") }
                    .sorted()
                    .toList()
            }
        } catch (error: IOException) {
            Logger.getInstance("app.anicanon.kumikae.as3.language.As3ServerLaunch")
                .warn("Reading bundled AS3 test declarations from $library", error)
            emptyList()
        }
    }
}
