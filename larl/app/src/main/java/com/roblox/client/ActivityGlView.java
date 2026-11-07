package com.roblox.client;

import android.app.Activity;
import android.graphics.Color;
import android.os.Bundle;
import android.os.Build;
import android.util.Log;
import android.view.Gravity;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.widget.FrameLayout;
import android.widget.TextView;

import com.larl.loader.LegacyRuntime;
import com.larl.loader.LegacyAssets;
import com.larl.loader.HexagonJoinBridge;
import com.larl.loader.BuildConfig;

import java.io.File;
import java.io.IOException;

/**
 * Clean SurfaceView host for the legacy graphics ABI. This intentionally does not invoke
 * nativeStartGame: phase 1 validates native graphics initialization only.
 */
public final class ActivityGlView extends Activity implements SurfaceHolder.Callback {
    private static final String TAG = "LARL.ActivityGlView";
    private static volatile ActivityGlView singleton;

    private SurfaceView surfaceView;
    private InputListener inputListener;
    private TextView status;
    private WebView joinWebView;
    private boolean runtimeReady;
    private boolean graphicsStarted;
    private boolean gameStarted;
    private File assetPath;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        singleton = this;
        FrameLayout root = new FrameLayout(this);
        surfaceView = new SurfaceView(this);
        surfaceView.getHolder().addCallback(this);
        root.addView(surfaceView, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));

        status = new TextView(this);
        status.setBackgroundColor(0x99000000);
        status.setTextColor(Color.WHITE);
        status.setGravity(Gravity.CENTER);
        status.setPadding(32, 24, 32, 24);
        root.addView(status, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.WRAP_CONTENT, Gravity.TOP));
        setContentView(root);
        setStatus("Installing legacy 2.217 bundle…");

        try {
            LegacyRuntime.load2217(this);
            RobloxSettings.configure(this);
            runtimeReady = true;
            setStatus("Native library loaded; waiting for graphics surface…");
        } catch (IOException | RuntimeException error) {
            reportFailure("Native bootstrap failed", error);
        }
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        if (!runtimeReady || graphicsStarted) {
            return;
        }
        try {
            Surface surface = holder.getSurface();
            nativeStartUpGraphics(surface, getResources().getDisplayMetrics().density, Build.MODEL);
            graphicsStarted = true;
            setStatus("Native graphics surface initialized (2.217 diagnostic mode)");
            Log.i(TAG, "nativeStartUpGraphics completed");
            prepareAndLaunchHexagon();
        } catch (RuntimeException | UnsatisfiedLinkError error) {
            reportFailure("Native graphics startup failed", error);
        }
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        // The legacy startup API receives the Surface; no resize callback exists in this ABI.
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        if (!graphicsStarted) {
            return;
        }
        try {
            nativeShutDownGraphics(holder.getSurface());
        } catch (RuntimeException | UnsatisfiedLinkError error) {
            Log.e(TAG, "Native graphics shutdown failed", error);
        } finally {
            graphicsStarted = false;
        }
    }

    @Override
    protected void onDestroy() {
        if (singleton == this) {
            singleton = null;
        }
        super.onDestroy();
    }

    private void setStatus(String message) {
        Log.i(TAG, message);
        if (status != null) {
            status.setText(message);
        }
    }

    private void reportFailure(String prefix, Throwable error) {
        Log.e(TAG, prefix, error);
        setStatus(prefix + ": " + error.getMessage());
    }

    private void prepareAndLaunchHexagon() {
        if (!RobloxSettings.hasHexagonSession()) {
            setStatus("Native surface initialized. Set hexagonRoblosecurity in local.properties to launch place "
                    + BuildConfig.HEXAGON_PLACE_ID + ".");
            return;
        }
        setStatus("Installing legacy game assets…");
        new Thread(() -> {
            try {
                assetPath = LegacyAssets.install2217(this);
                runOnUiThread(this::openHexagonStartUrl);
            } catch (IOException error) {
                runOnUiThread(() -> reportFailure("Legacy asset install failed", error));
            }
        }, "legacy-asset-install").start();
    }

    private void openHexagonStartUrl() {
        if (isFinishing() || !graphicsStarted || gameStarted) {
            return;
        }
        setStatus("Opening Hexagon game start for place " + BuildConfig.HEXAGON_PLACE_ID + "…");
        joinWebView = new WebView(this);
        WebSettings settings = joinWebView.getSettings();
        settings.setJavaScriptEnabled(true);
        settings.setDomStorageEnabled(true);
        HexagonJoinBridge.configureCookie(RobloxSettings.hexagonBaseUrl(), RobloxSettings.hexagonCookie());
        HexagonJoinBridge joinBridge = new HexagonJoinBridge(new HexagonJoinBridge.Listener() {
            @Override
            public void onJoinRequested(HexagonJoinBridge.JoinRequest request) {
                startNativeGame(request);
            }

            @Override
            public void onJoinError(String message) {
                setStatus("Hexagon join failed: " + message);
            }
        });
        joinWebView.setWebViewClient(joinBridge);
        addContentView(joinWebView, new android.view.ViewGroup.LayoutParams(1, 1));
        String startUrl = RobloxSettings.hexagonBaseUrl() + "games/start?placeid=" + BuildConfig.HEXAGON_PLACE_ID;
        if (!joinBridge.startUrl(startUrl)) {
            joinWebView.loadUrl(startUrl);
        }
    }

    private void startNativeGame(HexagonJoinBridge.JoinRequest request) {
        if (gameStarted || assetPath == null || !graphicsStarted) {
            return;
        }
        try {
            Surface surface = surfaceView.getHolder().getSurface();
            nativeStartGame(surface, request.placeId, request.userId,
                    request.accessCode == null ? "" : request.accessCode,
                    request.gameId == null ? "" : request.gameId,
                    request.requestType, assetPath.getAbsolutePath(),
                    getResources().getDisplayMetrics().density, true,
                    Integer.toString(Build.VERSION.SDK_INT), Build.DEVICE, RobloxSettings.version(), Build.MODEL);
            // The native engine receives no Android MotionEvents by itself. The stock
            // frontend installs this listener immediately after nativeStartGame.
            inputListener = new InputListener(this, surfaceView);
            surfaceView.setOnTouchListener(inputListener);
            surfaceView.setFocusableInTouchMode(true);
            surfaceView.requestFocus();
            gameStarted = true;
            status.setVisibility(android.view.View.GONE);
            Log.i(TAG, "Native game launch requested for place " + request.placeId + ".");
        } catch (RuntimeException | UnsatisfiedLinkError error) {
            reportFailure("Native game launch failed", error);
        }
    }

    // Legacy Java callbacks that may be resolved by the native library. Unsupported features
    // become explicit no-ops in diagnostic mode instead of requiring the old frontend.
    public static int getSurfaceState() {
        ActivityGlView activity = singleton;
        return activity != null && activity.graphicsStarted ? 2 : 0;
    }

    /**
     * libroblox posts this callback whenever its worker threads need their queued
     * Android-side messages handled.  The stock frontend puts the native drain at the
     * front of the UI queue; leaving it as a no-op stalls the post-authentication join.
     */
    public static void postAppEvent() {
        dispatchNativeMessages();
    }
    public static void hideKeyboard() { }
    public static void listenToMotionEvents(String motionType) { }
    public static void promptNativePurchase(long requestId, String productId, String payload) { }
    public static void releaseFocus(long textBoxId) { nativeReleaseFocus(textBoxId); }
    public static void removeGoogleAd() { }
    public static void showGoogleAd(String adUnit) { }
    public static void showAdColonyAd() { }
    public static void showKeyboard(long textBoxId, String initialText) { }
    public static void inGamePurchaseFinished(boolean success, long requestId, String receipt) {
        nativeInGamePurchaseFinished(success, requestId, receipt);
    }
    public static void sendAppEvent(boolean foreground) {
        dispatchNativeMessages();
    }
    public static void exitGame() { finishFromNative(); }
    public static void exitGameSilent() { finishFromNative(); }
    public static void exitGameWithError(String message) {
        ActivityGlView activity = singleton;
        if (activity != null) {
            activity.runOnUiThread(() -> {
                activity.setStatus("Native exit: " + message);
                activity.finish();
            });
        }
    }

    private static void finishFromNative() {
        ActivityGlView activity = singleton;
        if (activity != null) {
            activity.runOnUiThread(activity::finish);
        }
    }

    private static void dispatchNativeMessages() {
        ActivityGlView activity = singleton;
        if (activity == null || !activity.graphicsStarted || activity.isFinishing()) {
            return;
        }
        activity.runOnUiThread(() -> {
            if (activity.graphicsStarted && !activity.isFinishing()) {
                nativeCallMessagesFromMainThread();
            }
        });
    }

    public static native void nativeCallMessagesFromMainThread();
    public static native void nativeHandleBackPressed();
    public static native void nativeInGamePurchaseFinished(boolean success, long requestId, String receipt);
    public static native void nativeOnLowMemory();
    public static native void nativePassInput(int action, int x, int y, int id, int metaState, int source);
    public static native void nativePassText(long textBoxId, String value, boolean submitted, int cursorPosition);
    public static native void nativeReleaseFocus(long textBoxId);
    public static native void nativeShutDownGraphics(Surface surface);
    public static native void nativeStartGame(Surface surface, int width, int height, String url,
                                              String user, int userId, String ticket, float density,
                                              boolean touch, String sdk, String device, String version,
                                              String model);
    public static native void nativeStartUpGraphics(Surface surface, float density, String model);
    public static native void nativeStopGame();
    public static native void nativeVideoAdFinished(boolean completed);
    public static native void nativePassPinchGesture(int state, float scale, float velocity,
                                                      int x, int y, int x2, int y2);
    public static native void nativePassTapGesture(int state, int x, int y);
}
