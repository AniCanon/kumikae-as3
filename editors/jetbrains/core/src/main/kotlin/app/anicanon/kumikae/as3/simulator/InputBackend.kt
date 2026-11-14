package app.anicanon.kumikae.as3.simulator

import com.google.gson.JsonParser
import java.nio.file.Files
import java.nio.file.Path
import java.util.concurrent.ConcurrentHashMap

interface InputBackend : AutoCloseable {
    val enabled: Boolean

    /** Screenshot pixels for one of the device's points: input is sent in points. */
    fun scale(device: String): Double

    fun supports(device: String): Boolean = enabled

    fun send(device: String, operation: String, args: List<String>)

    fun video(device: String): Pair<StreamProcess, Geometry> =
        throw SimulatorException("INPUT_DISABLED", "Live picture needs the owner's idb switch")

    override fun close() = Unit
}

object NoInput : InputBackend {
    override val enabled = false

    override fun scale(device: String) = 1.0

    override fun send(device: String, operation: String, args: List<String>): Nothing = throw SimulatorException(
        "INPUT_DISABLED",
        "Input is off. Switch it on in the panel's menu; it needs idb.",
    )
}

/**
 * Input through Meta's idb, which drives the Simulator through private Apple interfaces. It is used only
 * after the owner switches it on: in the panel, or with an environment variable for the standalone service.
 */
class IdbInput(
    private val executable: String,
    private val runner: CommandRunner,
    private val persistent: IdbDevices? = null,
) : InputBackend {
    private val scales = ConcurrentHashMap<String, Double>()

    init {
        require(Path.of(executable).isAbsolute && Files.isExecutable(Path.of(executable))) {
            "idb must be an absolute executable path"
        }
    }

    override val enabled = true

    override fun scale(device: String): Double = persistent?.device(device)?.density ?: scales.computeIfAbsent(device) {
        val density = runCatching {
            JsonParser.parseString(runner.run(Commands.describe(executable, device), TIMEOUT_MILLIS))
                .asJsonObject.getAsJsonObject("screen_dimensions")["density"].asDouble
        }.getOrNull()
        if (density == null || !density.isFinite() || density !in 1.0..10.0) {
            throw SimulatorException("INPUT_UNAVAILABLE", "idb could not describe this device's screen")
        }
        density
    }

    override fun send(device: String, operation: String, args: List<String>) {
        if (persistent != null) {
            persistent.device(device).send(operation, args)
        } else {
            runner.run(Commands.input(executable, device, operation, args), TIMEOUT_MILLIS)
        }
    }

    override fun video(device: String): Pair<StreamProcess, Geometry> {
        val connection = persistent?.device(device)
            ?: throw SimulatorException("INPUT_UNAVAILABLE", "Live idb connection is unavailable")
        return connection.video() to connection.geometry
    }

    override fun close() {
        persistent?.close()
    }

    companion object {
        private const val TIMEOUT_MILLIS = 15_000L
        private const val SWITCH = "KUMIKAE_SIMULATOR_ALLOW_PRIVATE_INPUT"
        private const val PATH = "KUMIKAE_SIMULATOR_IDB"

        /** Where idb is, looking where its installers put it and then along the search path. */
        fun find(
            home: Path = Path.of(System.getProperty("user.home")),
            pathVariable: String = System.getenv("PATH").orEmpty(),
        ): String? {
            val known =
                listOf(home.resolve(".local/bin/idb"), Path.of("/opt/homebrew/bin/idb"), Path.of("/usr/local/bin/idb"))
            val searched = pathVariable.split(':').filter { it.startsWith("/") }.map { Path.of(it, "idb") }
            return (known + searched).firstOrNull { Files.isExecutable(it) && !Files.isDirectory(it) }?.toString()
        }

        /** For the standalone service, which has no panel to ask: the owner's switch, and a path if idb is elsewhere. */
        fun fromEnvironment(runner: CommandRunner, env: Map<String, String> = System.getenv()): InputBackend {
            if (env[SWITCH] != "1") return NoInput
            val executable =
                env[PATH] ?: find() ?: error("idb was not found. Install it, or set $PATH to its full path.")
            return IdbInput(executable, runner, IdbDevices(executable))
        }
    }
}
