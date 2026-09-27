package com.kreativekompas.kke;

import android.app.Activity;
import android.app.ActivityManager;
import android.content.ContentResolver;
import android.content.ContentValues;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.Process;
import android.os.SystemClock;
import android.provider.MediaStore;
import android.view.Gravity;
import android.view.View;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.Iterator;
import java.util.List;
import java.util.Locale;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

/**
 * The Android benchmark (docs/ANDROID.md, docs/BENCHMARKS.md): the same
 * suite as kke_benchmark on a PC. Android can't start programs, so this
 * activity plays the part of kke_benchmark's launcher: it starts each demo
 * in GameActivity (its own process) with the benchmark's environment, gives
 * it the suite's time plus the load timeout, and records whether it wrote
 * its report, crashed or hung. kke_benchmark itself, when the APK has it,
 * then turns the reports and logs into the one results file (--collect);
 * otherwise everything goes into one zip. Either way the phone's share menu
 * opens so the results can be sent, and a copy goes to Downloads.
 *
 * Reads assets/kke_suite.json, which android/build_apk.py makes from
 * benchmarks/suite.yaml.
 */
public class BenchmarkActivity extends Activity {
    private static final int REQUEST_DEMO = 1;
    private static final int REQUEST_COLLECT = 2;
    private static final String COLLECTOR = "kke_benchmark";

    private JSONObject mSuite;
    private JSONArray mDemos;       // the demos of this run (quick or full)
    private JSONArray mRuns;        // what happened to each one so far
    private File mRunDir;
    private File mResultsDir;
    private int mNext;
    private long mStartedAt;
    private boolean mHung;
    private boolean mRunning;
    private final Handler mHandler = new Handler(Looper.getMainLooper());
    private final Runnable mWatchdog = new Runnable() {
        @Override
        public void run() {
            mHung = true;
            killGameProcess();
        }
    };

    private TextView mStatus;
    private Button mFull;
    private Button mQuick;
    private Button mShare;
    private File mResultFile;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        mResultsDir = new File(getFilesDir(), "benchmark-results");
        try {
            mSuite = new JSONObject(readAsset("kke_suite.json"));
        } catch (IOException | JSONException e) {
            mSuite = null;
        }
        buildUi();
        if (mSuite == null) {
            setStatus("This APK has no benchmark suite (kke_suite.json).");
            mFull.setEnabled(false);
            mQuick.setEnabled(false);
            return;
        }
        if (savedInstanceState != null && savedInstanceState.getBoolean("running")) {
            // Android closed this activity while a demo ran: carry on.
            try {
                mDemos = new JSONArray(savedInstanceState.getString("demos"));
                mRuns = new JSONArray(savedInstanceState.getString("runs"));
                mRunDir = new File(savedInstanceState.getString("runDir"));
                mNext = savedInstanceState.getInt("next");
                mStartedAt = savedInstanceState.getLong("startedAt");
                mRunning = true;
                mFull.setEnabled(false);
                mQuick.setEnabled(false);
            } catch (JSONException e) {
                mRunning = false;
            }
        }
    }

    @Override
    protected void onSaveInstanceState(Bundle out) {
        super.onSaveInstanceState(out);
        out.putBoolean("running", mRunning);
        if (mRunning) {
            out.putString("demos", mDemos.toString());
            out.putString("runs", mRuns.toString());
            out.putString("runDir", mRunDir.getAbsolutePath());
            out.putInt("next", mNext);
            out.putLong("startedAt", mStartedAt);
        }
    }

    private void buildUi() {
        LinearLayout column = new LinearLayout(this);
        column.setOrientation(LinearLayout.VERTICAL);
        column.setGravity(Gravity.CENTER_HORIZONTAL);
        int pad = (int) (24 * getResources().getDisplayMetrics().density);
        column.setPadding(pad, pad, pad, pad);

        TextView title = new TextView(this);
        title.setText("KKE Benchmark");
        title.setTextSize(28);
        column.addView(title);

        TextView about = new TextView(this);
        about.setText("Plays every demo for a short while, one after the other, and measures how smoothly this phone runs it. "
                + "Plug the phone in, keep it still and don't touch the screen while it runs. "
                + "At the end you can send the results file (nothing is sent by itself).");
        about.setPadding(0, pad / 2, 0, pad / 2);
        column.addView(about);

        mFull = new Button(this);
        mFull.setText("Run the benchmark (about " + minutes(false) + " min)");
        mFull.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                start(false);
            }
        });
        column.addView(mFull);

        mQuick = new Button(this);
        mQuick.setText("Quick run (about " + minutes(true) + " min)");
        mQuick.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                start(true);
            }
        });
        column.addView(mQuick);

        mShare = new Button(this);
        mShare.setText("Send the results");
        mShare.setEnabled(false);
        mShare.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                share();
            }
        });
        column.addView(mShare);

        mStatus = new TextView(this);
        mStatus.setPadding(0, pad / 2, 0, 0);
        column.addView(mStatus);

        ScrollView scroll = new ScrollView(this);
        scroll.addView(column);
        setContentView(scroll);
    }

    private void setStatus(String text) {
        mStatus.setText(text);
    }

    private JSONArray demosFor(boolean quick) throws JSONException {
        JSONArray all = mSuite.getJSONArray("demos");
        JSONArray chosen = new JSONArray();
        for (int i = 0; i < all.length(); i++) {
            JSONObject d = new JSONObject(all.getJSONObject(i).toString());
            if (quick) {
                if (!d.optBoolean("quick", true)) {
                    continue;
                }
                d.put("seconds", Math.min(d.getDouble("seconds"), 8.0));
                d.put("warmup", Math.min(d.getDouble("warmup"), 2.0));
            }
            chosen.put(d);
        }
        return chosen;
    }

    private int minutes(boolean quick) {
        if (mSuite == null) {
            return 0;
        }
        try {
            JSONArray demos = demosFor(quick);
            double total = 0;
            for (int i = 0; i < demos.length(); i++) {
                JSONObject d = demos.getJSONObject(i);
                total += d.getDouble("warmup") + d.getDouble("seconds") + 8.0; // + loading and unpacking
            }
            return (int) Math.ceil(total / 60.0);
        } catch (JSONException e) {
            return 0;
        }
    }

    private void start(boolean quick) {
        try {
            mDemos = demosFor(quick);
        } catch (JSONException e) {
            setStatus("The benchmark suite is broken: " + e.getMessage());
            return;
        }
        String stamp = new SimpleDateFormat("yyyy-MM-dd_HH-mm-ss", Locale.US).format(new Date());
        mRunDir = new File(new File(getFilesDir(), "benchmark-runs"), "run_" + stamp);
        new File(mRunDir, "reports").mkdirs();
        new File(mRunDir, "logs").mkdirs();
        mRuns = new JSONArray();
        mNext = 0;
        mRunning = true;
        mResultFile = null;
        mFull.setEnabled(false);
        mQuick.setEnabled(false);
        mShare.setEnabled(false);
        startNext();
    }

    private boolean hasLibrary(String lib) {
        return new File(getApplicationInfo().nativeLibraryDir, "lib" + lib + ".so").exists();
    }

    private void startNext() {
        if (mNext >= mDemos.length()) {
            finishRun();
            return;
        }
        try {
            JSONObject d = mDemos.getJSONObject(mNext);
            String id = d.getString("id");
            String lib = d.optString("exe", id);
            setStatus("[" + (mNext + 1) + "/" + mDemos.length() + "] " + d.optString("title", id));
            if (!hasLibrary(lib)) {
                record(id, "missing", 0.0);
                mNext++;
                startNext();
                return;
            }
            String[] env = environment(d, id);
            Intent intent = new Intent(this, GameActivity.class);
            intent.putExtra(GameActivity.EXTRA_GAME, lib);
            intent.putExtra(GameActivity.EXTRA_ENV, env);
            mHung = false;
            mStartedAt = SystemClock.elapsedRealtime();
            long limitMs = (long) ((d.getDouble("warmup") + d.getDouble("seconds") + mSuite.optDouble("load_timeout", 120.0)) * 1000.0);
            mHandler.postDelayed(mWatchdog, limitMs);
            startActivityForResult(intent, REQUEST_DEMO);
        } catch (JSONException e) {
            setStatus("The benchmark suite is broken: " + e.getMessage());
            mRunning = false;
        }
    }

    // What kke_benchmark gives each demo on a PC, plus the log file (a PC
    // launcher captures the demo's output; here the demo writes it).
    private String[] environment(JSONObject d, String id) throws JSONException {
        List<String> env = new ArrayList<>();
        env.add("KKE_BENCHMARK=" + d.getDouble("seconds"));
        env.add("KKE_BENCH_WARMUP=" + d.getDouble("warmup"));
        env.add("KKE_BENCH_DIR=" + new File(mRunDir, "reports").getAbsolutePath());
        env.add("KKE_BENCH_NAME=" + id);
        env.add("KKE_BENCH_ITEM=" + id);
        env.add("KKE_SKIP_INTRO=1");
        env.add("KKE_LOG_FILE=" + new File(new File(mRunDir, "logs"), id + ".log").getAbsolutePath());
        JSONObject extra = d.optJSONObject("env");
        if (extra != null) {
            for (Iterator<String> keys = extra.keys(); keys.hasNext();) {
                String k = keys.next();
                env.add(k + "=" + extra.getString(k));
            }
        }
        return env.toArray(new String[0]);
    }

    private void killGameProcess() {
        ActivityManager am = (ActivityManager) getSystemService(ACTIVITY_SERVICE);
        if (am == null || am.getRunningAppProcesses() == null) {
            return;
        }
        String game = getPackageName() + ":game";
        for (ActivityManager.RunningAppProcessInfo p : am.getRunningAppProcesses()) {
            if (game.equals(p.processName)) {
                Process.killProcess(p.pid);
            }
        }
    }

    private void record(String id, String status, double wallSeconds) {
        try {
            JSONObject run = new JSONObject();
            run.put("id", id);
            run.put("status", status);
            run.put("wall_s", Math.round(wallSeconds * 10.0) / 10.0);
            mRuns.put(run);
            writeFile(new File(mRunDir, "runs.json"), mRuns.toString(1));
        } catch (JSONException | IOException e) {
            setStatus("Could not record " + id + ": " + e.getMessage());
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQUEST_DEMO && mRunning) {
            mHandler.removeCallbacks(mWatchdog);
            double wall = (SystemClock.elapsedRealtime() - mStartedAt) / 1000.0;
            try {
                String id = mDemos.getJSONObject(mNext).getString("id");
                boolean reported = new File(new File(mRunDir, "reports"), id + ".json").exists();
                record(id, mHung ? "hung" : reported ? "ok" : "crashed", wall);
            } catch (JSONException e) {
                setStatus("The benchmark suite is broken: " + e.getMessage());
            }
            mNext++;
            startNext();
        } else if (requestCode == REQUEST_COLLECT) {
            File newest = newestResult();
            if (newest != null) {
                done(newest);
            } else {
                zipAndFinish("kke_benchmark could not write the results file");
            }
        }
    }

    private void finishRun() {
        mRunning = false;
        mResultsDir.mkdirs();
        File suite = new File(new File(getFilesDir(), "benchmark"), "benchmark_suite.yaml");
        if (hasLibrary(COLLECTOR) && suite.exists()) {
            setStatus("Putting the results together...");
            Intent intent = new Intent(this, GameActivity.class);
            intent.putExtra(GameActivity.EXTRA_GAME, COLLECTOR);
            intent.putExtra(GameActivity.EXTRA_ARGS, new String[] {
                "--collect", mRunDir.getAbsolutePath(), "--suite", suite.getAbsolutePath(),
                "--out", mResultsDir.getAbsolutePath(), "--no-wait", "--no-open" });
            startActivityForResult(intent, REQUEST_COLLECT);
        } else {
            zipAndFinish(null);
        }
    }

    private File newestResult() {
        File[] files = mResultsDir.listFiles();
        File newest = null;
        if (files != null) {
            for (File f : files) {
                if (f.getName().startsWith("kke-benchmark-") && f.getName().endsWith(".json")
                        && f.lastModified() >= mRunDir.lastModified() && (newest == null || f.lastModified() > newest.lastModified())) {
                    newest = f;
                }
            }
        }
        return newest;
    }

    // Every report, log and runs.json of this run in one file.
    private void zipAndFinish(String problem) {
        File zip = new File(mResultsDir, "kke-benchmark-" + mRunDir.getName().substring(4) + "-android.zip");
        try (ZipOutputStream out = new ZipOutputStream(new FileOutputStream(zip))) {
            addToZip(out, mRunDir, "");
        } catch (IOException e) {
            setStatus("Could not write the results: " + e.getMessage());
            return;
        }
        done(zip);
        if (problem != null) {
            setStatus(mStatus.getText() + "\n(" + problem + ")");
        }
    }

    private void addToZip(ZipOutputStream out, File dir, String prefix) throws IOException {
        File[] files = dir.listFiles();
        if (files == null) {
            return;
        }
        byte[] buf = new byte[65536];
        for (File f : files) {
            if (f.isDirectory()) {
                addToZip(out, f, prefix + f.getName() + "/");
                continue;
            }
            out.putNextEntry(new ZipEntry(prefix + f.getName()));
            try (InputStream in = new FileInputStream(f)) {
                int n;
                while ((n = in.read(buf)) > 0) {
                    out.write(buf, 0, n);
                }
            }
            out.closeEntry();
        }
    }

    private void done(File result) {
        mResultFile = result;
        int ok = 0;
        for (int i = 0; i < mRuns.length(); i++) {
            if ("ok".equals(mRuns.optJSONObject(i).optString("status"))) {
                ok++;
            }
        }
        String saved = saveToDownloads(result);
        setStatus("Done: " + ok + " of " + mRuns.length() + " demos ran fine.\n" + result.getName()
                + (saved != null ? "\nA copy is in " + saved + "." : ""));
        mFull.setEnabled(true);
        mQuick.setEnabled(true);
        mShare.setEnabled(true);
        share();
    }

    private static String mimeType(File f) {
        return f.getName().endsWith(".zip") ? "application/zip" : f.getName().endsWith(".json") ? "application/json" : "text/plain";
    }

    // Downloads/KKE Benchmark, where a file manager finds it (Android 10+).
    private String saveToDownloads(File result) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) {
            return null;
        }
        ContentResolver resolver = getContentResolver();
        ContentValues values = new ContentValues();
        values.put(MediaStore.MediaColumns.DISPLAY_NAME, result.getName());
        values.put(MediaStore.MediaColumns.MIME_TYPE, mimeType(result));
        values.put(MediaStore.MediaColumns.RELATIVE_PATH, "Download/KKE Benchmark");
        Uri uri = resolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values);
        if (uri == null) {
            return null;
        }
        try (OutputStream out = resolver.openOutputStream(uri); InputStream in = new FileInputStream(result)) {
            if (out == null) {
                return null;
            }
            byte[] buf = new byte[65536];
            int n;
            while ((n = in.read(buf)) > 0) {
                out.write(buf, 0, n);
            }
        } catch (IOException e) {
            return null;
        }
        return "Downloads/KKE Benchmark";
    }

    private void share() {
        if (mResultFile == null) {
            return;
        }
        Uri uri = ResultsProvider.uriFor(this, mResultFile);
        Intent send = new Intent(Intent.ACTION_SEND);
        send.setType(mimeType(mResultFile));
        send.putExtra(Intent.EXTRA_STREAM, uri);
        send.putExtra(Intent.EXTRA_SUBJECT, "KKE benchmark: " + Build.MANUFACTURER + " " + Build.MODEL);
        send.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        startActivity(Intent.createChooser(send, "Send the benchmark results"));
    }

    private String readAsset(String name) throws IOException {
        StringBuilder sb = new StringBuilder();
        try (BufferedReader in = new BufferedReader(new InputStreamReader(getAssets().open(name), StandardCharsets.UTF_8))) {
            String line;
            while ((line = in.readLine()) != null) {
                sb.append(line).append('\n');
            }
        }
        return sb.toString();
    }

    private static void writeFile(File f, String text) throws IOException {
        try (OutputStream out = new FileOutputStream(f)) {
            out.write(text.getBytes(StandardCharsets.UTF_8));
        }
    }
}
