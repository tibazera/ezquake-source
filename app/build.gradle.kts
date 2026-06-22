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
                    "-DENABLE_LTO=OFF",
                    // AGP's externalNativeBuild only auto-maps CMAKE_BUILD_TYPE for
                    // build types it considers "debuggable" vs not; without this, our
                    // usual `assembleDebug` loop was silently compiling every .c/.cpp
                    // file with no -O flag at all (confirmed via compile_commands.json:
                    // no -O0/-O2/-O3, no -DNDEBUG) -- i.e. every perf number measured
                    // on-device this session was against an unoptimized build. Forcing
                    // RelWithDebInfo here matches the desktop Windows build's own
                    // convention (-O2 -g -DNDEBUG) while keeping debug symbols and the
                    // debuggable APK/manifest from the Gradle "debug" variant intact.
                    "-DCMAKE_BUILD_TYPE=RelWithDebInfo"
                )
            }
        }
    }

    buildTypes {
        release {
            // Signed with the debug keystore for internal testing; replace
            // with a real signing config before any public distribution.
            signingConfig = signingConfigs.getByName("debug")
        }
    }

    externalNativeBuild {
        cmake {
            path = file("../CMakeLists.txt")
            version = "3.22.1"
        }
    }
}
