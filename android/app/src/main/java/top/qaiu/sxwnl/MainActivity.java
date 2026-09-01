package top.qaiu.sxwnl;

import android.graphics.Insets;
import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.view.WindowInsets;

/**
 * NativeActivity host.
 *
 * <p>The whole UI is drawn by the native ImGui renderer, so the only job left on
 * the Java side is telling it about any screen area it must keep clear.
 *
 * <p>The app uses an ordinary non-fullscreen theme, so the system already lays
 * the GL surface out between the status bar and the navigation bar and these
 * insets normally come back as zero. They are still reported because the
 * listener is attached to the content view: whatever reaches it is genuinely
 * unclaimed space, which is exactly what the native side should pad for. That
 * covers display cutouts and the OEM variations where the surface is handed the
 * full display anyway.
 */
public class MainActivity extends android.app.NativeActivity {

    static {
        // NativeActivity loads this library itself from the manifest metadata,
        // but only after super.onCreate(). Loading it here first makes the JNI
        // entry point below resolvable from the very first inset callback.
        System.loadLibrary("sxwnl_android");
    }

    private static native void nativeSetInsets(int left, int top, int right, int bottom);

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

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
}
