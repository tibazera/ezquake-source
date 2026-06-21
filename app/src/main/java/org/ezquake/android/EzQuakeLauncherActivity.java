package org.ezquake.android;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;

public class EzQuakeLauncherActivity extends Activity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        startActivity(new Intent(this, EzQuakeActivity.class));
        finish();
    }
}
