# Android editor window and input contract

The Android frontend uses Qt 6.8.3. `EditorActivity` and its small Java/QPA adapter form the Android input
boundary; widgets and commands remain the shared Qt editor's widgets.

## Coordinates and system bars

* Android dispatches view-local native pixels. Qt 6.8.3's Java handler adds
  the view position and its JNI handler then mixes those pixels with a logical
  `QWindow::mapToGlobal` call. The error depends on window origin and DPI;
  Preferences can activate Exit, and dialogs miss otherwise valid touches.
* A finger-only Java adapter sends complete pointer frames to the GUI thread.
  The live `QPlatformWindow` maps local native pixels to Qt's virtual native
  desktop; QPA then converts to logical coordinates once. Physical screen raw
  coordinates cannot be substituted: system bars can give that screen a
  different origin. Queued events for retired windows are discarded.
* Focus is established on down only, so releasing a finger cannot reactivate
  a window beneath a newly opened popup. Multi-touch state, pressure, contact
  area and long press are preserved. Qt owns widget delivery, mouse synthesis
  and InputConnection. Mouse and stylus retain Qt's original path.
* This small Qt-private integration is pinned to Qt 6.8.3; review both the Java
  and QPA adapters when upgrading Qt. It applies no application-wide pointer
  offset and requires no reflection into Qt's Java fields.
* Content uses the available geometry once. The Activity fits content inside
  system bars and cutouts, with `adjustResize` for the keyboard. Android 15's
  theme explicitly retains this contract for the pinned Qt version. No fixed
  status-bar height or duplicate QWidget padding is applied.

`tests/test_android_ui.py` touches the centers of Android accessibility bounds,
opens File / Preferences, checks language and dialog controls, closes the dialog
and activates Go. It neither sends Qt test mouse events nor compensates touch
coordinates. Run it only against an owned emulator, selected with ANDROID_SERIAL.

Local regression: Android 14 / API 34, Pixel 7 Pro emulator, 1280x800 at density
160; the Preferences and ordinary button touch gate passes. Test evidence belongs
under `out/evidence/android-fixes`; private models and emulator data are not source
or release assets.

## Vulkan presentation

The shared final color image is in displayed window coordinates. The swapchain
therefore requests `VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR`; WSI applies the physical
display transform. Declaring `currentTransform` without pre-rotating the image
incorrectly rotates the scene on portrait-native displays. Both capability query
and swapchain creation reject surfaces lacking identity-transform support instead
of presenting an incorrectly oriented frame. Offscreen rendering is unaffected.

`tests/test_android_presentation.py` checks the red/green world axes in actual
screenshots, forces both landscape orientations and confirms the display really
changed rotation. It repeats the check after background/resume and a cold window
replacement. Android 14 / TITAN Xp passed these checks locally. The native
acceptance gate additionally destroys/recreates the child surface and resets the
device while preserving the loaded scene.
