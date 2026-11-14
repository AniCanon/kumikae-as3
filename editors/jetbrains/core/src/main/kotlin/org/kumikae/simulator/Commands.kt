package org.kumikae.simulator

object Commands {
    private fun simctl(vararg args: String) = listOf("/usr/bin/xcrun", "simctl") + args
    fun list() = simctl("list", "-j", "devices", "available")
    fun boot(device: String) = simctl("bootstatus", device, "-b")
    fun screenshot(device: String, path: String, format: String) =
        simctl("io", device, "screenshot", "--type=$format", path)
    fun install(device: String, path: String) = simctl("install", device, path)
    fun launch(device: String, bundle: String) =
        simctl("launch", "--terminate-running-process", "--console", device, bundle)
    fun container(device: String, bundle: String) = simctl("get_app_container", device, bundle, "data")
    fun input(executable: String, device: String, operation: String, args: List<String>): List<String> =
        listOf(executable, "ui", operation, "--udid", device) +
            (if (operation == "text") listOf("--") else emptyList()) + args
}
