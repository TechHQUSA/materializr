package com.materializr.app;

import android.content.Intent;
import android.content.res.Configuration;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.Settings;
import android.view.View;
import org.libsdl.app.SDLActivity;

// Materializr's Android entry activity. SDLActivity does the heavy lifting:
// it creates the GL surface, loads the native libraries below, and calls
// SDL_main (defined in android_main.cpp) on its own thread.
public class MaterializrActivity extends SDLActivity {

    // Desktop mode = a usable hardware keyboard is attached, or we're in a
    // freeform/multi-window container (Lenovo "PC mode", Samsung DeX, a
    // Chromebook, etc.).
    private boolean isDesktopMode() {
        Configuration c = getResources().getConfiguration();
        boolean hwKeyboard = c.keyboard == Configuration.KEYBOARD_QWERTY
                && c.hardKeyboardHidden == Configuration.HARDKEYBOARDHIDDEN_NO;
        boolean multiWindow = Build.VERSION.SDK_INT >= Build.VERSION_CODES.N
                && isInMultiWindowMode();
        return hwKeyboard || multiWindow;
    }

    // Bare tablet -> immersive (bars hidden, max canvas for CAD). Desktop mode ->
    // a normal window: bars/taskbar visible so the app behaves like any other app
    // in the dock (the taskbar stays put, the canvas doesn't overlap the gesture
    // strip). NOTE: we only ever toggle the system-UI VISIBILITY flags here, never
    // the window-level FLAG_FULLSCREEN — that flag is what made desktop mode
    // maximize us and hide the taskbar, and Window.cpp deliberately omits
    // SDL_WINDOW_FULLSCREEN so SDL doesn't set it either.
    private void applyWindowMode() {
        View dv = getWindow().getDecorView();
        if (!isDesktopMode()) {
            dv.setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_FULLSCREEN
                | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
        } else {
            dv.setSystemUiVisibility(View.SYSTEM_UI_FLAG_VISIBLE);
        }
    }

    // Request All-Files access so the in-app browser can reach /storage/emulated/0
    // (Android 11+). If declined, native code falls back to the app's own dir.
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R
                && !Environment.isExternalStorageManager()) {
            try {
                Intent i = new Intent(
                    Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                    Uri.parse("package:" + getPackageName()));
                startActivity(i);
            } catch (Exception e) {
                try {
                    startActivity(new Intent(
                        Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
                } catch (Exception ignored) {}
            }
        }
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) applyWindowMode();
    }

    // Keyboard plugged/unplugged (or orientation, etc.) — re-evaluate. The manifest
    // lists keyboard|keyboardHidden|navigation in configChanges, so this fires
    // instead of recreating the activity.
    @Override
    public void onConfigurationChanged(Configuration newConfig) {
        super.onConfigurationChanged(newConfig);
        applyWindowMode();
    }

    @Override
    public void onMultiWindowModeChanged(boolean isInMultiWindowMode, Configuration newConfig) {
        super.onMultiWindowModeChanged(isInMultiWindowMode, newConfig);
        applyWindowMode();
    }

    @Override
    protected String[] getLibraries() {
        // libmain.so links the OCCT toolkits via DT_NEEDED, so the dynamic
        // loader pulls them (and libc++_shared) from the APK automatically.
        return new String[] {
            "SDL2",
            "main"
        };
    }
}
