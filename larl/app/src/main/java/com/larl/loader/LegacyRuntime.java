package com.larl.loader;

import android.content.Context;

import java.io.File;
import java.io.IOException;

/** Loads the selected native bundle once per game process. */
public final class LegacyRuntime {
    private static boolean loaded;

    private LegacyRuntime() { }

    public static synchronized File load2217(Context context) throws IOException {
        if (loaded) {
            return new File(new File(context.getCodeCacheDir(), "legacy/2.217/armeabi-v7a"), "libroblox.so");
        }
        File library = VersionInstaller.install2217(context);
        try {
            System.loadLibrary("fmodex");
            System.load(library.getAbsolutePath());
        } catch (UnsatisfiedLinkError error) {
            throw new IOException("Unable to load legacy native dependencies: " + error.getMessage(), error);
        }
        loaded = true;
        return library;
    }
}
