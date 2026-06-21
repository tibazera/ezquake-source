# Android Pocket Vulkan handoff

Branch: `feature/android-pocket-vulkan`

## Current state

- Android arm64 client builds with SDL 3.4, Vulkan and the Oboe backend.
- The SDL3 Java bootstrap is synced with SDL 3.4 and starts the exported native `main` entry point.
- SDL3 boolean return contracts are applied to initialization, mutexes, semaphores, events and Vulkan surface creation.
- Dynamic lightmap sub-image updates are queued and copied through a persistent mapped staging buffer in the next frame command buffer. This removes the immediate-command-buffer and queue-idle path from normal frame updates.
- The renderer now permits two frames in flight instead of waiting for the previous frame at every `VK_BeginFrame`. Each frame owns its synchronization objects and lightmap staging buffer, while each swapchain image tracks the fence that last used it.
- Voice capture is disabled on Android; playback uses Oboe.

## Build

Required local versions used for validation:

- JDK 17
- Android SDK at `C:\Android\Sdk`
- NDK `28.2.13676358`
- glslang from the vcpkg host tools directory on `PATH`

Build with `gradlew.bat :app:assembleDebug`. The APK is generated at `app/build/outputs/apk/debug/app-debug.apk`.

## Device-specific pitfalls

- Do not implement a code-side fix, automatic archive rewrite or special-case map loading for the texture issue described below. Quake data must not be packaged or committed in this repository. The intended solution is to distribute a separate, corrected PK3 and let the normal filesystem precedence rules load it.
- Root cause observed on the test device: `id1/gpl_maps.pk3` contained replacement simple-texture BSPs at `maps/dm3.bsp`, `maps/dm4.bsp` and `maps/dm6.bsp`. Because that archive had higher filesystem precedence, those BSPs shadowed the original maps from `pak1.pak`. Animated lava, portals and item textures still appeared, but most wall and geometry textures were missing, which initially looked like a Vulkan texture-upload failure.
- Diagnostic confirmation: removing only those three BSP entries from the active archive immediately restored the original textured maps. This was a device-side experiment, not a project fix. A copy named `gpl_maps.pk3.simpletextures-backup` was retained on the test device.
- Follow-up data task: build or obtain a clean replacement PK3 that does not override dm3, dm4 and dm6 with the simple-texture BSP variants. Validate its contents and load order against `pak1.pak` before distributing it. Renderer changes should only be reconsidered if the problem reproduces with known-good BSP and texture data.
- scrcpy captures Android audio by default and routes it to `AUDIO_DEVICE_OUT_REMOTE_SUBMIX`, making the phone speaker appear silent. Use `scrcpy --keyboard=uhid --mouse=uhid --no-audio` when sound must remain on the phone.
- Xiaomi blocks normal ADB input injection unless its additional USB debugging security option is enabled. UHID keyboard and mouse work without that permission.

## Audio diagnosis

The SDL2 expression `SDL_TryLockMutex(mutex) == 0` was invalid after migration because SDL3 returns `bool`. It acquired the mixer mutex, treated success as failure and left it locked, producing the SDL mutex assertion and silence. `S_TryLockMixer()` now uses the SDL3 boolean result directly. Device logs confirmed that the Oboe callback produces non-zero PCM and AAudio plays it; remaining apparent silence during desktop mirroring was scrcpy audio routing, not the mixer.

## Next performance work

- Measure dm3/dm4/dm6 frame timings after the lightmap batching and two-frames-in-flight changes. Startup/stability has been validated on the Xiaomi test device, but the in-map FPS gain is not yet measured.
- Profile world draw-call count, pipeline switches and CPU/GPU frame split before importing broader renderer changes from vkQuake or FTEQW.
- The test device exposes a 2712x1220 landscape surface and was running its physical display at 120 Hz. If synchronization changes are insufficient, measure fill-rate pressure at native resolution before implementing a lower-resolution render target or Android surface scaling.
