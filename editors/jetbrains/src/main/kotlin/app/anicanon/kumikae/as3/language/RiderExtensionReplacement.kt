package app.anicanon.kumikae.as3.language

import com.intellij.openapi.Disposable
import com.intellij.openapi.extensions.ExtensionPoint
import com.intellij.openapi.extensions.LoadingOrder

// Removal by instance is deprecated, and the only undeprecated removals go by class or through the
// platform's internal adapter type. By class would also remove unrelated extensions of the same class
// (see RiderExtensionReplacementTest), so the instance form stays, in both supported builds.
@Suppress("DEPRECATION")
internal fun <T : Any> replaceRiderExtension(
    point: ExtensionPoint<T>,
    original: T,
    replacement: T,
    owner: Disposable,
    order: LoadingOrder = LoadingOrder.ANY,
) {
    point.registerExtension(replacement, order, owner)
    try {
        point.unregisterExtension(original)
    } catch (failure: Throwable) {
        try {
            point.unregisterExtension(replacement)
        } catch (rollback: Throwable) {
            failure.addSuppressed(rollback)
        }
        throw failure
    }
}
