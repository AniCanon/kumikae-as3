package app.anicanon.kumikae.as3.language

import com.intellij.openapi.diagnostic.Logger

internal fun repairRider(name: String, action: () -> Unit) = runRiderRepair(
    name,
    { operation, cause -> Logger.getInstance("kumikae.RiderLifetime").warn("Rider $operation repair failed", cause) },
    action,
)

internal fun runRiderRepair(name: String, failure: (String, Exception) -> Unit, action: () -> Unit) {
    try {
        action()
    } catch (cause: Exception) {
        failure(name, cause)
    }
}
