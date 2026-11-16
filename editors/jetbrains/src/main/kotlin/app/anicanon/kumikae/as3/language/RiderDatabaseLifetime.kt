package app.anicanon.kumikae.as3.language

import com.intellij.openapi.Disposable
import com.intellij.openapi.application.readAction
import com.intellij.openapi.components.Service
import com.intellij.openapi.components.service
import com.intellij.openapi.extensions.ExtensionPointName
import com.intellij.openapi.project.Project
import com.intellij.openapi.startup.ProjectActivity
import kotlinx.coroutines.CoroutineScope
import kotlin.coroutines.Continuation
import kotlin.coroutines.intrinsics.suspendCoroutineUninterceptedOrReturn

@Service(Service.Level.APP)
internal class RiderDatabaseLifetime : Disposable {
    @Volatile var installed = false
        private set

    @Synchronized fun install() {
        if (installed) return
        val point = ExtensionPointName.create<Any>("com.intellij.postStartupActivity")
        val original = point.findFirstSafe {
            it.javaClass.name ==
                "com.jetbrains.rider.plugins.appender.database.DatabaseConnectionDialogPromoter"
        } ?: return
        val replacement = RiderScopedActivity(original.javaClass.asSubclass(ProjectActivity::class.java)) {
            readAction { if (it.isDisposed) null else it.service<RiderProjectLifetime>().scope }
        }
        replaceRiderExtension(point.point, original, replacement, this)
        installed = true
    }

    override fun dispose() = Unit
}

/** Runs unchanged native startup behavior with a scope that ends with its project. */
internal class RiderScopedActivity(
    type: Class<out ProjectActivity>,
    private val scope: suspend (Project) -> CoroutineScope?,
) : ProjectActivity {
    private val constructor = type.getConstructor(CoroutineScope::class.java)
    private val execute = RiderNativeMethod(type, "execute", Project::class.java, Continuation::class.java)

    override suspend fun execute(project: Project) {
        if (!project.isDisposed) {
            val activity = constructor.newInstance(scope(project) ?: return)
            suspendCoroutineUninterceptedOrReturn<Unit> { execute.call(activity, project, it) }
        }
    }
}
