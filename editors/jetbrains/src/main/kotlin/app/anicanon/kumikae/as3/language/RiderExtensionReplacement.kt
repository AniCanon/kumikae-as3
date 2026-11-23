package app.anicanon.kumikae.as3.language

import com.intellij.openapi.Disposable
import com.intellij.openapi.extensions.ExtensionPoint
import com.intellij.openapi.extensions.LoadingOrder

// The class overload removes a matching registration, which can be a different instance of the same
// class. Both the replacement and rollback must remove the exact instance (RiderExtensionReplacementTest).
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
