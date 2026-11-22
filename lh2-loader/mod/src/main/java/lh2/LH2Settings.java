package lh2;

import java.io.IOException;
import java.io.Reader;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Locale;
import java.util.Properties;

/** Réglages LH2 Loader, lus depuis config/lh2loader.properties. Valeurs invalides -> défauts. */
public final class LH2Settings {

    /** Mode de culling des chunks : off, normal (champ de vision + distance), aggressive. */
    public enum Culling { OFF, NORMAL, AGGRESSIVE }

    /** Niveau de détail des particules. */
    public enum Particles { OFF, REDUCED, FULL }

    /** Profil OpenGL demandé : gl21 (compatibilité, défaut) ou gl33 (optionnel). */
    public enum GLProfile { GL21, GL33 }

    public final Culling culling;
    public final int renderDistance;
    public final int maxChunks;
    public final int textureCacheMb;
    public final boolean vsync;
    public final int fpsLimit;
    public final GLProfile glProfile;
    public final boolean fastGraphics;
    public final boolean smoothLighting;
    public final boolean clouds;
    public final Particles particles;
    public final int entityDistance;
    public final boolean periodicGc;
    public final boolean reduceAllocations;

    public LH2Settings(Culling culling, int renderDistance, int maxChunks, int textureCacheMb,
                       boolean vsync, int fpsLimit, GLProfile glProfile, boolean fastGraphics,
                       boolean smoothLighting, boolean clouds, Particles particles,
                       int entityDistance, boolean periodicGc, boolean reduceAllocations) {
        this.culling = culling;
        this.renderDistance = clamp(renderDistance, 2, 32);
        this.maxChunks = clamp(maxChunks, 64, 4096);
        this.textureCacheMb = clamp(textureCacheMb, 16, 1024);
        this.vsync = vsync;
        this.fpsLimit = clamp(fpsLimit, 0, 1000); // 0 = illimité
        this.glProfile = glProfile;
        this.fastGraphics = fastGraphics;
        this.smoothLighting = smoothLighting;
        this.clouds = clouds;
        this.particles = particles;
        this.entityDistance = clamp(entityDistance, 16, 256);
        this.periodicGc = periodicGc;
        this.reduceAllocations = reduceAllocations;
    }

    public static LH2Settings defaults() {
        return new LH2Settings(Culling.NORMAL, 8, 512, 128, false, 0, GLProfile.GL21,
                true, false, false, Particles.REDUCED, 96, false, true);
    }

    public static LH2Settings parse(Reader reader) throws IOException {
        Properties p = new Properties();
        p.load(reader);
        LH2Settings d = defaults();
        return new LH2Settings(
                enumOf(p.getProperty("culling"), Culling.class, d.culling),
                intOf(p.getProperty("render_distance"), d.renderDistance),
                intOf(p.getProperty("max_chunks"), d.maxChunks),
                intOf(p.getProperty("texture_cache_mb"), d.textureCacheMb),
                boolOf(p.getProperty("vsync"), d.vsync),
                intOf(p.getProperty("fps_limit"), d.fpsLimit),
                enumOf(p.getProperty("gl_profile"), GLProfile.class, d.glProfile),
                boolOf(p.getProperty("fast_graphics"), d.fastGraphics),
                boolOf(p.getProperty("smooth_lighting"), d.smoothLighting),
                boolOf(p.getProperty("clouds"), d.clouds),
                enumOf(p.getProperty("particles"), Particles.class, d.particles),
                intOf(p.getProperty("entity_distance"), d.entityDistance),
                boolOf(p.getProperty("periodic_gc"), d.periodicGc),
                boolOf(p.getProperty("reduce_allocations"), d.reduceAllocations));
    }

    public static LH2Settings load(Path file) throws IOException {
        if (!Files.exists(file)) {
            return defaults();
        }
        try (Reader r = Files.newBufferedReader(file, StandardCharsets.UTF_8)) {
            return parse(r);
        }
    }

    /** Contenu par défaut du fichier, avec la description de chaque option. */
    public static String defaultFileContent() {
        return "# LH2 Loader : réglages\n"
                + "# culling          : off | normal | aggressive (cache les chunks hors vue)\n"
                + "# render_distance  : 2-32 chunks\n"
                + "# max_chunks       : 64-4096 chunks gardés en mémoire\n"
                + "# texture_cache_mb : 16-1024 Mo pour l'atlas de textures\n"
                + "# vsync            : true | false\n"
                + "# fps_limit        : 0 (illimité) ou 1-1000\n"
                + "# gl_profile       : gl21 (compatibilité) | gl33 (optionnel, non encore actif)\n"
                + "# fast_graphics    : true | false (désactive les effets coûteux)\n"
                + "# smooth_lighting  : true | false\n"
                + "# clouds           : true | false\n"
                + "# particles        : off | reduced | full\n"
                + "# entity_distance  : 16-256 blocs\n"
                + "# periodic_gc      : true | false (GC forcé périodiquement ; utile seulement si la mémoire monte)\n"
                + "# reduce_allocations : true | false (réutilise les objets dans les boucles de rendu)\n"
                + "culling=normal\n"
                + "render_distance=8\n"
                + "max_chunks=512\n"
                + "texture_cache_mb=128\n"
                + "vsync=false\n"
                + "fps_limit=0\n"
                + "gl_profile=gl21\n"
                + "fast_graphics=true\n"
                + "smooth_lighting=false\n"
                + "clouds=false\n"
                + "particles=reduced\n"
                + "entity_distance=96\n"
                + "periodic_gc=false\n"
                + "reduce_allocations=true\n";
    }

    private static int clamp(int v, int lo, int hi) {
        return Math.max(lo, Math.min(hi, v));
    }

    private static int intOf(String s, int fallback) {
        if (s == null) return fallback;
        try {
            return Integer.parseInt(s.trim());
        } catch (NumberFormatException e) {
            return fallback;
        }
    }

    private static boolean boolOf(String s, boolean fallback) {
        if (s == null) return fallback;
        String v = s.trim().toLowerCase(Locale.ROOT);
        if (v.equals("true")) return true;
        if (v.equals("false")) return false;
        return fallback;
    }

    private static <E extends Enum<E>> E enumOf(String s, Class<E> type, E fallback) {
        if (s == null) return fallback;
        try {
            return Enum.valueOf(type, s.trim().toUpperCase(Locale.ROOT));
        } catch (IllegalArgumentException e) {
            return fallback;
        }
    }

    @Override
    public String toString() {
        return "LH2Settings{culling=" + culling + ", renderDistance=" + renderDistance
                + ", maxChunks=" + maxChunks + ", textureCacheMb=" + textureCacheMb
                + ", vsync=" + vsync + ", fpsLimit=" + fpsLimit + ", glProfile=" + glProfile
                + ", fastGraphics=" + fastGraphics + ", smoothLighting=" + smoothLighting
                + ", clouds=" + clouds + ", particles=" + particles
                + ", entityDistance=" + entityDistance + ", periodicGc=" + periodicGc
                + ", reduceAllocations=" + reduceAllocations + "}";
    }
}
