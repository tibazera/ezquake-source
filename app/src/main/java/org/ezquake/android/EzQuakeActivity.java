package org.ezquake.android;

import android.os.Bundle;
import android.view.ViewGroup;
import android.widget.ImageView;
import android.widget.RelativeLayout;

import org.libsdl.app.SDLActivity;

public class EzQuakeActivity extends SDLActivity {
    private ImageView startupSplashView;

    @Override
    protected String[] getLibraries() {
        return new String[] { "ezquake" };
    }

    @Override
    protected String getMainFunction() {
        return "main";
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        // SDLActivity's SurfaceView paints black as soon as it's attached,
        // well before the Vulkan device/swapchain are ready to present a
        // first frame -- the window's splash background (styles.xml) is
        // already obscured by then, so without this the user just sees a
        // black screen during native/renderer init. Stack the same splash
        // artwork as a normal View on top of the SurfaceView instead, and
        // remove it once hideStartupSplash() below fires.
        startupSplashView = new ImageView(this);
        startupSplashView.setImageResource(R.drawable.ezquake_splash_background);
        startupSplashView.setScaleType(ImageView.ScaleType.CENTER_CROP);
        ((ViewGroup) getContentView()).addView(startupSplashView, new RelativeLayout.LayoutParams(
                RelativeLayout.LayoutParams.MATCH_PARENT, RelativeLayout.LayoutParams.MATCH_PARENT));
    }

    // Called via JNI (GetMethodID "hideStartupSplash" "()V") from vk_main.c
    // once the Vulkan renderer has drawn its first frame, so the static
    // splash drawable set as the window background -- and the overlay view
    // added above -- don't linger behind/over it.
    public void hideStartupSplash() {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                getWindow().setBackgroundDrawable(null);
                if (startupSplashView != null) {
                    ViewGroup parent = (ViewGroup) startupSplashView.getParent();
                    if (parent != null) {
                        parent.removeView(startupSplashView);
                    }
                    startupSplashView = null;
                }
            }
        });
    }
}
