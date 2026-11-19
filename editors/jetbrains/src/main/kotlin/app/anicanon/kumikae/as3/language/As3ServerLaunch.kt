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
    val relative = "app/anicanon/kumikae/testing"
    val library = PluginFiles.native()?.resolve("testing/as3/$relative")
    val projectRoot = project.basePath?.let(Path::of)
    if (library != null && Files.isDirectory(library) &&
        projectRoot != null && !Files.exists(projectRoot.resolve("tests/framework/$relative/TestCase.as"))
    ) {
        try {
            Files.list(library).use { files ->
                files.filter { Files.isRegularFile(it) && it.fileName.toString().endsWith(".as") }.sorted().forEach {
                    args.addAll(listOf("--declarations", it.toString()))
                }
            }
        } catch (error: IOException) {
            Logger.getInstance("app.anicanon.kumikae.as3.language.As3ServerLaunch")
                .warn("Reading bundled AS3 test declarations from $library", error)
        }
    }
    val trace = System.getenv("KUMIKAE_AS3_LSP_TRACE")?.takeIf { it.isNotBlank() } ?: return GeneralCommandLine(args)
    val script = "trace=\$1; shift; tee \"\$trace/to-server.log\" | \"\$@\" | tee \"\$trace/from-server.log\""
    return GeneralCommandLine(listOf("/bin/sh", "-c", script, "sh", trace) + args)
}
