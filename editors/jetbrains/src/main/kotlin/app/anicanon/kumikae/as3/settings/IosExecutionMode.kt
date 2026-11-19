package app.anicanon.kumikae.as3.settings

internal enum class IosExecutionMode(val value: Int, val label: String, val detail: String) {
    Automatic(2, "Automatic", "Debug uses the interpreter for hot reload. Release compiles AS3 to native code."),
    Interpreter(0, "Interpreter", "Debug and Release both run AS3 on the interpreter."),
    Native(1, "Native", "Debug and Release both compile AS3 to native code. Debug still supports hot reload."),
    ;

    override fun toString() = label

    companion object {
        fun read(value: String?) = when (value) {
            "false", "0" -> Interpreter
            "true", "1" -> Native
            else -> Automatic
        }
    }
}
