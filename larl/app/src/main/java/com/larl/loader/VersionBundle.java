package com.larl.loader;

import java.util.Locale;

/** Immutable description of one packaged legacy native bundle. */
public final class VersionBundle {
    public final String version;
    public final String abi;
    public final String fileName;
    public final String sha256;

    VersionBundle(String version, String abi, String fileName, String sha256) {
        this.version = required(version, "version");
        this.abi = required(abi, "abi");
        this.fileName = required(fileName, "file");
        this.sha256 = required(sha256, "sha256").toLowerCase(Locale.ROOT);
        if (!isSha256(this.sha256)) {
            throw new IllegalArgumentException("Bundle digest is not SHA-256");
        }
        if (!"libroblox.so".equals(fileName)) {
            throw new IllegalArgumentException("Unexpected native library name: " + fileName);
        }
    }

    static boolean isSha256(String value) {
        return value != null && value.matches("[0-9a-fA-F]{64}");
    }

    private static String required(String value, String field) {
        if (value == null || value.trim().isEmpty()) {
            throw new IllegalArgumentException("Missing bundle " + field);
        }
        return value;
    }
}
