# ezQuake — Modern QuakeWorld Client
Homepage: [https://ezquake.com][homepage]

Community discord: [http://discord.quake.world][discord]

This is the right place to start playing QuakeWorld&reg; — the fastest first
person shooter action game ever.

Combining the features of all modern QuakeWorld® clients, ezQuake makes
QuakeWorld&reg; easier to start and play. The immortal first person shooter
Quake&reg; in the brand new skin with superb graphics and extremely fast
gameplay.

## Features

 * Modern graphics
 * [QuakeTV][qtv] support
 * Rich menus
 * Multiview support
 * Tons of features to serve latest pro-gaming needs
 * Built in server browser & MP3 player control
 * Recorded games browser
 * Customization of all possible graphics elements of the game including Heads Up Display
 * All sorts of scripting possibilities
 * Windows, Linux, MacOSX and FreeBSD platforms supported (SDL2).

Our client comes only with bare minimum of game media. If you want to
experience ezQuake with modern graphics and other additional media including
custom configurations, maps, textures and more, try using the [nQuake][nQuake]-installer.

## Support

Need help with using ezQuake? Try #dev-corner on [discord][discord]

Or (less populated these days) visit us on IRC at QuakeNet, channel #ezQuake: [webchat][webchat] or [IRC][IRC].

Sometimes help from other users of ezQuake might be more useful to you so you
can also try visiting the [quakeworld.nu Client Talk-forums][forum].

If you have found a bug, please report it [here][issues]

## Installation guide

To play Quakeworld you need the files *pak0.pak* and *pak1.pak* from the original Quake-game.

### Install ezQuake to an existing Quake-installation
If you have an existing Quake-installation simply extract the ezQuake executable into your Quake-directory.

A typical error message when installing ezQuake into a pre-existing directory is about *glide2x.dll* missing.
To get rid of this error, remove the file *opengl32.dll* from your Quake directory.

### Upgrade an nQuake-installation
If you have a version of [nQuake][nQuake] already installed you can upgrade ezQuake by extracting the new executable into the nQuake-directory.

### Minimal clean installation
If you want to make a clean installation of ezQuake you can do this by following these steps:

1. Create a new directory
2. Extract the ezQuake-executable into this directory
3. Create a subdirectory called *id1*
4. Copy *pak0.pak* and *pak1.pak* into this subdirectory

## Compiling

On Linux, `./build-linux.sh` produces an ezQuake binary in the top directory. 

For a more in-depth description of how to build on all platforms, have a look at 
[BUILD.md](BUILD.md).

## Android build

The Android port is built with Gradle, CMake, vcpkg, SDL2, Oboe, and Vulkan.
Gradle drives the Android native build and packages `libezquake.so` into the APK.

The current Android build is:

* `arm64-v8a` only
* Vulkan renderer only
* Oboe audio backend
* min SDK 29, target/compile SDK 36
* pinned Android NDK `28.2.13676358`
* CMake `3.22.1` from the Android SDK

At runtime the APK expects Quake/nQuake data in:

```text
/storage/emulated/0/Documents/ezQuake
```

The minimum data layout is:

```text
Documents/ezQuake/
  id1/
    pak0.pak
    pak1.pak
```

### Android build prerequisites

On Ubuntu, install the host tools, native desktop dependencies, Vulkan shader
compiler, and a JDK:

```sh
sudo apt update
sudo apt install -y \
  git curl zip unzip tar \
  build-essential cmake ninja-build pkg-config \
  autoconf automake libtool \
  glslang-tools openjdk-17-jdk \
  libsdl2-dev libjansson-dev libexpat1-dev libcurl4-openssl-dev \
  libpng-dev libjpeg-dev libspeex-dev libspeexdsp-dev \
  libfreetype-dev libsndfile1-dev libpcre2-dev libminizip-dev \
  libgl1-mesa-dev
```

Install the Android SDK command-line tools and the SDK packages used by this
project:

```sh
export ANDROID_HOME="$HOME/Android/Sdk"
export ANDROID_SDK_ROOT="$ANDROID_HOME"
mkdir -p "$ANDROID_HOME/cmdline-tools"

curl -L \
  "https://dl.google.com/android/repository/commandlinetools-linux-13114758_latest.zip" \
  -o /tmp/android-commandlinetools.zip

rm -rf /tmp/android-cmdline-tools "$ANDROID_HOME/cmdline-tools/latest"
mkdir -p /tmp/android-cmdline-tools
unzip -q /tmp/android-commandlinetools.zip -d /tmp/android-cmdline-tools
mv /tmp/android-cmdline-tools/cmdline-tools "$ANDROID_HOME/cmdline-tools/latest"

export PATH="$ANDROID_HOME/cmdline-tools/latest/bin:$ANDROID_HOME/platform-tools:$PATH"

yes | sdkmanager --licenses
sdkmanager \
  "platform-tools" \
  "platforms;android-36" \
  "build-tools;36.0.0" \
  "cmake;3.22.1" \
  "ndk;28.2.13676358"
```

For future shells, keep these exports in your shell profile:

```sh
export ANDROID_HOME="$HOME/Android/Sdk"
export ANDROID_SDK_ROOT="$ANDROID_HOME"
export ANDROID_NDK_HOME="$ANDROID_HOME/ndk/28.2.13676358"
export PATH="$ANDROID_HOME/cmdline-tools/latest/bin:$ANDROID_HOME/platform-tools:$PATH"
```

### End-to-end build commands

From a fresh checkout, initialize submodules and vcpkg:

```sh
git submodule update --init --recursive
./bootstrap.sh
```

Build a native Linux desktop binary:

```sh
./build-linux.sh
```

The desktop binary is produced under `build/`, for example
`build/ezquake-linux-x86_64`.

Build the Android native library and debug APK:

```sh
export ANDROID_HOME="$HOME/Android/Sdk"
export ANDROID_SDK_ROOT="$ANDROID_HOME"
export ANDROID_NDK_HOME="$ANDROID_HOME/ndk/28.2.13676358"

./gradlew :app:assembleDebug
```

The APK is written to:

```text
app/build/outputs/apk/debug/app-debug.apk
```

To install it on a connected Android device:

```sh
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

To copy a local nQuake-style data tree to the device:

```sh
adb shell 'mkdir -p /storage/emulated/0/Documents/ezQuake'
adb push /path/to/nquake/. /storage/emulated/0/Documents/ezQuake/
```

## Nightly builds

Nightly builds can be found [here][nightly]

 [nQuake]: http://nquake.com/
 [webchat]: http://webchat.quakenet.org/?channels=#ezquake
 [IRC]: irc://irc.quakenet.org/#ezquake
 [forum]: http://www.quakeworld.nu/forum/8
 [qtv]: http://qtv.quakeworld.nu/
 [nightly]: https://builds.quakeworld.nu/ezquake/snapshots/
 [releases]: https://github.com/ezQuake/ezquake-source/releases
 [issues]: https://github.com/ezQuake/ezquake-source/issues
 [homepage]: https://ezquake.com
 [discord]: http://discord.quake.world/
