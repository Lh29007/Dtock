package lh2;

import java.io.IOException;
import java.io.InputStream;
import java.io.Reader;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Properties;

/** Réglages LH2 Loader, lus depuis config/lh2loader.properties. */
public final class LH2Settings {
    public final boolean culling;
    public final int maxChunks;
    public final boolean vsync;
    public final String glProfile;

    public LH2Settings(boolean culling, int maxChunks, boolean vsync, String glProfile) {
        this.culling = culling;
        this.maxChunks = Math.max(64, maxChunks);
        this.vsync = vsync;
        this.glProfile = "gl33".equals(glProfile) ? "gl33" : "gl21";
    }

    public static LH2Settings defaults() {
        return new LH2Settings(true, 512, false, "gl21");
    }

    public static LH2Settings parse(Reader reader) throws IOException {
        Properties p = new Properties();
        p.load(reader);
        return new LH2Settings(
                Boolean.parseBoolean(p.getProperty("culling", "true")),
                parseInt(p.getProperty("max_chunks"), 512),
                Boolean.parseBoolean(p.getProperty("vsync", "false")),
                p.getProperty("gl_profile", "gl21"));
    }

    public static LH2Settings load(Path file) throws IOException {
        if (!Files.exists(file)) {
            return defaults();
        }
        try (Reader r = Files.newBufferedReader(file, StandardCharsets.UTF_8)) {
            return parse(r);
        }
    }

    private static int parseInt(String value, int fallback) {
        try {
            return Integer.parseInt(value.trim());
        } catch (RuntimeException e) {
            return fallback;
        }
    }

    @Override
    public String toString() {
        return "LH2Settings{culling=" + culling + ", maxChunks=" + maxChunks
                + ", vsync=" + vsync + ", glProfile=" + glProfile + "}";
    }
}
