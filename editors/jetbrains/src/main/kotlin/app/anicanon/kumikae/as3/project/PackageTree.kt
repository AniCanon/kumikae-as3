package app.anicanon.kumikae.as3.project

internal data class PackageFolder(val name: String, val files: Int, val children: List<PackageFolder> = emptyList())

internal fun compactPackage(folder: PackageFolder): List<String> {
    val names = mutableListOf(folder.name)
    var current = folder
    while (current.files == 0 && current.children.size == 1) {
        current = current.children.single()
        names += current.name
    }
    return names
}
