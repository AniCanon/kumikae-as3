package app.anicanon.kumikae.as3.language

import com.intellij.openapi.project.Project
import com.intellij.openapi.startup.ProjectActivity
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.Job
import kotlinx.coroutines.awaitCancellation
import kotlinx.coroutines.cancel
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import org.junit.jupiter.api.Test
import java.lang.reflect.Proxy
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class RiderScopedActivityTest {
    class NativeActivity(private val scope: CoroutineScope) : ProjectActivity {
        override suspend fun execute(project: Project) {
            scope.launch(start = CoroutineStart.UNDISPATCHED) { awaitCancellation() }
        }
    }

    @Test fun `native work ends with its project scope while another project stays active`() = runBlocking {
        val first = Job()
        val second = Job()
        val firstProject = project(false)
        val activity = RiderScopedActivity(NativeActivity::class.java) {
            CoroutineScope(if (it === firstProject) first else second)
        }
        try {
            activity.execute(firstProject)
            activity.execute(project(false))
            assertEquals(1, first.children.count())
            assertEquals(1, second.children.count())
            first.cancel()
            first.join()
            assertTrue(first.isCompleted)
            assertFalse(second.isCancelled)
            assertEquals(1, second.children.count())
        } finally {
            first.cancel()
            second.cancel()
        }
    }

    @Test fun `disposed project does not start native work`() = runBlocking {
        var requested = false
        RiderScopedActivity(NativeActivity::class.java) {
            requested = true
            this
        }.execute(project(true))
        assertFalse(requested)
    }

    @Test fun `project closing during scope lookup does not start native work`() = runBlocking {
        RiderScopedActivity(NativeActivity::class.java) { null }.execute(project(false))
    }

    private fun project(disposed: Boolean): Project = Proxy.newProxyInstance(
        Project::class.java.classLoader,
        arrayOf(Project::class.java),
    ) { _, method, _ ->
        check(method.name == "isDisposed")
        disposed
    } as Project
}
