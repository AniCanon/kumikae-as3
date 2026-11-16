package app.anicanon.kumikae.as3.language

import java.lang.reflect.InvocationTargetException
import java.lang.reflect.Method

/** Signature-checked access to the tested Rider build's lifecycle methods, never fields or bytecode. */
internal class RiderNativeMethod(type: Class<*>, name: String, vararg parameters: Class<*>) {
    private val method: Method = findMethod(type, name, parameters).also {
        check(it.trySetAccessible()) { "Rider lifecycle method cannot be accessed: ${type.name}.$name" }
    }

    val returnType: Class<*> get() = method.returnType

    fun call(target: Any?, vararg arguments: Any?): Any? {
        try {
            return method.invoke(target, *arguments)
        } catch (failure: InvocationTargetException) {
            throw failure.targetException
        }
    }
}

private fun findMethod(type: Class<*>, name: String, parameters: Array<out Class<*>>): Method {
    var owner: Class<*>? = type
    while (owner != null) {
        try {
            return owner.getDeclaredMethod(name, *parameters)
        } catch (_: NoSuchMethodException) {
            owner = owner.superclass
        }
    }
    return type.getMethod(name, *parameters)
}
