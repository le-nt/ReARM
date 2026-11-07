package com.larl.loader;

import android.content.Context;
import android.content.res.AssetManager;
import android.os.Build;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.Properties;

/** Installs a packaged bundle into process-private storage without trusting stale copies. */
public final class VersionInstaller {
    public static final String VERSION_2217 = "2.217";
    private static final String ABI = "armeabi-v7a";

    private VersionInstaller() { }

    public static File install2217(Context context) throws IOException {
        requireCompatibleAbi();
        AssetManager assets = context.getAssets();
        String base = "versions/" + VERSION_2217 + "/" + ABI + "/";
        VersionBundle bundle = readBundle(assets, base);
        File versionDir = new File(new File(context.getCodeCacheDir(), "legacy"), bundle.version + "/" + bundle.abi);
        if (!versionDir.exists() && !versionDir.mkdirs()) {
            throw new IOException("Unable to create version directory: " + versionDir);
        }
        File destination = new File(versionDir, bundle.fileName);
        if (destination.isFile() && bundle.sha256.equals(sha256(destination))) {
            return destination;
        }

        File temporary = new File(versionDir, bundle.fileName + ".part");
        if (temporary.exists() && !temporary.delete()) {
            throw new IOException("Unable to replace temporary bundle: " + temporary);
        }
        try (InputStream input = assets.open(base + bundle.fileName);
             FileOutputStream output = new FileOutputStream(temporary)) {
            byte[] buffer = new byte[32 * 1024];
            for (int count; (count = input.read(buffer)) != -1; ) {
                output.write(buffer, 0, count);
            }
            output.getFD().sync();
        }
        if (!bundle.sha256.equals(sha256(temporary))) {
            temporary.delete();
            throw new IOException("Packaged legacy library failed SHA-256 verification");
        }
        if (destination.exists() && !destination.delete()) {
            throw new IOException("Unable to replace installed bundle: " + destination);
        }
        if (!temporary.renameTo(destination)) {
            throw new IOException("Unable to install legacy bundle atomically");
        }
        return destination;
    }

    static void requireCompatibleAbi() throws IOException {
        for (String supported : Build.SUPPORTED_ABIS) {
            if (ABI.equals(supported) || "armeabi".equals(supported)) {
                return;
            }
        }
        StringBuilder supportedAbis = new StringBuilder();
        for (String supported : Build.SUPPORTED_ABIS) {
            if (supportedAbis.length() > 0) {
                supportedAbis.append(", ");
            }
            supportedAbis.append(supported);
        }
        throw new IOException("2.217 requires armeabi-v7a; device supports " + supportedAbis);
    }

    private static VersionBundle readBundle(AssetManager assets, String base) throws IOException {
        Properties properties = new Properties();
        try (InputStream input = assets.open(base + "bundle.properties")) {
            properties.load(input);
        }
        try {
            return new VersionBundle(
                    properties.getProperty("version"),
                    properties.getProperty("abi"),
                    properties.getProperty("file"),
                    properties.getProperty("sha256"));
        } catch (IllegalArgumentException error) {
            throw new IOException("Invalid packaged bundle metadata", error);
        }
    }

    static String sha256(File file) throws IOException {
        final MessageDigest digest;
        try {
            digest = MessageDigest.getInstance("SHA-256");
        } catch (NoSuchAlgorithmException error) {
            throw new AssertionError(error);
        }
        try (InputStream input = new FileInputStream(file)) {
            byte[] buffer = new byte[32 * 1024];
            for (int count; (count = input.read(buffer)) != -1; ) {
                digest.update(buffer, 0, count);
            }
        }
        StringBuilder hex = new StringBuilder(64);
        for (byte value : digest.digest()) {
            hex.append(String.format("%02x", value & 0xff));
        }
        return hex.toString();
    }
}
