package com.larl.loader;

import android.content.Context;
import android.content.res.AssetManager;

import java.io.BufferedOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/** Installs the 2.217 OBB content tree used by nativeStartGame. */
public final class LegacyAssets {
    private static final String ASSET = "versions/2.217/armeabi-v7a/main.1.com.roblox.client.obb";

    private LegacyAssets() { }

    public static synchronized File install2217(Context context) throws IOException {
        // libroblox 2.217 was launched with Context.getDir("assets", 0).  Keep that
        // exact convention because parts of its native bootstrap derive companion paths
        // from the supplied content directory.
        File base = context.getDir("assets", Context.MODE_PRIVATE);
        File content = new File(base, "content");
        File marker = new File(base, ".ready");
        if (marker.isFile() && content.isDirectory()) {
            return content;
        }
        deleteRecursively(base);
        if (!base.mkdirs()) {
            throw new IOException("Unable to create legacy asset directory: " + base);
        }
        String canonicalBase = base.getCanonicalPath() + File.separator;
        try (InputStream input = context.getAssets().open(ASSET);
             ZipInputStream zip = new ZipInputStream(input)) {
            byte[] buffer = new byte[32 * 1024];
            for (ZipEntry entry; (entry = zip.getNextEntry()) != null; ) {
                File output = new File(base, entry.getName());
                if (!output.getCanonicalPath().startsWith(canonicalBase)) {
                    throw new IOException("Refusing unsafe OBB entry: " + entry.getName());
                }
                if (entry.isDirectory()) {
                    if (!output.mkdirs() && !output.isDirectory()) {
                        throw new IOException("Unable to create asset directory: " + output);
                    }
                    continue;
                }
                File parent = output.getParentFile();
                if (parent != null && !parent.exists() && !parent.mkdirs()) {
                    throw new IOException("Unable to create asset parent: " + parent);
                }
                try (BufferedOutputStream stream = new BufferedOutputStream(new FileOutputStream(output))) {
                    for (int count; (count = zip.read(buffer)) != -1; ) {
                        stream.write(buffer, 0, count);
                    }
                }
            }
        } catch (IOException error) {
            deleteRecursively(base);
            throw error;
        }
        if (!content.isDirectory()) {
            deleteRecursively(base);
            throw new IOException("Legacy OBB did not contain content/");
        }
        if (!marker.createNewFile()) {
            throw new IOException("Unable to mark legacy assets ready");
        }
        return content;
    }

    private static void deleteRecursively(File file) {
        if (file.isDirectory()) {
            File[] children = file.listFiles();
            if (children != null) {
                for (File child : children) {
                    deleteRecursively(child);
                }
            }
        }
        if (file.exists()) {
            file.delete();
        }
    }
}
