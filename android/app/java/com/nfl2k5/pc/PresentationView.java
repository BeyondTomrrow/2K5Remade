package com.nfl2k5.pc;

import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.PorterDuff;
import android.os.Handler;
import android.os.Looper;
import android.util.SparseArray;
import android.view.Choreographer;
import android.view.View;
import android.view.ViewGroup;
import android.webkit.JavascriptInterface;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.RelativeLayout;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import java.util.Map;

/* The HTML broadcast presentation (src/presentation/webview2_host.cpp, the
 * Android half). A WebView laid out behind the game's surface at the
 * package's canvas size (1920x1080 CSS pixels) runs the page; ~30 times a
 * second it is drawn, scaled down, into a bitmap whose pixels go to the game,
 * which draws them over the picture like the Windows build's captured frames.
 * The page's files are served from the game folder under the same
 * https://presentation.local/ (teams.local, ...) names the Windows host uses,
 * and window.chrome.webview is provided so packages run unchanged. */
public class PresentationView {
    private static final Handler ui = new Handler(Looper.getMainLooper());
    private static final SparseArray<PresentationView> views = new SparseArray<>();

    private static native void nativeFrame(int id, ByteBuffer pixels, int w, int h);
    private static native String nativeTake(int id);
    private static native void nativeMessage(int id, String text);
    private static native void nativeStatus(int id, String text);

    private final int id;
    private final Map<String, File> hosts = new HashMap<>();
    private final int cssW, cssH, outW, outH;
    private WebView web;
    private Bitmap bitmap;
    private Canvas canvas;
    private ByteBuffer buffer;
    private boolean ready, flushQueued, stopped, asking, forceDraw = true;
    private long lastDraw, lastFull;

    private static final String SHIM =
        "<script>(function(){var L=[];window.NFL2K5_HOST='game';" +
        "window.chrome=window.chrome||{};window.chrome.webview={" +
        "addEventListener:function(t,f){if(t==='message')L.push(f);}," +
        "removeEventListener:function(t,f){var i=L.indexOf(f);if(i>=0)L.splice(i,1);}," +
        "postMessage:function(s){NFL2K5Native.post(String(s));}};" +
        "window.__nfl2k5_recv=function(d){for(var i=0;i<L.length;i++){try{L[i]({data:d});}catch(e){}}};" +
        /* "anything to draw?": the page changed or an animation is running */
        "var D=true;try{new MutationObserver(function(){D=true;}).observe(document,{subtree:true,childList:true," +
        "attributes:true,characterData:true});}catch(e){}" +
        "window.__nfl2k5_busy=function(){var b=D;D=false;try{var a=document.getAnimations();" +
        "for(var i=0;i<a.length;i++)if(a[i].playState==='running'){b=true;break;}}catch(e){b=true;}return b;};})();</script>";

    private PresentationView(int id, int cssW, int cssH, int outW, int outH) {
        this.id = id; this.cssW = cssW; this.cssH = cssH; this.outW = outW; this.outH = outH;
    }

    /* Called by the game (any thread). */
    public static void start(final int id, final String root, final String teams, final String eras,
                             final String portraits, final String entryUrl,
                             final int cssW, final int cssH, final int outW, final int outH) {
        ui.post(new Runnable() { public void run() {
            PresentationView v = new PresentationView(id, cssW, cssH, outW, outH);
            if (root != null && !root.isEmpty()) v.hosts.put("presentation.local", new File(root));
            if (teams != null && !teams.isEmpty()) v.hosts.put("teams.local", new File(teams));
            if (eras != null && !eras.isEmpty()) v.hosts.put("erateams.local", new File(eras));
            if (portraits != null && !portraits.isEmpty()) {
                new File(portraits).mkdirs();
                v.hosts.put("portraits.local", new File(portraits));
            }
            views.put(id, v);
            v.create(entryUrl);
        }});
    }

    public static void stop(final int id) {
        ui.post(new Runnable() { public void run() {
            PresentationView v = views.get(id);
            if (v == null) return;
            views.remove(id);
            v.stopped = true;
            if (v.web != null) {
                ViewGroup p = (ViewGroup) v.web.getParent();
                if (p != null) p.removeView(v.web);
                v.web.destroy();
                v.web = null;
            }
        }});
    }

    /* The game has new state/events for this page. */
    public static void kick(final int id) {
        ui.post(new Runnable() { public void run() {
            PresentationView v = views.get(id);
            if (v != null) v.flush();
        }});
    }

    private void create(String entryUrl) {
        ViewGroup layout = NFL2K5Activity.layout();
        if (layout == null) { nativeStatus(id, "error: no activity layout"); return; }
        web = new WebView(layout.getContext());
        web.setBackgroundColor(Color.TRANSPARENT);
        web.setFocusable(false);
        web.setClickable(false);
        web.setVerticalScrollBarEnabled(false);
        web.setHorizontalScrollBarEnabled(false);
        WebSettings s = web.getSettings();
        s.setJavaScriptEnabled(true);
        s.setDomStorageEnabled(true);
        s.setMediaPlaybackRequiresUserGesture(false);
        s.setUseWideViewPort(true);
        s.setLoadWithOverviewMode(true);
        s.setSupportZoom(false);
        s.setTextZoom(100);
        s.setAllowFileAccess(false);
        web.addJavascriptInterface(new Object() {
            @JavascriptInterface public void post(String text) { nativeMessage(id, text); }
        }, "NFL2K5Native");
        web.setWebViewClient(new WebViewClient() {
            @Override public WebResourceResponse shouldInterceptRequest(WebView view, WebResourceRequest req) {
                return serve(req.getUrl().getHost(), req.getUrl().getPath());
            }
            @Override public void onPageFinished(WebView view, String url) {
                if (stopped) return;
                ready = true;
                nativeStatus(id, "running");
                flush();
                web.evaluateJavascript("NFL2K5Native.post('android viewport '+innerWidth+'x'+innerHeight)", null);
            }
        });
        /* Behind the game's surface: never seen, but attached and visible, so
         * the page runs its animations. */
        RelativeLayout.LayoutParams lp = new RelativeLayout.LayoutParams(cssW, cssH);
        layout.addView(web, 0, lp);
        bitmap = Bitmap.createBitmap(outW, outH, Bitmap.Config.ARGB_8888);
        canvas = new Canvas(bitmap);
        canvas.scale((float) outW / cssW, (float) outH / cssH);
        buffer = ByteBuffer.allocateDirect(outW * outH * 4);
        nativeStatus(id, "loading " + entryUrl);
        web.loadUrl(entryUrl);
        Choreographer.getInstance().postFrameCallback(frame);
    }

    private final Choreographer.FrameCallback frame = new Choreographer.FrameCallback() {
        public void doFrame(long nanos) {
            if (stopped || web == null) return;
            /* ~30 frames a second is plenty for broadcast graphics and half
             * the work of matching the display -- and only while the page
             * changes or animates: drawing a still scorebug again and again
             * was a third of a CPU core (2026-10-30 log). Once a second
             * regardless, for anything the check cannot see. */
            if (ready && !asking && nanos - lastDraw >= 30_000_000L) {
                lastDraw = nanos;
                if (forceDraw || nanos - lastFull >= 1_000_000_000L) {
                    forceDraw = false;
                    lastFull = nanos;
                    draw();
                } else {
                    asking = true;
                    web.evaluateJavascript("window.__nfl2k5_busy?__nfl2k5_busy():true", new android.webkit.ValueCallback<String>() {
                        public void onReceiveValue(String v) {
                            asking = false;
                            if (!stopped && web != null && !"false".equals(v)) draw();
                        }
                    });
                }
            }
            Choreographer.getInstance().postFrameCallback(this);
        }
    };

    private void draw() {
        canvas.save();
        canvas.setMatrix(null);
        canvas.drawColor(0, PorterDuff.Mode.CLEAR);
        canvas.restore();
        web.draw(canvas);
        buffer.rewind();
        bitmap.copyPixelsToBuffer(buffer);
        nativeFrame(id, buffer, outW, outH);
    }

    private void flush() {
        if (!ready || web == null) return;
        String js = nativeTake(id);
        if (js != null && !js.isEmpty()) { web.evaluateJavascript(js, null); forceDraw = true; }
    }

    private static String mime(String path) {
        String p = path.toLowerCase();
        if (p.endsWith(".html") || p.endsWith(".htm")) return "text/html";
        if (p.endsWith(".js")) return "text/javascript";
        if (p.endsWith(".css")) return "text/css";
        if (p.endsWith(".json")) return "application/json";
        if (p.endsWith(".png")) return "image/png";
        if (p.endsWith(".jpg") || p.endsWith(".jpeg")) return "image/jpeg";
        if (p.endsWith(".webp")) return "image/webp";
        if (p.endsWith(".gif")) return "image/gif";
        if (p.endsWith(".svg")) return "image/svg+xml";
        if (p.endsWith(".ttf")) return "font/ttf";
        if (p.endsWith(".otf")) return "font/otf";
        if (p.endsWith(".woff")) return "font/woff";
        if (p.endsWith(".woff2")) return "font/woff2";
        if (p.endsWith(".mp3")) return "audio/mpeg";
        if (p.endsWith(".mp4")) return "video/mp4";
        if (p.endsWith(".webm")) return "video/webm";
        return "application/octet-stream";
    }

    private WebResourceResponse serve(String host, String path) {
        File base = host != null ? hosts.get(host.toLowerCase()) : null;
        if (base == null) return null;
        Map<String, String> headers = new HashMap<>();
        headers.put("Access-Control-Allow-Origin", "*");
        headers.put("Cache-Control", "no-cache");
        try {
            if (path == null || path.contains("..")) throw new Exception("bad path");
            File f = new File(base, path);   /* Uri.getPath(): already decoded */
            if (f.isDirectory()) f = new File(f, "index.html");
            String type = mime(f.getName());
            InputStream in = new FileInputStream(f);
            if (type.equals("text/html")) {
                ByteArrayOutputStream o = new ByteArrayOutputStream();
                byte[] b = new byte[65536];
                int n;
                while ((n = in.read(b)) > 0) o.write(b, 0, n);
                in.close();
                String html = new String(o.toByteArray(), StandardCharsets.UTF_8);
                String head = "<meta name=\"viewport\" content=\"width=" + cssW + "\">" + SHIM;
                int at = html.toLowerCase().indexOf("<head");
                if (at >= 0) at = html.indexOf('>', at) + 1;
                else at = 0;
                html = html.substring(0, at) + head + html.substring(at);
                in = new ByteArrayInputStream(html.getBytes(StandardCharsets.UTF_8));
            }
            return new WebResourceResponse(type, type.startsWith("text/") || type.endsWith("json") ? "UTF-8" : null,
                                           200, "OK", headers, in);
        } catch (Exception e) {
            return new WebResourceResponse("text/plain", "UTF-8", 404, "Not Found", headers,
                                           new ByteArrayInputStream(new byte[0]));
        }
    }
}
