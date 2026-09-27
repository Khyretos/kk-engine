package com.kreativekompas.kke;

import android.content.pm.ActivityInfo;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.system.ErrnoException;
import android.system.Os;
import android.util.Log;

import org.libsdl.app.SDLActivity;

/**
 * Runs one game. Every game is its own shared library (lib<game>.so, built
 * by kke_add_game) with the engine and SDL linked in statically, and its
 * ordinary main() is the entry point. Which game: the "game" extra from
 * LauncherActivity, else the kke.game meta-data in the manifest.
 *
 * Optional extras: "env", a String[] of NAME=value environment variables set before the
 * game's library loads (the benchmark's KKE_BENCHMARK=..., a demo's
 * autopilot switch), and "args", the String[] main() gets after argv[0].
 */
public class GameActivity extends SDLActivity {
    public static final String EXTRA_GAME = "game";
    public static final String EXTRA_ENV = "env";
    public static final String EXTRA_ARGS = "args";
    private static final String TAG_GAME = "kke.game";

    private String game() {
        String fromIntent = getIntent() != null ? getIntent().getStringExtra(EXTRA_GAME) : null;
        if (fromIntent != null && !fromIntent.isEmpty()) {
            return fromIntent;
        }
        try {
            ActivityInfo info = getPackageManager().getActivityInfo(getComponentName(), PackageManager.GET_META_DATA);
            if (info.metaData != null) {
                String fromManifest = info.metaData.getString(TAG_GAME);
                if (fromManifest != null) {
                    return fromManifest;
                }
            }
        } catch (PackageManager.NameNotFoundException e) {
            // Cannot happen for our own activity; fall through.
        }
        return "main";
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        // Before super.onCreate(), which loads the library: SDL reads the
        // process environment once, when it starts.
        String[] env = getIntent() != null ? getIntent().getStringArrayExtra(EXTRA_ENV) : null;
        if (env != null) {
            for (String pair : env) {
                int eq = pair.indexOf('=');
                if (eq <= 0) {
                    continue;
                }
                try {
                    Os.setenv(pair.substring(0, eq), pair.substring(eq + 1), true);
                } catch (ErrnoException e) {
                    Log.w("kke", "could not set " + pair + ": " + e.getMessage());
                }
            }
        }
        super.onCreate(savedInstanceState);
    }

    @Override
    protected String[] getArguments() {
        String[] args = getIntent() != null ? getIntent().getStringArrayExtra(EXTRA_ARGS) : null;
        return args != null ? args : new String[0];
    }

    @Override
    protected String[] getLibraries() {
        // SDL is inside the game's library, so that is the only one.
        return new String[] { game() };
    }

    @Override
    protected String getMainFunction() {
        return "main";
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        // The game's process ends with it: a second game (or the same one
        // again) then starts in a fresh process with fresh static state.
        if (isFinishing()) {
            System.exit(0);
        }
    }
}
