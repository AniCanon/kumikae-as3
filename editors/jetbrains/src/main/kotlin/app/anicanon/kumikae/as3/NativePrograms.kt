package app.anicanon.kumikae.as3

import java.nio.file.Files
import java.nio.file.Path

/**
 * The programs the plugin carries. An IDE's plugin installer may unpack them without their execute bit, and
 * a program that can't be run is passed over (the compiler search takes only executable files), so the
 * plugin gives the bit back itself.
 */
internal object NativePrograms {
    private val PROGRAMS = listOf("kas3c", "simulator-bridge")

    fun makeRunnable(folder: Path) {
        PROGRAMS.map(folder::resolve)
            .filter { Files.isRegularFile(it) && !Files.isExecutable(it) }
            .forEach { it.toFile().setExecutable(true, false) }
    }
}
