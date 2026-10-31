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
    /* SDL's layout holding the game's surface (PresentationView adds its
     * page behind it). */
    static android.view.ViewGroup layout() { return mLayout; }

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL3", "main" };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        File shared = new File(Environment.getExternalStorageDirectory(), "NFL2K5");
        /* Make Internal storage/NFL2K5/original on first start, so there is a
         * folder to copy the game into (needs the permission below first). */
        try { new File(shared, "original").mkdirs(); } catch (Exception e) { }
        if (Build.VERSION.SDK_INT >= 30 && !Environment.isExternalStorageManager()) {
            try {
                Intent i = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                                      Uri.parse("package:" + getPackageName()));
                startActivity(i);
            } catch (Exception e) {
                /* Fire TV has no such settings page: the app folder still works */
            }
        } else if (Build.VERSION.SDK_INT < 30) {
            requestPermissions(new String[] { "android.permission.READ_EXTERNAL_STORAGE",
                                              "android.permission.WRITE_EXTERNAL_STORAGE" }, 1);
        }
        getExternalFilesDir(null);   /* creates Android/data/com.nfl2k5.pc/files */
        /* Landscape from the first frame (SDL also gets SDL_HINT_ORIENTATIONS) */
        setRequestedOrientation(android.content.pm.ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
        /* Android 12+ Game Mode, e.g. Samsung Game Booster's battery or
         * performance setting: read by the game at start (src/main.c). */
        if (Build.VERSION.SDK_INT >= 31) {
            try {
                android.app.GameManager gm = getSystemService(android.app.GameManager.class);
                int mode = gm != null ? gm.getGameMode() : 0;
                android.system.Os.setenv("NFL2K5_GAME_MODE",
                    mode == android.app.GameManager.GAME_MODE_BATTERY ? "battery" :
                    mode == android.app.GameManager.GAME_MODE_PERFORMANCE ? "performance" : "standard", true);
            } catch (Exception e) { }
        }
        super.onCreate(savedInstanceState);
    }

    /* ---- Phone Settings > Add Songs (src/platform/android/android_settings.c) ----
     * Android's own picker; the chosen songs are copied into
     * <game folder>/Music/Phone Songs, then the game rescans its library. */
    private static final int PICK_SONGS = 7101;
    private static native void nativeSongsAdded(int count);

    public static void pickSongs() {
        final NFL2K5Activity a = (NFL2K5Activity) mSingleton;
        if (a == null) return;
        a.runOnUiThread(new Runnable() { public void run() {
            Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            i.addCategory(Intent.CATEGORY_OPENABLE);
            i.setType("audio/*");
            i.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
            try { a.startActivityForResult(i, PICK_SONGS); } catch (Exception e) { }
        }});
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != PICK_SONGS || result != RESULT_OK || data == null) return;
        final java.util.ArrayList<Uri> uris = new java.util.ArrayList<>();
        if (data.getClipData() != null)
            for (int k = 0; k < data.getClipData().getItemCount(); k++) uris.add(data.getClipData().getItemAt(k).getUri());
        else if (data.getData() != null)
            uris.add(data.getData());
        new Thread(new Runnable() { public void run() {
            String root = android.system.Os.getenv("NFL2K5_ROOT");   /* set by the game; System.getenv is a startup copy */
            File dir = new File(root != null ? root : new File(Environment.getExternalStorageDirectory(), "NFL2K5").getPath(),
                                "Music/Phone Songs");
            dir.mkdirs();
            int n = 0;
            for (Uri u : uris) {
                String name = null;
                try (android.database.Cursor c = getContentResolver().query(u, new String[] {
                        android.provider.OpenableColumns.DISPLAY_NAME }, null, null, null)) {
                    if (c != null && c.moveToFirst()) name = c.getString(0);
                } catch (Exception e) { }
                if (name == null) name = "song" + System.currentTimeMillis() + ".mp3";
                name = name.replaceAll("[\\\\/:*?\"<>|]", "_");
                try (java.io.InputStream in = getContentResolver().openInputStream(u);
                     java.io.OutputStream out = new java.io.FileOutputStream(new File(dir, name))) {
                    byte[] b = new byte[1 << 16];
                    int r;
                    while ((r = in.read(b)) > 0) out.write(b, 0, r);
                    n++;
                } catch (Exception e) { }
            }
            nativeSongsAdded(n);
        }}).start();
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
