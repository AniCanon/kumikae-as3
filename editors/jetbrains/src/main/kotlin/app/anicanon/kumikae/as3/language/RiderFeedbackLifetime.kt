package app.anicanon.kumikae.as3.language

import com.intellij.ide.IdleTracker
import com.intellij.openapi.Disposable
import com.intellij.openapi.application.AccessToken
import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.application.readAction
import com.intellij.openapi.components.Service
import com.intellij.openapi.components.service
import com.intellij.openapi.extensions.ExtensionPointName
import com.intellij.openapi.project.Project
import com.intellij.openapi.startup.ProjectActivity
import com.intellij.openapi.util.Disposer
import com.intellij.openapi.util.registry.RegistryManager
import com.intellij.util.Time
import kotlin.coroutines.Continuation
import kotlin.coroutines.intrinsics.suspendCoroutineUninterceptedOrReturn

@Service(Service.Level.APP)
internal class RiderFeedbackLifetime : Disposable {
    @Volatile var installed = false
        private set

    @Synchronized fun install() {
        if (installed || ApplicationManager.getApplication().isHeadlessEnvironment) return
        val name = ExtensionPointName.create<Any>("com.intellij.backgroundPostStartupActivity")
        val original = name.findFirstSafe {
            it.javaClass.name == "com.intellij.performanceTesting.freezes.promo.ErrorReportPromoterActivity"
        } ?: return
        val replacement = ProjectOwnedFeedback(original.javaClass.classLoader)
        replaceRiderExtension(name.point, original, replacement, this)
        installed = true
    }

    override fun dispose() = Unit
}

/** Keeps Rider's feedback service and eligibility; only the idle subscription changes ownership. */
private class ProjectOwnedFeedback(loader: ClassLoader) : ProjectActivity {
    private val serviceType = loader.loadClass(
        "com.intellij.performanceTesting.freezes.promo.ErrorReportPromoterService",
    )
    private val processIdle =
        RiderNativeMethod(serviceType, "processIdleEvent", Project::class.java, AccessToken::class.java)
    private val eligibility = RiderNativeMethod(
        loader.loadClass("com.intellij.performanceTesting.freezes.promo.ErrorReportPromoterActivityKt"),
        "isEligible",
        Continuation::class.java,
    )
    private val addIdle = RiderNativeMethod(
        IdleTracker::class.java,
        "addIdleListener",
        Int::class.javaPrimitiveType!!,
        Runnable::class.java,
    )

    override suspend fun execute(project: Project) {
        if (!suspendCoroutineUninterceptedOrReturn<Boolean> { eligibility.call(null, it) }) return
        val delay = RegistryManager.getInstanceAsync().intValue("platform.feedback.time.to.show.notification", 600) *
            Time.SECOND
        val services = readAction {
            if (project.isDisposed) {
                null
            } else {
                checkNotNull(project.getService(serviceType)) to
                    project.service<RiderProjectLifetime>()
            }
        } ?: return
        val (nativeService, owner) = services
        val owned = OwnedIdleListener { token ->
            if (!project.isDisposed) processIdle.call(nativeService, project, token)
        }
        if (!Disposer.tryRegister(owner, owned)) {
            owned.dispose()
            return
        }
        owned.register { addIdle.call(IdleTracker.getInstance(), delay, it) as AccessToken }
    }
}
