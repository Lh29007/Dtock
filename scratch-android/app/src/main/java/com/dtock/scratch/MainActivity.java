package com.dtock.scratch;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.ContentValues;
import android.content.Intent;
import android.graphics.Color;
import android.view.Gravity;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.MediaStore;
import android.util.Base64;
import android.view.View;
import android.view.WindowManager;
import android.webkit.JavascriptInterface;
import android.webkit.ValueCallback;
import android.webkit.WebChromeClient;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.Toast;

import androidx.webkit.WebViewAssetLoader;

import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStream;

/**
 * Affiche l'éditeur Scratch (scratch-gui, compilé dans assets/) en plein écran.
 *
 * Les fichiers sont servis depuis https://appassets.androidplatform.net/ :
 * l'éditeur n'a donc pas besoin d'un accès fichier et fonctionne comme sur le web.
 *
 * Compatibilité .sb3 :
 * - Ouverture : le sélecteur de fichiers Android alimente l'<input type="file"> de scratch-gui.
 * - Enregistrement : le téléchargement blob de scratch-gui est intercepté et écrit
 *   dans Téléchargements (MediaStore sur Android 10+, dossier de l'app avant).
 */
public class MainActivity extends Activity {

    private static final String START_URL = "https://appassets.androidplatform.net/index.html";
    private static final int REQUEST_OPEN_FILE = 1001;
    private static final int REQUEST_NATIVE_OPEN = 1002;
    private static final String SB3_MIME = "application/x.scratch.sb3";

    /**
     * Intercepte les téléchargements blob (ex. File > Save to your computer) et les
     * transmet au natif. Les autres clics d'ancre gardent leur comportement normal.
     */
    private static final String SAVE_HOOK_JS =
            "(function(){"
            + "if (window.__dtockSaveHook) return; window.__dtockSaveHook = true;"
            + "var originalClick = HTMLAnchorElement.prototype.click;"
            + "HTMLAnchorElement.prototype.click = function(){"
            + "  var a = this;"
            + "  if (a.download && a.href && a.href.indexOf('blob:') === 0 && window.AndroidSave) {"
            + "    fetch(a.href).then(function(r){ return r.blob(); }).then(function(blob){"
            + "      var reader = new FileReader();"
            + "      reader.onloadend = function(){ window.AndroidSave.saveFile(a.download, reader.result); };"
            + "      reader.readAsDataURL(blob);"
            + "    });"
            + "    return;"
            + "  }"
            + "  return originalClick.apply(this, arguments);"
            + "};"
            + "})();";

    /**
     * scratch-gui est dessiné pour une largeur de bureau (~1100 px). Sur téléphone, on réduit
     * l'affichage avec CSS zoom pour que la scène et les blocs tiennent dans l'écran.
     * Recalculé au chargement et à chaque redimensionnement (rotation).
     */
    private static final String FIT_JS =
            "(function(){"
            + "var BASE_WIDTH = 1100;"
            + "function fit(){"
            + "  var z = Math.min(1, window.innerWidth / BASE_WIDTH);"
            + "  var h = document.documentElement;"
            + "  h.style.zoom = z.toFixed(3);"
            + "  h.style.width = (window.innerWidth / z) + 'px';"
            + "  h.style.height = (window.innerHeight / z) + 'px';"
            + "  h.style.overflow = 'hidden';"
            + "}"
            + "if (!window.__dtockFit) { window.__dtockFit = true; window.addEventListener('resize', fit); }"
            + "fit();"
            + "})();";

    /**
     * Fixe la largeur de mise en page à 1100 px : la WebView réduit alors la page entière
     * pour qu'elle tienne dans l'écran (scène et blocs visibles), au lieu de la couper.
     */
    private static final String FIXED_VIEWPORT_JS =
            "(function(){"
            + "var m = document.querySelector('meta[name=\"viewport\"]');"
            + "if (!m) { m = document.createElement('meta'); m.name = 'viewport'; document.head.appendChild(m); }"
            + "m.content = 'width=1100, user-scalable=no';"
            + "})();";

    /** Masque la barre de menus web : elle est remplacée par la barre native. */
    private static final String HIDE_WEB_CHROME_JS =
            "(function(){"
            + "if (document.getElementById('dtock-hide')) return;"
            + "var s = document.createElement('style'); s.id = 'dtock-hide';"
            + "s.textContent = '[class*=\"menu-bar_menu-bar\"]{display:none!important}';"
            + "document.head.appendChild(s);"
            + "})();";

    /**
     * Pilote l'éditeur depuis la barre native : lit les boutons existants de scratch-gui
     * (drapeau vert, stop, menu Fichier). Les libellés sont testés en français et en anglais.
     */
    private static final String NATIVE_ACTIONS_JS =
            "(function(){"
            + "function byText(sel, texts){"
            + "  var els = document.querySelectorAll(sel);"
            + "  for (var i = 0; i < els.length; i++) {"
            + "    var t = (els[i].textContent || '').trim();"
            + "    for (var j = 0; j < texts.length; j++) {"
            + "      if (t.indexOf(texts[j]) === 0) return els[i];"
            + "    }"
            + "  }"
            + "  return null;"
            + "}"
            + "function clickEl(el){"
            + "  var o = {bubbles: true, cancelable: true, view: window};"
            + "  ['pointerdown','mousedown','pointerup','mouseup','click'].forEach(function(t){"
            + "    el.dispatchEvent(new MouseEvent(t, o));"
            + "  });"
            + "}"
            + "function notify(msg){ if (window.AndroidSave && AndroidSave.toast) AndroidSave.toast(msg); }"
            + "function fileMenuItem(labels){"
            + "  var file = byText('[class*=\"menu-bar_menu-bar-item\"]', ['Fichier', 'File']);"
            + "  if (!file) { notify('Menu Fichier introuvable'); return; }"
            + "  clickEl(file);"
            + "  var tries = 0;"
            + "  (function wait(){"
            + "    var it = byText('[class*=\"menu-bar_menu-item\"]', labels);"
            + "    if (it) { clickEl(it); return; }"
            + "    if (++tries > 30) { notify('Option de menu introuvable'); return; }"
            + "    setTimeout(wait, 50);"
            + "  })();"
            + "}"
            + "window.dtock = {"
            + "  run: function(){ var g = document.querySelector('[class*=\"green-flag_green-flag\"]'); if (g) g.click(); },"
            + "  stop: function(){ var s = document.querySelector('[class*=\"stop-all\"]'); if (s) s.click(); },"
            + "  loadFile: function(name, b64){"
            + "    var bin = atob(b64);"
            + "    var bytes = new Uint8Array(bin.length);"
            + "    for (var i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);"
            + "    var file = new File([bytes], name, {type: 'application/x.scratch.sb3'});"
            + "    var input = document.querySelector('input[type=\"file\"]');"
            + "    if (!input) { notify('Chargeur de fichier introuvable'); return; }"
            + "    var dt = new DataTransfer();"
            + "    dt.items.add(file);"
            + "    input.files = dt.files;"
            + "    input.dispatchEvent(new Event('change', {bubbles: true}));"
            + "  },"
            + "  save: function(){ fileMenuItem(['Enregistrer sur votre ordinateur', 'Save to your computer']); },"
            + "  newProject: function(){ fileMenuItem(['Nouveau', 'New']); },"
            + "  stage: function(large){"
            + "    var toggles = document.querySelectorAll('[class*=\"stage-size-toggle\"]');"
            + "    var t = toggles[large ? 1 : 0];"
            + "    if (t) t.click();"
            + "  }"
            + "};"
            + "})();";

    private WebView webView;
    private WebViewAssetLoader assetLoader;
    private ValueCallback<Uri[]> pendingFileCallback;
    private TextView stageButton;
    private boolean stageLarge = false;

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    /** Barre d'outils Android native : titre, Ouvrir, Enregistrer, Lecture, Stop. */
    private LinearLayout buildNativeTopBar() {
        LinearLayout bar = new LinearLayout(this);
        bar.setOrientation(LinearLayout.HORIZONTAL);
        bar.setGravity(Gravity.CENTER_VERTICAL);
        bar.setBackgroundColor(Color.rgb(0x85, 0x5C, 0xD6)); // violet Scratch
        bar.setElevation(dp(4));

        TextView title = new TextView(this);
        title.setText("Scratch");
        title.setTextColor(Color.WHITE);
        title.setTextSize(20);
        title.setTypeface(null, android.graphics.Typeface.BOLD);
        title.setPadding(dp(16), 0, dp(8), 0);
        bar.addView(title, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.MATCH_PARENT, 1f));

        bar.addView(barButton("Nouveau", () -> runJs("window.dtock && dtock.newProject();")));
        bar.addView(barButton("Ouvrir", this::openNativeFile));
        bar.addView(barButton("Enregistrer", () -> runJs("window.dtock && dtock.save();")));

        // Bascule scène petite / grande (plein écran de la scène, blocs masqués).
        stageButton = barButton(stageLarge ? "Petite scène" : "Grande scène", () -> {
            stageLarge = !stageLarge;
            stageButton.setText(stageLarge ? "Petite scène" : "Grande scène");
            runJs("window.dtock && dtock.stage(" + stageLarge + ");");
        });
        bar.addView(stageButton);
        bar.addView(barButton("▶", () -> runJs("window.dtock && dtock.run();")));
        bar.addView(barButton("■", () -> runJs("window.dtock && dtock.stop();")));
        return bar;
    }

    private TextView barButton(String label, Runnable action) {
        TextView b = new TextView(this);
        b.setText(label);
        b.setTextColor(Color.WHITE);
        b.setTextSize(16);
        b.setGravity(Gravity.CENTER);
        b.setPadding(dp(12), 0, dp(12), 0);
        b.setOnClickListener(v -> action.run());
        b.setLayoutParams(new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.MATCH_PARENT));
        return b;
    }

    /** Ouvre le sélecteur Android côté natif, puis transmet le .sb3 à l'éditeur. */
    private void openNativeFile() {
        Intent pick = new Intent(Intent.ACTION_GET_CONTENT);
        pick.addCategory(Intent.CATEGORY_OPENABLE);
        pick.setType("*/*");
        try {
            startActivityForResult(Intent.createChooser(pick, "Ouvrir un projet"), REQUEST_NATIVE_OPEN);
        } catch (ActivityNotFoundException e) {
            Toast.makeText(this, "Aucun gestionnaire de fichiers", Toast.LENGTH_SHORT).show();
        }
    }

    private void loadProjectFromUri(Uri uri) {
        new Thread(() -> {
            try {
                String name = "projet.sb3";
                try (android.database.Cursor c = getContentResolver().query(uri, null, null, null, null)) {
                    if (c != null && c.moveToFirst()) {
                        int idx = c.getColumnIndex(android.provider.OpenableColumns.DISPLAY_NAME);
                        if (idx >= 0 && c.getString(idx) != null) {
                            name = c.getString(idx);
                        }
                    }
                }
                byte[] bytes;
                try (java.io.InputStream in = getContentResolver().openInputStream(uri)) {
                    if (in == null) throw new Exception("lecture impossible");
                    java.io.ByteArrayOutputStream buf = new java.io.ByteArrayOutputStream();
                    byte[] chunk = new byte[16384];
                    int n;
                    while ((n = in.read(chunk)) != -1) buf.write(chunk, 0, n);
                    bytes = buf.toByteArray();
                }
                String b64 = Base64.encodeToString(bytes, Base64.NO_WRAP);
                final String js = "window.dtock && dtock.loadFile("
                        + org.json.JSONObject.quote(name) + ", '" + b64 + "');";
                runOnUiThread(() -> runJs(js));
            } catch (Exception e) {
                showToast("Échec de l'ouverture : " + e.getMessage());
            }
        }).start();
    }

    private void runJs(String code) {
        webView.evaluateJavascript("(function(){ try { " + code + " } catch (e) {} })();", null);
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        assetLoader = new WebViewAssetLoader.Builder()
                .setDomain("appassets.androidplatform.net")
                .addPathHandler("/", new WebViewAssetLoader.AssetsPathHandler(this))
                .build();

        webView = new WebView(this);
        configureWebView(webView);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.addView(buildNativeTopBar(), new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, dp(52)));
        root.addView(webView, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f));
        setContentView(root);

        if (savedInstanceState != null) {
            webView.restoreState(savedInstanceState);
        } else {
            webView.loadUrl(START_URL);
        }
        hideSystemBars();
    }

    private void configureWebView(WebView view) {
        WebSettings s = view.getSettings();
        s.setJavaScriptEnabled(true);
        s.setDomStorageEnabled(true);
        s.setDatabaseEnabled(true);
        s.setMediaPlaybackRequiresUserGesture(false);
        s.setAllowFileAccess(false);
        s.setAllowContentAccess(false); // le sélecteur de fichiers accorde déjà l'accès au .sb3 choisi
        s.setSupportZoom(false);
        s.setBuiltInZoomControls(false);
        s.setDisplayZoomControls(false);
        s.setUseWideViewPort(true);
        s.setLoadWithOverviewMode(true);

        view.addJavascriptInterface(new SaveBridge(), "AndroidSave");

        // Fluidité : rendu matériel de la vue, pas d'effet de rebond ni de barres de défilement
        // qui gênent le glisser-déposer des blocs et de la scène.
        view.setLayerType(View.LAYER_TYPE_HARDWARE, null);
        view.setOverScrollMode(View.OVER_SCROLL_NEVER);
        view.setVerticalScrollBarEnabled(false);
        view.setHorizontalScrollBarEnabled(false);

        view.setWebChromeClient(new WebChromeClient() {
            @Override
            public boolean onShowFileChooser(WebView w, ValueCallback<Uri[]> callback,
                                             FileChooserParams params) {
                if (pendingFileCallback != null) {
                    pendingFileCallback.onReceiveValue(null);
                }
                pendingFileCallback = callback;

                Intent pick = new Intent(Intent.ACTION_GET_CONTENT);
                pick.addCategory(Intent.CATEGORY_OPENABLE);
                pick.setType("*/*");
                try {
                    startActivityForResult(Intent.createChooser(pick, "Ouvrir un projet"), REQUEST_OPEN_FILE);
                } catch (ActivityNotFoundException e) {
                    pendingFileCallback.onReceiveValue(null);
                    pendingFileCallback = null;
                    Toast.makeText(MainActivity.this, "Aucun gestionnaire de fichiers", Toast.LENGTH_SHORT).show();
                    return false;
                }
                return true;
            }
        });

        view.setWebViewClient(new WebViewClient() {
            @Override
            public WebResourceResponse shouldInterceptRequest(WebView v, WebResourceRequest request) {
                return assetLoader.shouldInterceptRequest(request.getUrl());
            }

            @Override
            public void onPageFinished(WebView v, String url) {
                super.onPageFinished(v, url);
                v.evaluateJavascript(HIDE_WEB_CHROME_JS, null);
                v.evaluateJavascript(NATIVE_ACTIONS_JS, null);
                v.evaluateJavascript(SAVE_HOOK_JS, null);
                v.evaluateJavascript(FIXED_VIEWPORT_JS, null);
            }
        });
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQUEST_NATIVE_OPEN) {
            if (resultCode == RESULT_OK && data != null && data.getData() != null) {
                loadProjectFromUri(data.getData());
            }
            return;
        }
        if (requestCode != REQUEST_OPEN_FILE || pendingFileCallback == null) {
            return;
        }
        Uri[] result = null;
        if (resultCode == RESULT_OK && data != null && data.getData() != null) {
            result = new Uri[]{data.getData()};
        }
        pendingFileCallback.onReceiveValue(result);
        pendingFileCallback = null;
    }

    private void hideSystemBars() {
        getWindow().getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                        | View.SYSTEM_UI_FLAG_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                        | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN);
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            hideSystemBars();
        }
    }

    @Override
    protected void onSaveInstanceState(Bundle outState) {
        super.onSaveInstanceState(outState);
        webView.saveState(outState);
    }

    @Override
    protected void onPause() {
        super.onPause();
        webView.onPause();
    }

    @Override
    protected void onResume() {
        super.onResume();
        webView.onResume();
    }

    @Override
    public void onBackPressed() {
        if (webView.canGoBack()) {
            webView.goBack();
        } else {
            super.onBackPressed();
        }
    }

    @Override
    protected void onDestroy() {
        if (webView != null) {
            webView.destroy();
        }
        super.onDestroy();
    }

    /** Pont JavaScript → natif appelé par SAVE_HOOK_JS (thread d'arrière-plan). */
    private class SaveBridge {
        @JavascriptInterface
        public void toast(String message) {
            showToast(message);
        }

        @JavascriptInterface
        public void saveFile(String name, String dataUrl) {
            try {
                int comma = dataUrl.indexOf(',');
                byte[] bytes = Base64.decode(dataUrl.substring(comma + 1), Base64.DEFAULT);
                String fileName = sanitize(name);
                String where = writeToDownloads(fileName, bytes);
                showToast("Enregistré : " + where);
            } catch (Exception e) {
                showToast("Échec de l'enregistrement : " + e.getMessage());
            }
        }
    }

    private static String sanitize(String name) {
        String clean = (name == null || name.trim().isEmpty()) ? "projet.sb3" : name.replaceAll("[\\\\/:*?\"<>|]", "_");
        return clean.endsWith(".sb3") ? clean : clean + ".sb3";
    }

    private String writeToDownloads(String fileName, byte[] bytes) throws Exception {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            ContentValues values = new ContentValues();
            values.put(MediaStore.Downloads.DISPLAY_NAME, fileName);
            values.put(MediaStore.Downloads.MIME_TYPE, SB3_MIME);
            values.put(MediaStore.Downloads.IS_PENDING, 1);
            Uri uri = getContentResolver().insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values);
            if (uri == null) {
                throw new Exception("insertion impossible");
            }
            try (OutputStream out = getContentResolver().openOutputStream(uri)) {
                if (out == null) {
                    throw new Exception("écriture impossible");
                }
                out.write(bytes);
            }
            ContentValues done = new ContentValues();
            done.put(MediaStore.Downloads.IS_PENDING, 0);
            getContentResolver().update(uri, done, null, null);
            return "Téléchargements/" + fileName;
        }

        // Android 9 et antérieur : dossier de l'app, lisible dans le gestionnaire de fichiers
        // (Android/data/com.dtock.scratch/files/Download), sans permission de stockage.
        File dir = getExternalFilesDir(Environment.DIRECTORY_DOWNLOADS);
        if (dir == null) {
            dir = getFilesDir();
        }
        File target = new File(dir, fileName);
        try (FileOutputStream out = new FileOutputStream(target)) {
            out.write(bytes);
        }
        return target.getAbsolutePath();
    }

    private void showToast(final String message) {
        runOnUiThread(() -> Toast.makeText(MainActivity.this, message, Toast.LENGTH_LONG).show());
    }
}
