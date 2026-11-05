package io.github.jstzwj.mds.testime;

import android.accessibilityservice.AccessibilityService;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.graphics.Rect;
import android.util.Xml;
import android.view.accessibility.AccessibilityEvent;
import android.view.accessibility.AccessibilityNodeInfo;
import android.view.accessibility.AccessibilityWindowInfo;
import java.io.File;
import java.io.FileOutputStream;
import java.util.HashSet;
import java.util.Set;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicBoolean;
import org.xmlpull.v1.XmlSerializer;

/** Emulator-only read-only observer. No accessibility actions are invoked. */
public class UiObserver extends AccessibilityService {
    private final ExecutorService snapshots = Executors.newSingleThreadExecutor();
    private final AtomicBoolean busy = new AtomicBoolean();
    private volatile boolean connected;
    private final BroadcastReceiver dump = new BroadcastReceiver() {
        @Override public void onReceive(Context context, Intent intent) {
            if (!connected || !busy.compareAndSet(false, true)) return;
            // Accessibility IPC may wait for a window being replaced. Never
            // block the IME/service main looper or a broadcast's 10s deadline.
            snapshots.execute(() -> snapshot(intent));
        }
    };
    private void snapshot(Intent intent) {
        try {
            File temporary = new File(getFilesDir(), "hierarchy.tmp");
            try (FileOutputStream stream = new FileOutputStream(temporary)) {
                XmlSerializer xml = Xml.newSerializer();
                xml.setOutput(stream, "UTF-8");
                xml.startDocument("UTF-8", true);
                xml.startTag(null, "hierarchy");
                xml.attribute(null, "request", intent.getStringExtra("request"));
                clearCache();
                for (AccessibilityWindowInfo window : getWindows()) {
                    if (intent.getBooleanExtra("imeOnly", false) &&
                        window.getType() != AccessibilityWindowInfo.TYPE_INPUT_METHOD) continue;
                    if (window.getType() != AccessibilityWindowInfo.TYPE_APPLICATION &&
                        window.getType() != AccessibilityWindowInfo.TYPE_INPUT_METHOD) continue;
                    if (window.getType() == AccessibilityWindowInfo.TYPE_APPLICATION &&
                        !window.isActive() && !window.isFocused()) continue;
                    // Enumerate explicitly, without Android 13+'s asynchronous
                    // descendant prefetch. Qt serializes all these JNI queries
                    // through one deadlock guard; overlapping prefetch can
                    // return empty nodes or outlive a retired dialog window.
                    AccessibilityNodeInfo root = window.getRoot(0);
                    xml.startTag(null, "window");
                    xml.attribute(null, "title", value(window.getTitle()));
                    xml.attribute(null, "active", Boolean.toString(window.isActive()));
                    xml.attribute(null, "focused", Boolean.toString(window.isFocused()));
                    xml.attribute(null, "rootPresent", Boolean.toString(root != null));
                    xml.attribute(null, "rootVisible", Boolean.toString(root != null && root.isVisibleToUser()));
                    if (root != null) node(xml, root, 0, new HashSet<>());
                    xml.endTag(null, "window");
                }
                xml.endTag(null, "hierarchy");
                xml.endDocument();
            }
            if (!temporary.renameTo(new File(getFilesDir(), "hierarchy.xml")))
                throw new java.io.IOException("Cannot commit hierarchy");
        } catch (Exception error) {
            android.util.Log.e("MdsUiObserver", "dump failed", error);
        } finally {
            busy.set(false);
        }
    }
    @Override public void onCreate() {
        super.onCreate();
        registerReceiver(dump, new IntentFilter(getPackageName() + ".DUMP"), Context.RECEIVER_EXPORTED);
    }
    @Override public void onServiceConnected() {
        // Use current window/node state across native AlertDialog, IME and Qt
        // popup transitions, rather than a cached root from the previous window.
        setCacheEnabled(false);
        connected = true;
    }
    @Override public void onDestroy() {
        connected = false;
        unregisterReceiver(dump);
        snapshots.shutdownNow();
        super.onDestroy();
    }
    @Override public void onAccessibilityEvent(AccessibilityEvent event) { }
    @Override public void onInterrupt() { }
    private static String value(CharSequence text) { return text == null ? "" : text.toString(); }
    private void node(XmlSerializer xml, AccessibilityNodeInfo info, int depth, Set<AccessibilityNodeInfo> visited) throws Exception {
        if (!info.isVisibleToUser()) return;
        if (depth > 64 || !visited.add(info)) throw new IllegalStateException("Cyclic accessibility tree");
        Rect bounds = new Rect();
        info.getBoundsInScreen(bounds);
        xml.startTag(null, "node");
        xml.attribute(null, "text", value(info.getText()));
        xml.attribute(null, "content-desc", value(info.getContentDescription()));
        xml.attribute(null, "class", value(info.getClassName()));
        xml.attribute(null, "resource-id", value(info.getViewIdResourceName()));
        xml.attribute(null, "enabled", Boolean.toString(info.isEnabled()));
        xml.attribute(null, "bounds", "[" + bounds.left + "," + bounds.top + "][" + bounds.right + "," + bounds.bottom + "]");
        for (int i = 0; i < info.getChildCount(); ++i) {
            AccessibilityNodeInfo child = info.getChild(i, 0);
            if (child != null) node(xml, child, depth + 1, visited);
        }
        xml.endTag(null, "node");
    }
}
