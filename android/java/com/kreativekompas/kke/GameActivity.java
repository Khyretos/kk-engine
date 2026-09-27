package com.kreativekompas.kke;

import android.content.pm.ActivityInfo;
import android.content.pm.PackageManager;

import org.libsdl.app.SDLActivity;

/**
 * Runs one game. Every game is its own shared library (lib<game>.so, built
 * by kke_add_game) with the engine and SDL linked in statically, and its
 * ordinary main() is the entry point. Which game: the "game" extra from
 * LauncherActivity, else the kke.game meta-data in the manifest.
 */
public class GameActivity extends SDLActivity {
    public static final String EXTRA_GAME = "game";
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
