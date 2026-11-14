package org.kumikae.simulator

import java.nio.file.Files
import java.nio.file.Path

interface InputBackend {
    val enabled: Boolean
    val scale: Double
    fun supports(device: String): Boolean = enabled
    fun send(device: String, operation: String, args: List<String>)
}

object NoInput : InputBackend {
    override val enabled = false
    override val scale = 1.0
    override fun send(device: String, operation: String, args: List<String>): Nothing =
        throw SimulatorException("INPUT_DISABLED", "Input needs an explicitly enabled idb installation. Use Device Hub for input.")
}

class IdbInput(private val executable: String, private val targetDevice: String, override val scale: Double,
               private val runner: CommandRunner) : InputBackend {
    init {
        require(Path.of(executable).isAbsolute && Files.isExecutable(Path.of(executable))) { "idb must be an absolute executable path" }
        require(scale.isFinite() && scale in 1.0..10.0) { "Set the selected device's pixels per point (1–10)" }
    }
    override val enabled = true
    override fun supports(device: String) = device == targetDevice
    override fun send(device: String, operation: String, args: List<String>) {
        if (!supports(device)) throw SimulatorException("INPUT_DEVICE_MISMATCH", "idb scale is configured for a different device")
        runner.run(Commands.input(executable, device, operation, args), 15_000)
    }

    companion object {
        fun fromEnvironment(runner: CommandRunner, env: Map<String, String> = System.getenv()): InputBackend {
            if (env["KUMIKAE_SIMULATOR_ALLOW_PRIVATE_INPUT"] != "1") return NoInput
            val executable = env["KUMIKAE_SIMULATOR_IDB"] ?: error("Set KUMIKAE_SIMULATOR_IDB")
            val scale = env["KUMIKAE_SIMULATOR_SCALE"]?.toDoubleOrNull() ?: error("Set KUMIKAE_SIMULATOR_SCALE")
            val device = env["KUMIKAE_SIMULATOR_INPUT_DEVICE"]?.takeIf { it.isNotBlank() }
                ?: error("Set KUMIKAE_SIMULATOR_INPUT_DEVICE to the device for that scale")
            return IdbInput(executable, device, scale, runner)
        }
    }
}
