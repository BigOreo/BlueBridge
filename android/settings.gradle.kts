pluginManagement {
    repositories {
        gradlePluginPortal()
        mavenCentral()
        google {
            content {
                includeGroupByRegex("com\\.android.*")
                includeGroupByRegex("androidx.*")
                includeGroupByRegex("com\\.google.*")
            }
        }
    }
    plugins {
        id("com.android.application") version "8.10.1"
        kotlin("android") version "2.1.21"
        kotlin("jvm") version "2.1.21"
    }
}

dependencyResolutionManagement {
    repositories {
        mavenCentral()
        google {
            content {
                includeGroupByRegex("com\\.android.*")
                includeGroupByRegex("androidx.*")
                includeGroupByRegex("com\\.google.*")
            }
        }
    }
}

rootProject.name = "glidekvm-android"

// The connection core is plain Kotlin and builds anywhere. The app needs the
// Android SDK, so it is only part of the build where one is installed.
include(":core")

fun androidSdk(): String? {
    System.getenv("ANDROID_HOME")?.let { return it }
    System.getenv("ANDROID_SDK_ROOT")?.let { return it }
    val local = file("local.properties")
    if (local.exists()) {
        val props = java.util.Properties()
        local.inputStream().use { props.load(it) }
        props.getProperty("sdk.dir")?.let { return it }
    }
    return null
}

if (androidSdk() != null) {
    include(":app")
}
