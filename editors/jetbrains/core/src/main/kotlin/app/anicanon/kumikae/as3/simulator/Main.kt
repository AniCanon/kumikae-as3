package app.anicanon.kumikae.as3.simulator

import java.nio.file.Path
import java.util.concurrent.CountDownLatch

fun main(args: Array<String>) {
    require(System.getProperty("os.name").startsWith("Mac")) { "The iOS Simulator service requires macOS" }
    require(args.size == 2 && args[0] == "--state-dir") { "Usage: kumikae-simulator --state-dir <local directory>" }
    val session = LocalSession(Path.of(args[1]).toAbsolutePath())
    val stopped = CountDownLatch(1)
    Runtime.getRuntime().addShutdownHook(Thread {
        try { session.close() } finally { stopped.countDown() }
    })
    println("Endpoint file: ${session.server.endpointFile}")
    stopped.await()
}
