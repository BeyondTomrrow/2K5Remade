package com.nfl2k5.pc;

import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.Settings;
import android.view.View;

import org.libsdl.app.SDLActivity;

import java.io.File;

/* SDL's activity running libmain.so (the game). The game folder is the app's
 * own storage folder or /sdcard/NFL2K5 (src/main.c android_pick_root); the
 * second needs "All files access" on Android 11 and later, asked for here
 * only when that folder exists. */
public class NFL2K5Activity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL3", "main" };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        File shared = new File(Environment.getExternalStorageDirectory(), "NFL2K5");
        if (Build.VERSION.SDK_INT >= 30 && shared.isDirectory() && !Environment.isExternalStorageManager()) {
            try {
                Intent i = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                                      Uri.parse("package:" + getPackageName()));
                startActivity(i);
            } catch (Exception e) {
                /* Fire TV has no such settings page: the app folder still works */
            }
        } else if (Build.VERSION.SDK_INT < 30 && shared.isDirectory()) {
            requestPermissions(new String[] { "android.permission.READ_EXTERNAL_STORAGE",
                                              "android.permission.WRITE_EXTERNAL_STORAGE" }, 1);
        }
        getExternalFilesDir(null);   /* creates Android/data/com.nfl2k5.pc/files */
        super.onCreate(savedInstanceState);
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            getWindow().getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_FULLSCREEN |
                View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_STABLE |
                View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN);
        }
    }
}
