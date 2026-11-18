package app.anicanon.kumikae.as3.language

import com.intellij.execution.configurations.GeneralCommandLine
import java.nio.file.Path

/** Shared wire options do not load either IDE's language-client classes. */
internal fun serverOptions(): Map<String, String> = mapOf("codeLanguage" to As3Language.id)

internal fun serverCommandLine(compiler: Path, project: com.intellij.openapi.project.Project): GeneralCommandLine {
    val args = mutableListOf(compiler.toString(), "lsp")
    val relative = "app/anicanon/kumikae/testing"
    val library = app.anicanon.kumikae.as3.PluginFiles.native()?.resolve("testing/as3/$relative")
    val projectRoot = project.basePath?.let(Path::of)
    if (library != null && java.nio.file.Files.isDirectory(library) &&
        projectRoot != null && !java.nio.file.Files.exists(projectRoot.resolve("tests/framework/$relative/TestCase.as"))
    ) {
        java.nio.file.Files.list(library).use { files ->
            files.filter { it.fileName.toString().endsWith(".as") }.sorted().forEach {
                args.addAll(listOf("--declarations", it.toString()))
            }
        }
    }
    val trace = System.getenv("KUMIKAE_AS3_LSP_TRACE")?.takeIf { it.isNotBlank() } ?: return GeneralCommandLine(args)
    val script = "trace=\$1; shift; tee \"\$trace/to-server.log\" | \"\$@\" | tee \"\$trace/from-server.log\""
    return GeneralCommandLine(listOf("/bin/sh", "-c", script, "sh", trace) + args)
}
