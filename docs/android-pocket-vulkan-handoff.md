# Android Pocket Vulkan handoff

Branch: `feature/android-pocket-vulkan`

## Current state

- Android arm64 client builds with SDL 3.4, Vulkan and the Oboe backend.
- The SDL3 Java bootstrap is synced with SDL 3.4 and starts the exported native `main` entry point.
- SDL3 boolean return contracts are applied to initialization, mutexes, semaphores, events and Vulkan surface creation.
- Dynamic lightmap sub-image updates are queued and copied through a persistent mapped staging buffer in the next frame command buffer. This removes the immediate-command-buffer and queue-idle path from normal frame updates.
- Voice capture is disabled on Android; playback uses Oboe.

## Build

Required local versions used for validation:

- JDK 17
- Android SDK at `C:\Android\Sdk`
- NDK `28.2.13676358`
- glslang from the vcpkg host tools directory on `PATH`

Build with `gradlew.bat :app:assembleDebug`. The APK is generated at `app/build/outputs/apk/debug/app-debug.apk`.

## Device-specific pitfalls

- Do not package or commit Quake data. On the test device, `id1/gpl_maps.pk3` contained replacement simple-texture BSPs for dm3, dm4 and dm6 and shadowed the original maps from pak1. Removing those three BSP entries restored wall textures. A backup was kept on the device as `gpl_maps.pk3.simpletextures-backup`.
- scrcpy captures Android audio by default and routes it to `AUDIO_DEVICE_OUT_REMOTE_SUBMIX`, making the phone speaker appear silent. Use `scrcpy --keyboard=uhid --mouse=uhid --no-audio` when sound must remain on the phone.
- Xiaomi blocks normal ADB input injection unless its additional USB debugging security option is enabled. UHID keyboard and mouse work without that permission.

## Audio diagnosis

The SDL2 expression `SDL_TryLockMutex(mutex) == 0` was invalid after migration because SDL3 returns `bool`. It acquired the mixer mutex, treated success as failure and left it locked, producing the SDL mutex assertion and silence. `S_TryLockMixer()` now uses the SDL3 boolean result directly. Device logs confirmed that the Oboe callback produces non-zero PCM and AAudio plays it; remaining apparent silence during desktop mirroring was scrcpy audio routing, not the mixer.

## Next performance work

- Measure dm3/dm4/dm6 frame timings after the lightmap batching change.
- Profile world draw-call count, pipeline switches and CPU/GPU frame split before importing broader renderer changes from vkQuake or FTEQW.
- If multiple frames in flight are introduced later, make the persistent upload buffer frame-indexed. The current renderer uses one global in-flight fence, so one buffer is safe today.
