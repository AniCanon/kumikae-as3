package app.anicanon.kumikae.as3.language

import com.intellij.openapi.Disposable
import com.intellij.openapi.application.AccessToken

/** One native idle subscription, including callbacks racing its registration or disposal. */
internal class OwnedIdleListener(deliver: (AccessToken) -> Unit) : Disposable {
    private val lock = Any()
    private var deliver: ((AccessToken) -> Unit)? = deliver
    private var token: AccessToken? = null
    private var registered = false
    private var pending = false
    private val completion = object : AccessToken() {
        override fun finish() = dispose()
    }

    fun register(factory: (Runnable) -> AccessToken) {
        synchronized(lock) {
            if (deliver == null) return
            check(!registered) { "Idle listener is already registered" }
            registered = true
        }
        val created = try {
            factory(Runnable { fire() })
        } catch (failure: Throwable) {
            dispose()
            throw failure
        }
        val (finish, notify) = synchronized(lock) {
            if (deliver == null) {
                true to false
            } else {
                token = created
                false to pending.also { pending = false }
            }
        }
        if (finish) created.finish()
        if (notify) fire()
    }

    private fun fire() {
        val callback = synchronized(lock) {
            if (token == null) {
                pending = deliver != null
                null
            } else {
                deliver
            }
        }
        callback?.invoke(completion)
    }

    override fun dispose() {
        val previous = synchronized(lock) {
            if (deliver == null) return
            deliver = null
            pending = false
            token.also { token = null }
        }
        previous?.finish()
    }
}
