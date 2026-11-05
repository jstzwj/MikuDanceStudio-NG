package io.github.jstzwj.mikudancestudiong;

import android.os.Bundle;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import java.util.WeakHashMap;
import androidx.core.view.WindowCompat;
import org.qtproject.qt.android.bindings.QtActivity;
import org.qtproject.qt.android.EditorTouchAdapter;

/** The platform boundary for the pinned Qt 6.8.3 Android window adapter. */
public class EditorActivity extends QtActivity {
    private final WeakHashMap<View, Boolean> adaptedWindows = new WeakHashMap<>();
    @Override
    public void onCreate(Bundle state) {
        super.onCreate(state);
        // Qt reports the available screen, including system-bar/cutout insets.
        // Keep content in that area; do not add those insets again in QWidget.
        WindowCompat.setDecorFitsSystemWindows(getWindow(), true);
        View decor = getWindow().getDecorView();
        decor.getViewTreeObserver().addOnGlobalLayoutListener(() -> installTouchAdapter(decor));
    }

    @Override
    public boolean dispatchTouchEvent(MotionEvent event) {
        // A combo/menu can create a new QtWindow while a finger is down.
        // Install before every pointer frame, including the release delivered
        // to that popup; otherwise it falls back to Qt's mixed-coordinate JNI.
        installTouchAdapter(getWindow().getDecorView());
        return super.dispatchTouchEvent(event);
    }

    private void installTouchAdapter(View view) {
        if (view.getClass().getName().equals("org.qtproject.qt.android.QtWindow") &&
            adaptedWindows.put(view, Boolean.TRUE) == null) {
            view.setOnTouchListener((window, event) -> {
                // Qt 6.8.3's JNI touchAdd mixes native pixels with logical
                // mapToGlobal coordinates. The adapter submits global screen
                // pixels through the native platform window; QPA then performs
                // the DPI conversion once, independent of physical bar insets.
                if (event.getToolType(0) != MotionEvent.TOOL_TYPE_FINGER)
                    return false;
                return EditorTouchAdapter.dispatch(window, event);
            });
        }
        if (view.getClass().getName().equals("org.qtproject.qt.android.QtWindow"))
            EditorTouchAdapter.observeSurfaces((ViewGroup) view);
        if (view instanceof ViewGroup) {
            ViewGroup group = (ViewGroup) view;
            for (int index = 0; index < group.getChildCount(); ++index)
                installTouchAdapter(group.getChildAt(index));
        }
    }
}
