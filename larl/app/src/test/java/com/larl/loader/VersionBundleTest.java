package com.larl.loader;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

public final class VersionBundleTest {
    @Test
    public void acceptsLowerAndUpperCaseSha256() {
        assertTrue(VersionBundle.isSha256("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));
        assertTrue(VersionBundle.isSha256("ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789"));
    }

    @Test
    public void rejectsMalformedSha256() {
        assertFalse(VersionBundle.isSha256("abc"));
        assertFalse(VersionBundle.isSha256(null));
    }
}
