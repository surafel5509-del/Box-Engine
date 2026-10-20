<!-- Path: docs/profiling.md -->
# Android performance profiling

This note applies to the Android-only Box Engine app (minSdk 24, targetSdk 34, GLES 3.0). Use a physical arm64 device for representative graphics, scheduling, and thermal measurements. The engine provides a lightweight on-screen overlay, a bounded native log stream, CPU subsystem timers, and Android trace sections; these are complementary to Android Studio Profiler and Perfetto, not replacements for them.

## In-game diagnostics

The top-right **DEBUG ON** button toggles the overlay. Its values describe the most recently completed frame:

- **FPS / FT**: frame pacing estimate and frame duration, with the 60 Hz target floor applied when the render loop is idle.
- **DRAW**: `SpriteBatch` GLES draw calls from the previous frame. Add counters for any future direct GLES submission paths.
- **ENT**: live entities in the ECS world.
- **MEM**: process resident-set size sampled from `/proc/self/statm` about once per second. This is process RSS, not a GPU-memory or total-system-memory measurement.
- **GL**: current GLES program and framebuffer bindings plus depth-test and blending enable state.
- **CPU**: fixed update, variable update, scene graph, renderer, UI, and buffer-swap timings in milliseconds. Scene-graph time is nested inside variable-update time, and UI time is nested inside renderer time; those values overlap and must not be summed as exclusive costs.

The bottom-right **CONSOLE** opens a compact log view backed by a bounded 256-entry native stream. Commands are `help`, `clear`, `echo <text>`, `stats`, and `entities`. Text entry uses Android's soft keyboard; dismiss or submit with the IME action button.

## Android Studio Profiler

1. Install and launch the debug variant on a device. Its package ID is `com.boxengine.app.debug`; the non-debug variant uses `com.boxengine.app`.
2. In Android Studio, open **View → Tool Windows → Profiler**, select the device and process, and start a CPU recording before reproducing the issue.
3. Use **System Trace** for render-thread scheduling, CPU frequency, frame pacing, input, and graphics timing. The native `ATrace` slices appear as `BoxEngine.FixedUpdate`, `BoxEngine.VariableUpdate`, `BoxEngine.SceneGraph`, `BoxEngine.Renderer`, `BoxEngine.UI`, and `BoxEngine.EGLSwapBuffers`.
4. Use the memory timeline for Java/native process growth and compare it with the overlay RSS. The overlay is sampled process RSS and should not be interpreted as a complete native-allocation breakdown.
5. Repeat a capture after warm-up. Record device model, Android build, refresh rate, render scale, scene, and whether the run was thermally throttled. Do not compare a cold first launch with a warmed run.

## Perfetto

Perfetto is the preferred system-trace viewer on current Android releases. From a host with Android Platform Tools, the following category-based capture works on many devices; supported categories can vary by OEM and OS release:

```sh
adb shell perfetto -o /data/misc/perfetto-traces/box-engine.pftrace -t 20s \
  sched freq idle am wm gfx view input binder_driver
adb pull /data/misc/perfetto-traces/box-engine.pftrace ./box-engine.pftrace
```

Open the resulting trace in Android Studio's System Trace view or the Perfetto UI. Inspect the `BoxEngine.*` slices on the native render thread alongside `sched`, `freq`, `gfx`, and `input` tracks. A long `EGLSwapBuffers` slice may indicate presentation/vsync wait; correlate it with frame pacing rather than treating it automatically as CPU work. For automated captures, use a Perfetto text-protobuf configuration with `linux.ftrace` and `atrace_categories` data sources, then pass it with `perfetto -c config.pbtxt -o trace.pftrace` (or pipe the config on devices that support `-c -`).

## Systrace compatibility

Systrace is deprecated on recent Android versions but remains useful on older supported devices. If the Android SDK's legacy `systrace.py` is installed, capture scheduling and graphics categories, for example:

```sh
python3 "$ANDROID_HOME/platform-tools/systrace/systrace.py" \
  -t 20 gfx view sched freq idle am wm input -o box-engine-systrace.html
```

Open the HTML trace in a browser. Native `ATrace` sections are emitted through Android's trace API and are visible to both Perfetto and legacy Systrace. Prefer Perfetto when the device supports it.

## Adding trace markers

Wrap work on the render thread with `box::core::TraceSection` using a stable, short ASCII name. Keep markers scoped and non-nested where possible. For timings that need to appear in the in-game overlay, add a `ProfileSubsystem` scope as well; Android trace slices answer scheduling questions while the in-engine profiler provides frame-to-frame numeric summaries.
