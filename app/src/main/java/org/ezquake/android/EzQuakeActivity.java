package org.ezquake.android;

import org.libsdl.app.SDLActivity;

public class EzQuakeActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "ezquake" };
    }
}
