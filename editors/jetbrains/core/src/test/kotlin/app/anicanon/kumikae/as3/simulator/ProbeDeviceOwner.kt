package app.anicanon.kumikae.as3.simulator

/** Test-only ownership, including a create command that loses its reply after allocating a device. */
internal class ProbeDeviceOwner(private val runner: CommandRunner) {
    fun <T> use(type: String, runtime: String, work: (String) -> T): T {
        val before = devices()
        check(before.none { it.name == NAME }) { "A probe name is already in use; no device was changed" }
        var owned: String? = null
        try {
            val reply = simctl("create", NAME, type, runtime).trim()
            check(reply.matches(Regex("[A-Fa-f0-9-]{36}"))) { "Create returned an invalid device identifier" }
            owned = reply
            return work(reply)
        } finally {
            val cleanup = owned ?: devices().filter { candidate ->
                candidate.name == NAME && before.none { it.id == candidate.id }
            }.let { created ->
                check(created.size <= 1) { "Ambiguous probe creation; refusing to adopt another device" }
                created.singleOrNull()?.id
            }
            if (cleanup != null) {
                try {
                    if (devices().firstOrNull { it.id == cleanup }?.state == "Booted") simctl("shutdown", cleanup)
                } finally {
                    simctl("delete", cleanup)
                }
                check(devices().none { it.id == cleanup }) { "The owned probe was not deleted" }
                println("owned_device_deleted=true")
            }
        }
    }

    private fun devices() = Devices.parse(runner.run(Commands.list(), 15_000))
    private fun simctl(vararg args: String) = runner.run(listOf("/usr/bin/xcrun", "simctl") + args, 120_000)

    private companion object {
        const val NAME = "kas3-probe"
    }
}
