package app.anicanon.kumikae.as3.language

import com.intellij.openapi.Disposable
import com.intellij.openapi.extensions.ExtensionPoint
import com.intellij.openapi.extensions.LoadingOrder

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
