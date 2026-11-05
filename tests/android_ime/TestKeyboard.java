package io.github.jstzwj.mds.testime;

import android.inputmethodservice.InputMethodService;
import android.util.Log;
import android.view.View;
import android.view.inputmethod.InputConnection;
import android.widget.Button;
import android.widget.LinearLayout;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;

/** Installed only in the owned emulator; never a product APK dependency.
 * Uses the real Android InputConnection, including preedit/commit, rather than
 * synthesizing a QInputMethodEvent in Qt or sending Unicode as adb key events.
 */
public class TestKeyboard extends InputMethodService {
    private String input = "";
    private final BroadcastReceiver prepare = new BroadcastReceiver() {
        @Override public void onReceive(Context context, Intent intent) {
            input = intent.getStringExtra("text");
        }
    };
    @Override public void onCreate() {
        super.onCreate();
        registerReceiver(prepare, new IntentFilter(getPackageName() + ".PREPARE"), Context.RECEIVER_EXPORTED);
    }
    @Override public void onDestroy() { unregisterReceiver(prepare); super.onDestroy(); }
    @Override public void onWindowShown() { super.onWindowShown(); Log.i("MdsTestIme", "shown"); }
    @Override public void onWindowHidden() { Log.i("MdsTestIme", "hidden"); super.onWindowHidden(); }
    @Override public void onStartInput(android.view.inputmethod.EditorInfo info, boolean restarting) {
        super.onStartInput(info, restarting);
        Log.i("MdsTestIme", "start input restarting=" + restarting + " type=" + info.inputType);
    }
    @Override public void onFinishInput() { Log.i("MdsTestIme", "finish input"); super.onFinishInput(); }
    @Override
    public boolean onEvaluateFullscreenMode() { return false; }

    @Override
    public View onCreateInputView() {
        LinearLayout keys = new LinearLayout(this);
        keys.setPadding(4, 4, 4, 4);
        add(keys, "Compose", () -> connection().setComposingText("nihongo", 1));
        add(keys, "Japanese", () -> connection().commitText("\u65e5\u672c\u8a9e", 1));
        add(keys, "Chinese", () -> connection().commitText("\u4e2d\u6587", 1));
        add(keys, "Unicode", () -> connection().commitText("e\u0301\ud83d\ude00", 1));
        add(keys, "Clear", () -> {
            InputConnection c = connection();
            c.finishComposingText();
            c.performContextMenuAction(android.R.id.selectAll);
            c.commitText("", 1);
        });
        add(keys, "Path", () -> {
            InputConnection c = connection();
            c.finishComposingText();
            c.performContextMenuAction(android.R.id.selectAll);
            c.commitText(input, 1);
        });
        return keys;
    }
    private InputConnection connection() { return getCurrentInputConnection(); }
    private void add(LinearLayout keys, String label, Runnable action) {
        Button button = new Button(this);
        button.setText(label);
        button.setContentDescription(label);
        button.setOnClickListener(v -> {
            Log.i("MdsTestIme", "key=" + label + " connected=" + (connection() != null));
            if (connection() != null) action.run();
        });
        // A realistic keyboard height also exercises adjustResize on phones.
        int height = Math.round(112 * getResources().getDisplayMetrics().density);
        keys.addView(button, new LinearLayout.LayoutParams(0, height, 1));
    }
}
