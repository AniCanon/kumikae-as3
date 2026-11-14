plugins {
    kotlin("jvm")
    application
}
repositories { mavenCentral() }
dependencies {
    implementation(kotlin("stdlib"))
    implementation("com.google.code.gson:gson:2.13.2")
    testImplementation("junit:junit:4.13.2")
}
kotlin { jvmToolchain(21) }
application { mainClass = "org.kumikae.simulator.MainKt" }
tasks.test { useJUnit() }
