package app.anicanon.kumikae.as3.language

import org.junit.jupiter.api.Test
import kotlin.test.assertEquals

class DeclarationRefreshTest {
    @Test fun `many opened files share one bounded refresh and close rejects late work`() {
        val pending = ArrayDeque<() -> Unit>()
        var calls = 0
        var cancelled = 0
        val refresh = DeclarationRefresh(
            schedule = { _, work -> pending.addLast(work) },
            cancel = { cancelled++ },
            refresh = {
                calls++
                false
            },
        )
        repeat(100) { refresh.start() }
        assertEquals(1, pending.size)
        repeat(20) { if (pending.isNotEmpty()) pending.removeFirst().invoke() }
        assertEquals(20, calls)
        assertEquals(0, pending.size)
        refresh.close()
        refresh.start()
        assertEquals(0, pending.size)
        assertEquals(1, cancelled)
    }

    @Test fun `queued work cannot refresh after project disposal`() {
        val pending = ArrayDeque<() -> Unit>()
        var calls = 0
        val refresh = DeclarationRefresh({ _, work -> pending.addLast(work) }, {}, {
            calls++
            true
        })
        refresh.start()
        assertEquals(1, pending.size)
        refresh.close()
        pending.removeFirst().invoke()
        assertEquals(0, calls)
    }

    @Test fun `a discovered declaration ends polling immediately`() {
        val pending = ArrayDeque<() -> Unit>()
        var calls = 0
        val refresh = DeclarationRefresh({ _, work -> pending.addLast(work) }, {}, {
            calls++
            true
        })
        refresh.start()
        pending.removeFirst().invoke()
        refresh.start()
        assertEquals(1, calls)
        assertEquals(0, pending.size)
    }
}
