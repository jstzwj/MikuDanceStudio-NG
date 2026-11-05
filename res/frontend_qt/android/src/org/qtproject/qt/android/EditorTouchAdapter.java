package org.qtproject.qt.android;

import android.view.GestureDetector;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.SurfaceView;
import android.view.TextureView;
import android.graphics.SurfaceTexture;
import android.view.SurfaceHolder;
import java.util.WeakHashMap;

/** Pinned Qt 6.8.3 input boundary: view-local native pixels are mapped once. */
public final class EditorTouchAdapter {
    private static final WeakHashMap<View, GestureDetector> gestures = new WeakHashMap<>();
    private static native void sendTouch(int window, long time, int action, int changed,
                                         int[] identifiers, float[] coordinates);
    private static final WeakHashMap<SurfaceHolder, Boolean> surfaces = new WeakHashMap<>();
    private static native void nativeSurfaceChanged(int window);
    private static volatile boolean nativeReady;
    public static void enableSurfaceObserver() { nativeReady = true; }
    private static final WeakHashMap<TextureView, Boolean> textures = new WeakHashMap<>();
    public static void observeSurfaces(ViewGroup window) {
        if (!nativeReady)
            return;
        final int id = window.getId();
        for (int index = 0; index < window.getChildCount(); ++index) {
            View child = window.getChildAt(index);
            if (child instanceof TextureView) {
                TextureView texture = (TextureView) child;
                if (textures.put(texture, Boolean.TRUE) != null)
                    continue;
                final TextureView.SurfaceTextureListener qt = texture.getSurfaceTextureListener();
                texture.setSurfaceTextureListener(new TextureView.SurfaceTextureListener() {
                    @Override public void onSurfaceTextureAvailable(SurfaceTexture surface, int width, int height) {
                        if (qt != null) qt.onSurfaceTextureAvailable(surface, width, height);
                        nativeSurfaceChanged(id);
                    }
                    @Override public void onSurfaceTextureSizeChanged(SurfaceTexture surface, int width, int height) {
                        if (qt != null) qt.onSurfaceTextureSizeChanged(surface, width, height);
                        nativeSurfaceChanged(id);
                    }
                    @Override public boolean onSurfaceTextureDestroyed(SurfaceTexture surface) {
                        boolean release = qt == null || qt.onSurfaceTextureDestroyed(surface);
                        nativeSurfaceChanged(id);
                        return release;
                    }
                    @Override public void onSurfaceTextureUpdated(SurfaceTexture surface) {
                        if (qt != null) qt.onSurfaceTextureUpdated(surface);
                    }
                });
                nativeSurfaceChanged(id);
                continue;
            }
            if (!(child instanceof SurfaceView))
                continue;
            SurfaceHolder holder = ((SurfaceView) child).getHolder();
            if (surfaces.put(holder, Boolean.TRUE) != null)
                continue;
            holder.addCallback(new SurfaceHolder.Callback() {
                @Override public void surfaceCreated(SurfaceHolder surface) { nativeSurfaceChanged(id); }
                @Override public void surfaceChanged(SurfaceHolder surface, int format, int width, int height) {
                    nativeSurfaceChanged(id);
                }
                @Override public void surfaceDestroyed(SurfaceHolder surface) { nativeSurfaceChanged(id); }
            });
            // Registration may follow the initial callback. The GUI retries
            // without waiting for Android; later Surface generations re-arm it.
            nativeSurfaceChanged(id);
        }
    }
    private static native void longPress(int window, float x, float y);
    public static boolean dispatch(View view, MotionEvent event) {
        int action = event.getActionMasked();
        if (action == MotionEvent.ACTION_DOWN) {
            // Refocusing on UP can dismiss a popup which DOWN just opened.
            QtWindow.windowFocusChanged(true, view.getId());
            ((QtWindow) view).updateFocusedEditText();
        }
        int count = event.getPointerCount();
        int[] identifiers = new int[count];
        float[] coordinates = new float[count * 6];
        for (int i = 0; i < count; ++i) {
            identifiers[i] = event.getPointerId(i);
            coordinates[i * 6] = event.getX(i);
            coordinates[i * 6 + 1] = event.getY(i);
            coordinates[i * 6 + 2] = event.getPressure(i);
            coordinates[i * 6 + 3] = event.getTouchMajor(i);
            coordinates[i * 6 + 4] = event.getTouchMinor(i);
            coordinates[i * 6 + 5] = event.getOrientation(i);
        }
        sendTouch(view.getId(), event.getEventTime(), action, event.getActionIndex(),
                  identifiers, coordinates);
        GestureDetector detector = gestures.get(view);
        if (detector == null) {
            // Capture only the integer ID: the WeakHashMap value must not keep
            // its key alive across popup and Surface/window reconstruction.
            final int id = view.getId();
            detector = new GestureDetector(view.getContext(), new GestureDetector.SimpleOnGestureListener() {
                @Override public void onLongPress(MotionEvent held) {
                    longPress(id, held.getX(), held.getY());
                }
            });
            gestures.put(view, detector);
        }
        detector.onTouchEvent(event);
        return true;
    }
    private EditorTouchAdapter() { }
}
