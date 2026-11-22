package lh2;

/**
 * Logique de culling des chunks, indépendante du client Minecraft.
 * Un chunk est visible s'il est dans la distance de rendu et dans le cône de vue
 * (avec une marge), selon le mode choisi.
 */
public final class ChunkCulling {
    private ChunkCulling() {}

    /** Largeur d'un chunk en blocs. */
    public static final double CHUNK_SIZE = 16.0;

    /**
     * @param chunkX coordonnée X du chunk (en chunks)
     * @param chunkZ coordonnée Z du chunk (en chunks)
     * @param camX   position caméra X en blocs
     * @param camZ   position caméra Z en blocs
     * @param yawRad direction de la caméra en radians (0 = +Z)
     * @param fovRad champ de vision horizontal en radians
     * @param renderDistanceChunks distance de rendu en chunks
     * @param mode   mode de culling
     */
    public static boolean isVisible(int chunkX, int chunkZ, double camX, double camZ,
                                    double yawRad, double fovRad, int renderDistanceChunks,
                                    LH2Settings.Culling mode) {
        if (mode == LH2Settings.Culling.OFF) {
            return true;
        }
        // Centre du chunk
        double cx = chunkX * CHUNK_SIZE + CHUNK_SIZE / 2.0 - camX;
        double cz = chunkZ * CHUNK_SIZE + CHUNK_SIZE / 2.0 - camZ;
        double dist = Math.sqrt(cx * cx + cz * cz);

        double maxDist = (renderDistanceChunks + 1) * CHUNK_SIZE;
        if (mode == LH2Settings.Culling.AGGRESSIVE) {
            maxDist = renderDistanceChunks * CHUNK_SIZE;
        }
        if (dist > maxDist) {
            return false;
        }
        // Les chunks très proches (autour du joueur) restent toujours visibles.
        if (dist <= CHUNK_SIZE * 1.5) {
            return true;
        }
        double forwardX = Math.sin(yawRad);
        double forwardZ = Math.cos(yawRad);
        double cos = (cx * forwardX + cz * forwardZ) / dist;
        // Marge sur le cône : en mode aggressive, le cône est plus étroit.
        double margin = mode == LH2Settings.Culling.AGGRESSIVE ? 0.0 : 0.35;
        double halfCone = fovRad / 2.0 + margin;
        return Math.acos(Math.max(-1.0, Math.min(1.0, cos))) <= halfCone;
    }
}
