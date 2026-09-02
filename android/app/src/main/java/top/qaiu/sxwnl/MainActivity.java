package top.qaiu.sxwnl;

import android.graphics.Insets;
import android.os.Build;
import android.os.Bundle;
import android.text.Editable;
import android.text.InputType;
import android.text.TextWatcher;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.InputMethodManager;
import android.widget.EditText;

import java.util.concurrent.LinkedBlockingQueue;

/**
 * NativeActivity host.
 *
 * <p>The UI is drawn entirely by the native ImGui renderer, so this class exists
 * for the two things the native side cannot reach on its own.
 *
 * <p><b>Soft keyboard.</b> ImGui's Android backend never raises the IME - its
 * source carries an explicit FIXME saying the application must do it - and the
 * NDK has no equivalent of {@code KeyEvent.getUnicodeChar()}, so nothing typed
 * into a text field ever arrived. The fix is an invisible one-pixel EditText
 * that owns the IME focus: the keyboard commits into it, a TextWatcher drains
 * the committed characters into a queue, and the render loop polls that queue
 * each frame. Routing through a real EditText (rather than the decor view, as
 * the ImGui example does) is what makes Chinese input work: the IME needs a view
 * with an InputConnection to hold the composing text, and on newer Android the
 * decor-view approach is rejected outright with "servedView != view".
 *
 * <p><b>Window insets.</b> The app uses an ordinary non-fullscreen theme, so the
 * system already lays the GL surface out between the status and navigation bars
 * and these normally come back as zero. They are still reported because the
 * listener sits on the content view: whatever reaches it is genuinely unclaimed
 * space, which is what the native side should pad for. That covers display
 * cutouts and the OEM variations that hand over the full display anyway.
 */
public class MainActivity extends android.app.NativeActivity {

    static {
        // NativeActivity loads this library itself from the manifest metadata,
        // but only after super.onCreate(). Loading it here first makes the JNI
        // entry point below resolvable from the very first inset callback.
        System.loadLibrary("sxwnl_android");
    }

    private static native void nativeSetInsets(int left, int top, int right, int bottom);

    /** Characters committed by the IME, drained by the native render loop. */
    private final LinkedBlockingQueue<Integer> pendingChars = new LinkedBlockingQueue<>();

    private EditText input;
    /** Guards the clear() below from re-entering our own TextWatcher. */
    private boolean clearing = false;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setUpHiddenInput();
        setUpInsets();
    }

    private void setUpHiddenInput() {
        input = new EditText(this);
        input.setFocusableInTouchMode(true);
        input.setCursorVisible(false);
        input.setAlpha(0f);
        // No suggestion bar: it would cover the app and there is nothing to
        // suggest against a numeric field.
        input.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        addContentView(input, new ViewGroup.LayoutParams(1, 1));

        input.addTextChangedListener(new TextWatcher() {
            @Override
            public void beforeTextChanged(CharSequence s, int start, int count, int after) {}

            @Override
            public void onTextChanged(CharSequence s, int start, int before, int count) {}

            @Override
            public void afterTextChanged(Editable e) {
                if (clearing) return;
                // Still composing (pinyin in progress). Wait for the commit,
                // otherwise every intermediate letter would be sent through.
                if (BaseInputConnection.getComposingSpanStart(e) != -1) return;
                for (int i = 0; i < e.length(); i++) {
                    pendingChars.add((int) e.charAt(i));
                }
                clearing = true;
                e.clear();
                clearing = false;
            }
        });

        // Backspace produces no character, so forward it as a control code.
        input.setOnKeyListener((v, keyCode, event) -> {
            if (event.getAction() == KeyEvent.ACTION_DOWN && keyCode == KeyEvent.KEYCODE_DEL) {
                pendingChars.add(8);
            }
            return false;
        });
    }

    private void setUpInsets() {
        final View content = findViewById(android.R.id.content);
        if (content == null) return;
        content.setOnApplyWindowInsetsListener((view, insets) -> {
            applyInsets(insets);
            return insets;
        });
        content.requestApplyInsets();
    }

    private void applyInsets(WindowInsets insets) {
        int left, top, right, bottom;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            Insets bars = insets.getInsets(
                    WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
            left = bars.left;
            top = bars.top;
            right = bars.right;
            bottom = bars.bottom;
        } else {
            left = insets.getSystemWindowInsetLeft();
            top = insets.getSystemWindowInsetTop();
            right = insets.getSystemWindowInsetRight();
            bottom = insets.getSystemWindowInsetBottom();
        }
        nativeSetInsets(left, top, right, bottom);
    }

    private InputMethodManager inputMethodManager() {
        return (InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
    }

    // ---- Called from the native render loop (see android_main.cpp) ----------

    /** Raises the soft keyboard. Must be edge-triggered: see the C++ caller. */
    @SuppressWarnings("unused")
    public void showSoftInput() {
        runOnUiThread(() -> {
            if (input == null) return;
            input.requestFocus();
            inputMethodManager().showSoftInput(input, 0);
        });
    }

    @SuppressWarnings("unused")
    public void hideSoftInput() {
        runOnUiThread(() -> {
            if (input == null) return;
            inputMethodManager().hideSoftInputFromWindow(input.getWindowToken(), 0);
            input.clearFocus();
        });
    }

    /** Next queued character, or 0 when the queue is empty. */
    @SuppressWarnings("unused")
    public int pollUnicodeChar() {
        Integer c = pendingChars.poll();
        return c == null ? 0 : c;
    }
}
