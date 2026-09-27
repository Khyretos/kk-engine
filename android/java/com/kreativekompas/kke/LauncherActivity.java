package com.kreativekompas.kke;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.view.View;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.ListView;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

/**
 * The list of games in an APK that holds several (the demos APK). Reads
 * assets/kke_games.txt, one "library<TAB>title" per line, written by
 * android/build_apk.py, and starts GameActivity with the one picked.
 */
public class LauncherActivity extends Activity {
    private final List<String> mLibraries = new ArrayList<>();
    private final List<String> mTitles = new ArrayList<>();

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        try (BufferedReader in = new BufferedReader(new InputStreamReader(getAssets().open("kke_games.txt"), StandardCharsets.UTF_8))) {
            String line;
            while ((line = in.readLine()) != null) {
                int tab = line.indexOf('\t');
                if (tab > 0) {
                    mLibraries.add(line.substring(0, tab));
                    mTitles.add(line.substring(tab + 1));
                }
            }
        } catch (IOException e) {
            mTitles.add("No games in this APK: " + e.getMessage());
        }

        ListView list = new ListView(this);
        list.setAdapter(new ArrayAdapter<>(this, android.R.layout.simple_list_item_1, mTitles));
        list.setOnItemClickListener(new AdapterView.OnItemClickListener() {
            @Override
            public void onItemClick(AdapterView<?> parent, View view, int position, long id) {
                if (position < mLibraries.size()) {
                    Intent intent = new Intent(LauncherActivity.this, GameActivity.class);
                    intent.putExtra(GameActivity.EXTRA_GAME, mLibraries.get(position));
                    intent.putExtra(GameActivity.EXTRA_LABEL, mTitles.get(position));
                    startActivity(intent);
                }
            }
        });
        setContentView(list);
    }
}
