package lh2.installer;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;

/**
 * Installeur LH2 Loader (Java, Java 8+). Détecte les dossiers .minecraft,
 * vérifie que Minecraft 1.8.9 est installé et crée le profil 1.8.9-LH2.
 */
public final class Installer {

    public static final String PROFILE_ID = "1.8.9-LH2";
    public static final String PARENT_ID = "1.8.9";
    public static final String MAIN_CLASS = "net.minecraft.client.main.Main";
    public static final String LIB_NAME = "lh2:lh2-loader:1.0.0";
    public static final String LIB_PATH = "lh2/lh2-loader/1.0.0/lh2-loader-1.0.0.jar";

    private Installer() {}

    /** Dossiers .minecraft probables, selon l'OS et l'environnement (Windows, macOS, Linux, Android). */
    public static List<Path> candidateDirs(Map<String, String> env, String userHome, String osName) {
        List<Path> out = new ArrayList<Path>();
        String os = osName == null ? "" : osName.toLowerCase();
        if (os.contains("win")) {
            String appData = env.get("APPDATA");
            if (appData != null) out.add(Paths.get(appData, ".minecraft"));
        } else if (os.contains("mac")) {
            out.add(Paths.get(userHome, "Library", "Application Support", "minecraft"));
        }
        out.add(Paths.get(userHome, ".minecraft"));
        // Android : PojavLauncher / Amethyst (chemins usuels, à confirmer sur l'appareil)
        out.add(Paths.get("/storage/emulated/0/games/PojavLauncher/.minecraft"));
        out.add(Paths.get("/storage/emulated/0/games/AmethystLauncher/.minecraft"));
        out.add(Paths.get("/storage/emulated/0/games/com.mojang"));
        return out;
    }

    /** Renvoie le premier dossier qui contient une installation vanilla 1.8.9 complète. */
    public static Path findVanilla189(List<Path> dirs) {
        for (Path dir : dirs) {
            if (hasVanilla189(dir)) return dir;
        }
        return null;
    }

    public static boolean hasVanilla189(Path mcDir) {
        Path version = mcDir.resolve("versions").resolve(PARENT_ID);
        return Files.isRegularFile(version.resolve(PARENT_ID + ".json"))
                && Files.isRegularFile(version.resolve(PARENT_ID + ".jar"));
    }

    /** JSON du profil LH2, hérité de 1.8.9. */
    public static String profileJson() {
        return "{\n"
                + "  \"id\": \"" + PROFILE_ID + "\",\n"
                + "  \"inheritsFrom\": \"" + PARENT_ID + "\",\n"
                + "  \"type\": \"release\",\n"
                + "  \"mainClass\": \"" + MAIN_CLASS + "\",\n"
                + "  \"libraries\": [\n"
                + "    { \"name\": \"" + LIB_NAME + "\" }\n"
                + "  ]\n"
                + "}\n";
    }

    /**
     * Installe LH2 dans mcDir. sourceJar est le JAR LH2 à copier dans libraries/.
     * Ne remplace pas un fichier de réglages existant.
     */
    public static void install(Path mcDir, Path sourceJar) throws IOException {
        if (!Files.isRegularFile(sourceJar)) {
            throw new IOException("JAR source introuvable : " + sourceJar);
        }
        if (!hasVanilla189(mcDir)) {
            throw new IOException("Minecraft " + PARENT_ID + " n'est pas installé dans " + mcDir
                    + " : lancez-le une fois avec votre launcher avant d'installer LH2.");
        }
        Path profileDir = mcDir.resolve("versions").resolve(PROFILE_ID);
        Files.createDirectories(profileDir);
        Files.write(profileDir.resolve(PROFILE_ID + ".json"),
                profileJson().getBytes(StandardCharsets.UTF_8));

        Path libTarget = mcDir.resolve("libraries").resolve(LIB_PATH);
        Files.createDirectories(libTarget.getParent());
        Files.copy(sourceJar, libTarget, java.nio.file.StandardCopyOption.REPLACE_EXISTING);

        Path conf = mcDir.resolve("config").resolve("lh2loader.properties");
        if (!Files.exists(conf)) {
            Files.createDirectories(conf.getParent());
            Files.write(conf, lh2lib().getBytes(StandardCharsets.UTF_8));
        }
    }

    /** Contenu par défaut des réglages, identique à LH2Settings.defaultFileContent(). */
    static String lh2lib() {
        return lh2.LH2Settings.defaultFileContent();
    }

    /** Copie le contenu d'un flux vers un fichier (utilitaire pour les tests). */
    static void copy(InputStream in, OutputStream out) throws IOException {
        byte[] buf = new byte[8192];
        int n;
        while ((n = in.read(buf)) != -1) out.write(buf, 0, n);
    }
}
