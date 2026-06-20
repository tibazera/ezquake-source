plugins {
    id("com.android.application")
}

val sdkRoot = System.getenv("ANDROID_HOME")
    ?: System.getenv("ANDROID_SDK_ROOT")
    ?: "${System.getProperty("user.home")}/Android/Sdk"
val ndkVersionPinned = "28.2.13676358"
val ndkRoot = System.getenv("ANDROID_NDK_HOME")
    ?: "$sdkRoot/ndk/$ndkVersionPinned"

android {
    namespace = "org.ezquake.android"
    compileSdk = 36
    ndkVersion = ndkVersionPinned

    defaultConfig {
        applicationId = "org.ezquake.android"
        minSdk = 29
        targetSdk = 36
        versionCode = 1
        versionName = "0.1.0"

        ndk {
            abiFilters += "arm64-v8a"
        }

        externalNativeBuild {
            cmake {
                arguments += listOf(
                    "-DCMAKE_TOOLCHAIN_FILE=${rootDir}/vcpkg/scripts/buildsystems/vcpkg.cmake",
                    "-DVCPKG_CHAINLOAD_TOOLCHAIN_FILE=$ndkRoot/build/cmake/android.toolchain.cmake",
                    "-DVCPKG_TARGET_TRIPLET=arm64-android",
                    "-DUSE_SYSTEM_LIBS=OFF",
                    "-DANDROID_ABI=arm64-v8a",
                    "-DANDROID_PLATFORM=android-29",
                    "-DRENDERER_CLASSIC_OPENGL=OFF",
                    "-DRENDERER_MODERN_OPENGL=OFF",
                    "-DRENDERER_VULKAN=ON",
                    "-DENABLE_LTO=OFF"
                )
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("../CMakeLists.txt")
            version = "3.22.1"
        }
    }
}
