# Android editor window and input contract

The Android frontend uses Qt 6.8.3. `EditorActivity` and its small Java/QPA
adapter form the Android input boundary; widgets and commands remain the shared Qt editor's widgets.

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

Local evidence is retained under `out/evidence/android-fixes`. Qualification
uses both tablet geometry (1280x800, density 160) and a portrait-native phone
(1080x2400, density 420) in landscape. Private models and emulator data are
never source or release assets.

Qt keeps its default composition policy: the editor's root uses a SurfaceView,
while raster dialogs and menus use TextureViews above the embedded Vulkan
SurfaceView. That child explicitly uses Android's media-overlay tier: above
the raster root and below TextureView dialogs, independent of Surface creation
order on background recovery. Forcing all windows to SurfaceViews is incorrect: same-Z native
Surfaces can follow creation order and cover a reused popup despite View reordering. Qt 6.8.3's accessibility bridge can hold a
deadlock guard during the first backing-store flush, preventing EGL Surface
creation. A window-owned asynchronous observer retries painting outside that
query until Qt reports a successful backing-store flush on an exposed window.
A valid native Surface alone does not prove that Qt presented it. The Android
adapter observes SurfaceHolder callbacks and wraps TextureView's existing Qt
listener without replacing its behavior. Native recreation re-arms painting
after Qt receives the new generation; GUI and Android threads never wait on
each other. Registration is enabled only after JNI methods are installed.
Show/expose and ordinary backing-store update requests re-arm the
window-owned retry, including reused popup windows; hiding stops it
and destroying the window removes it. A retry releases Qt's cached current GL
context first so a stale EGL Surface cannot produce a false successful flush.
A shown popup is explicitly raised: Qt 6.8.3 otherwise blocks activation of
a modal dialog's own popup, leaving a reused language list below Preferences.
Opening combo taps are tracked through release, independently of Qt's short
double-click timer: slow Surface creation must not consume that tap as a
selection. A deliberate drag to another item retains normal selection behavior.
Android file and preference windows fit the available
Activity area, including keyboard resize.

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
replacement. The native
acceptance gate additionally destroys/recreates the child surface and resets the
device while preserving the loaded scene.

Android WSI can return `VK_SUBOPTIMAL_KHR` on every presentation when identity
pre-transform differs from the display's rotation hint. It does not mean the
swapchain's extent changed. The Android backend compares actual extent and
transform before requesting a rebuild; an unchanged hint never causes a
per-frame rebuild. The UI gate checks continued submissions and the actual
swapchain-rebuild counter.

Android acquisition uses a nonblocking timeout. A covered BufferQueue can stop
supplying images while Android attaches a menu or keyboard. `SurfaceHealth::Busy`
means retry the next frame without submitting, rebuilding or losing uploaded
resources. The controller resumes directly when an image is available. A native
presentation regression injects `VK_NOT_READY`, then checks exact readback,
unchanged resource uploads/device identity and no swapchain rebuild.

## Compact editing and focus

The Android editor currently uses landscape orientation. Below 1100 logical
pixels wide or 560 high, it uses a compact layout.
Timeline and Controls toggle a drawer; the viewport retains the remaining
space. The drawer contains the original timeline, interpolation editor and
editor controls. It never copies the session or reloads model resources.
Sections/readouts scroll and retain standard Qt text focus and IME behavior.
Larger landscape tablets retain the original-style complete layout.

One finger orbits the camera in camera mode; two fingers pan and pinch to zoom.
Bone selection and axis tools continue through their existing command paths.
Cancel, focus loss and geometry changes retire the touch gesture.

Android's Qt accessibility bridge starts from the focused native window. The
embedded viewport returns its containing editor's root; its QWidget container
is registered as an accessibility leaf, preventing a reference cycle through
QAccessibleWindowContainer. Relayout does not raise that native child: doing so
on a keyboard resize activates the viewport above a modal text editor and closes
its input connection. Modal dialogs and popups pause viewport submissions
so a covered SurfaceView cannot block the GUI's touch and IME handling. A pending
native Surface resize skips drawing until the requested dimensions are ready.
The native child rectangle supplies physical dimensions at fractional DPI. The
Android adapter asynchronously resizes the SurfaceHolder buffers; changing the
View alone can retain an old Vulkan buffer extent indefinitely. BufferQueue
identity, holder extent and native lifecycle serial belong to a Surface
generation. A SurfaceView callback retires the old lease even when the Java
Surface and native pointer remain valid across a background transition. A
replacement is reacquired while retaining the CPU document and scene.

Surface acquisition also runs asynchronously. Android accessibility can wait for
the Qt GUI thread, so the GUI must never synchronously wait for Android's main
thread to fetch that Surface. The render loop polls completion and keeps both
event loops available during IME closing, window resizing and Surface changes.

## Repeatable qualification

`qt_mobile_editor` runs offscreen on desktop CI. It covers drawer/frame editing,
all 15 languages, camera/model modes and phone/tablet layout transitions while
checking that the same viewport and model resources survive.

The shipping APK's emulator gate additionally invokes both Android regression
scripts. `test_android_ui.py --phone --ime out/android-test-ime/test-ime.apk`
uses physical Android touches at accessibility bounds and observes ordinary
editor state through `--android-ui-report`. The observer never invokes editor
commands. Screenshot checks additionally require actual control chrome and text
for File / Preferences, the language selector, Apply and language choices; an
accessibility node alone cannot pass an invisible popup. Generated PMM/PMX files exercise the same file dialog/import path as
user documents. The public PMM includes a historical Windows `UserFile/Model`
reference, while the imported workspace has a different directory name. The
resolver keeps the saved subdirectory hierarchy under the PMM directory; it
never recursively picks a model by basename. Existing explicit search roots
and reselection retain their precedence. Optional `--project` and `--model` device paths allow private
local fixtures; they are never copied into source or release assets.

`tools/build_android_test_ime.py` builds an emulator-only helper with a real
InputMethodService and read-only AccessibilityService. It checks composition,
Japanese/Chinese commits, combining marks and Emoji through InputConnection.
The read-only service disables accessibility node caching and asynchronous
descendant prefetch, which can overlap Qt's serialized JNI queries or retain
a stale root across native confirmation dialogs and IME transitions.
It avoids UIAutomator's idle waits and checks for cycles;
it never performs accessibility actions. The runner restores keyboard,
accessibility, display density and geometry settings in `finally` blocks.
These settings may be changed only in an owned isolated emulator. The helper
APK is not a product dependency and is never packaged or published.
