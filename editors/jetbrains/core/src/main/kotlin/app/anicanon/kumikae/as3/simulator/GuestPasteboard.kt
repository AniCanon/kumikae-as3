package app.anicanon.kumikae.as3.simulator

fun interface GuestPasteboard {
    fun copy(device: String, text: String)
}

class SimctlPasteboard(private val runner: CommandRunner) : GuestPasteboard {
    override fun copy(device: String, text: String) {
        SimulatorText.validate(text)
        runner.runWithInput(
            listOf("/usr/bin/xcrun", "simctl", "pbcopy", device),
            15_000,
            text.toByteArray(Charsets.UTF_8),
        )
    }
}
