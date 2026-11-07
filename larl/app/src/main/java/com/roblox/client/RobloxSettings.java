package com.roblox.client;

import android.content.Context;
import android.net.Uri;
import android.os.Build;

import com.larl.loader.BuildConfig;

import java.io.File;

/**
 * Minimal ABI-compatible settings bridge. Its package and native signatures are fixed by
 * libroblox.so; it is not a copy of the original Roblox frontend.
 */
public final class RobloxSettings {
    private static final String VERSION = "2.217.64833";
    private static boolean configured;

    private RobloxSettings() { }

    public static synchronized void configure(Context context) {
        if (configured) {
            return;
        }
        // These are the locations supplied by the original 2.217 frontend.  The native
        // networking bootstrap retains pointers to each setting, so initialise every field
        // it receives there rather than relying on its legacy null defaults.
        File cache = context.getCacheDir();
        File files = context.getFilesDir();
        nativeSetCacheDirectory(cache.getAbsolutePath());
        nativeSetFilesDirectory(files.getAbsolutePath());
        nativeSetExceptionReasonFilename(new File(cache, "exception-reason.txt").getAbsolutePath());
        nativeSetBaseUrl(BuildConfig.HEXAGON_BASE_URL);
        nativeSetHttpProxy("", 0L);
        if (!BuildConfig.HEXAGON_ROBLOSECURITY.isEmpty()) {
            nativeSetCookiesForDomain(cookieDomain(BuildConfig.HEXAGON_BASE_URL),
                    ".ROBLOSECURITY=" + BuildConfig.HEXAGON_ROBLOSECURITY);
        }
        nativeSetPlatformUserAgent("Mozilla/5.0 (Android " + Build.VERSION.RELEASE
                + "; " + Build.MODEL + ") AppleWebKit/537.36 (KHTML, like Gecko) "
                + "RobloxAndroidClient");
        nativeSetRobloxVersion(VERSION);
        nativeInitFastLog();
        configured = true;
    }

    public static String version() {
        return VERSION;
    }

    public static boolean hasHexagonSession() {
        return !BuildConfig.HEXAGON_ROBLOSECURITY.isEmpty();
    }

    public static String hexagonBaseUrl() {
        return BuildConfig.HEXAGON_BASE_URL;
    }

    public static String hexagonCookie() {
        return BuildConfig.HEXAGON_ROBLOSECURITY;
    }

    private static String cookieDomain(String baseUrl) {
        Uri uri = Uri.parse(baseUrl);
        String host = uri.getHost() == null ? baseUrl : uri.getHost();
        // Match WebkitCookieManagerProxy in the 2.217 frontend: share the session across
        // the www/api/mobile host aliases that the place launcher and join script use.
        return host.replace("m.", "").replace("www.", "")
                .replace("api.", "").replace("web.", "");
    }

    public static native void nativeSetBaseUrl(String value);
    public static native void nativeSetCacheDirectory(String value);
    public static native void nativeSetFilesDirectory(String value);
    public static native void nativeSetExceptionReasonFilename(String value);
    public static native void nativeInitFastLog();
    public static native void nativeInitBreakpad(boolean cleanPreviousDumps);
    public static native void nativeSetPlatformUserAgent(String value);
    public static native void nativeSetRobloxVersion(String value);
    public static native void nativeSetHttpProxy(String host, long port);
    public static native void nativeSetCookiesForDomain(String domain, String cookies);
    public static native void nativeSetDebugDisplay(boolean enabled);
    public static native boolean nativeEnableNDKProfiler(int enabled);
}
