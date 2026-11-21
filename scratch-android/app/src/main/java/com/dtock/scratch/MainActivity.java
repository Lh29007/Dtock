package com.dtock.scratch;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.ContentValues;
import android.content.Intent;
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

    private WebView webView;
    private WebViewAssetLoader assetLoader;
    private ValueCallback<Uri[]> pendingFileCallback;

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
        setContentView(webView);

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
                v.evaluateJavascript(SAVE_HOOK_JS, null);
                v.evaluateJavascript(FIT_JS, null);
            }
        });
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
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
