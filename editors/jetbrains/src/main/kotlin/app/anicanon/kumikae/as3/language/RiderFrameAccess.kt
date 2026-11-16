package app.anicanon.kumikae.as3.language

import javax.swing.JFrame
import javax.swing.JRootPane

/** Exact-build bridge: validate every native signature before touching the captured frame. */
internal class RiderFrameAccess(private val frame: JFrame, root: JRootPane) {
    private val getHelper = RiderNativeMethod(frame.javaClass, "getFrameHelper")
    private val setHelper = RiderNativeMethod(frame.javaClass, "setFrameHelper", getHelper.returnType)
    private val helper = checkNotNull(getHelper.call(frame))
    private val projectFrame = checkNotNull(RiderNativeMethod(helper.javaClass, "getHelper").call(helper))
    private val isDisposed = RiderNativeMethod(projectFrame.javaClass, "isDisposed")
    private val glassPane = root.glassPane
    private val setToolbar = RiderNativeMethod(
        glassPane.javaClass,
        "setColorfulToolbar\$intellij_platform_ide_impl",
        Function0::class.java,
    )
    private val getPainters = RiderNativeMethod(glassPane.javaClass, "getNamedPainters", String::class.java)
    private val clearPainters = RiderNativeMethod(getPainters.returnType, "clear")
    private val painters = listOf("idea.background.editor", "idea.background.frame").map { name ->
        checkNotNull(getPainters.call(glassPane, name))
    }

    fun helperDisposed(): Boolean = isDisposed.call(projectFrame) == true
    fun ownsCurrentHelper(): Boolean = getHelper.call(frame) === helper

    fun release() {
        setToolbar.call(glassPane, { false })
        painters.forEach { clearPainters.call(it) }
        if (ownsCurrentHelper()) setHelper.call(frame, null)
    }
}
