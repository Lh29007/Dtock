package lh2;

import java.io.StringReader;

/** Vérification sans framework : lève une erreur si un cas échoue. */
public final class SettingsCheck {
    public static void main(String[] args) throws Exception {
        LH2Settings d = LH2Settings.defaults();
        check(d.culling == LH2Settings.Culling.NORMAL, "culling par défaut");
        check(d.maxChunks == 512, "max_chunks par défaut");
        check(d.glProfile == LH2Settings.GLProfile.GL21, "gl21 par défaut");

        // Parsing complet avec valeurs valides
        LH2Settings s = LH2Settings.parse(new StringReader(
                "culling=aggressive\nrender_distance=12\nmax_chunks=1000\ntexture_cache_mb=256\n"
                + "vsync=true\nfps_limit=120\ngl_profile=gl33\nfast_graphics=false\n"
                + "smooth_lighting=true\nclouds=true\nparticles=full\nentity_distance=128\n"
                + "periodic_gc=true\nreduce_allocations=false\n"));
        check(s.culling == LH2Settings.Culling.AGGRESSIVE, "culling aggressive");
        check(s.renderDistance == 12, "render_distance");
        check(s.maxChunks == 1000, "max_chunks");
        check(s.textureCacheMb == 256, "texture_cache_mb");
        check(s.vsync && s.fpsLimit == 120, "vsync/fps_limit");
        check(s.glProfile == LH2Settings.GLProfile.GL33, "gl33");
        check(!s.fastGraphics && s.smoothLighting && s.clouds, "booléens graphiques");
        check(s.particles == LH2Settings.Particles.FULL, "particules");
        check(s.entityDistance == 128 && s.periodicGc && !s.reduceAllocations, "entités/GC/alloc");

        // Bornes
        LH2Settings b = LH2Settings.parse(new StringReader(
                "render_distance=999\nmax_chunks=1\ntexture_cache_mb=0\nfps_limit=-5\n"));
        check(b.renderDistance == 32, "render_distance borné haut");
        check(b.maxChunks == 64, "max_chunks borné bas");
        check(b.textureCacheMb == 16, "texture_cache_mb borné bas");
        check(b.fpsLimit == 0, "fps_limit borné bas");

        // Valeurs invalides -> défauts
        LH2Settings bad = LH2Settings.parse(new StringReader(
                "culling=turbo\ngl_profile=vulkan\nvsync=yes\nmax_chunks=abc\nparticles=max\n"));
        check(bad.culling == LH2Settings.Culling.NORMAL, "culling inconnu");
        check(bad.glProfile == LH2Settings.GLProfile.GL21, "gl inconnu");
        check(!bad.vsync, "vsync invalide");
        check(bad.maxChunks == 512, "max_chunks invalide");
        check(bad.particles == LH2Settings.Particles.REDUCED, "particules inconnues");

        // Le fichier par défaut doit se relire à l'identique
        LH2Settings round = LH2Settings.parse(new StringReader(LH2Settings.defaultFileContent()));
        check(round.toString().equals(d.toString()), "fichier par défaut = défauts");

        // Culling : chunk devant, chunk derrière, chunk trop loin, mode OFF
        check(ChunkCulling.isVisible(0, 3, 0, 0, 0, Math.toRadians(70), 8,
                LH2Settings.Culling.NORMAL), "chunk devant visible");
        check(!ChunkCulling.isVisible(0, -3, 0, 0, 0, Math.toRadians(70), 8,
                LH2Settings.Culling.AGGRESSIVE), "chunk derrière masqué (aggressive)");
        check(!ChunkCulling.isVisible(40, 0, 0, 0, 0, Math.toRadians(70), 8,
                LH2Settings.Culling.NORMAL), "chunk trop loin masqué");
        check(ChunkCulling.isVisible(0, -3, 0, 0, 0, Math.toRadians(70), 8,
                LH2Settings.Culling.OFF), "OFF garde tout");
        check(ChunkCulling.isVisible(0, -1, 0, 0, 0, Math.toRadians(70), 8,
                LH2Settings.Culling.AGGRESSIVE), "chunk proche toujours visible");

        System.out.println("SettingsCheck OK");
    }

    private static void check(boolean ok, String name) {
        if (!ok) {
            throw new AssertionError("échec : " + name);
        }
    }
}
