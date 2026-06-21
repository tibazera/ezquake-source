package org.ezquake.android;

import org.libsdl.app.SDLActivity;

public class EzQuakeActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "ezquake" };
    }

    // Called via JNI (GetMethodID "hideStartupSplash" "()V") from vk_main.c
    // once the Vulkan renderer has drawn its first frame, so the static
    // splash drawable set as the window background doesn't linger behind it.
    public void hideStartupSplash() {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                getWindow().setBackgroundDrawable(null);
            }
        });
    }
}
