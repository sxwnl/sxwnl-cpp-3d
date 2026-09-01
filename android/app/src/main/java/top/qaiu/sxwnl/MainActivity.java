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
 * the Java side is telling it where the system chrome is. The window is
 * fullscreen and the GL surface spans the display, which means the status bar
 * and the navigation bar (or gesture pill) sit <em>on top of</em> whatever the
 * native code draws at those edges. Without the insets below, the bottom
 * navigation row of the app lands underneath the system buttons and cannot be
 * tapped at all.
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

        final View decor = getWindow().getDecorView();
        decor.setOnApplyWindowInsetsListener((view, insets) -> {
            applyInsets(insets);
            return insets;
        });
        decor.requestApplyInsets();
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
