package lh2.installer;

import java.io.File;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Collections;
import java.util.List;

/**
 * Point d'entrée de l'installeur.
 *   java -jar lh2-installer.jar            -> détection automatique
 *   java -jar lh2-installer.jar <dossier>  -> installe dans ce dossier .minecraft
 */
public final class Main {
    public static void main(String[] args) {
        int code = run(args, System.getenv(), System.getProperty("user.home"),
                System.getProperty("os.name"));
        if (isWindows() && System.console() == null) {
            // Double-clic sous Windows : garder la fenêtre ouverte.
            System.out.println("Appuyez sur Entrée pour fermer...");
            try { System.in.read(); } catch (Exception ignored) { }
        }
        System.exit(code);
    }

    static int run(String[] args, java.util.Map<String, String> env, String home, String os) {
        try {
            Path mcDir;
            if (args.length > 0) {
                mcDir = Paths.get(args[0]);
            } else {
                List<Path> candidates = Installer.candidateDirs(env, home, os);
                mcDir = Installer.findVanilla189(candidates);
                if (mcDir == null) {
                    System.out.println("Minecraft 1.8.9 introuvable. Dossiers recherchés :");
                    for (Path p : candidates) System.out.println("  - " + p);
                    System.out.println("Lancez Minecraft 1.8.9 une fois, ou passez le dossier en argument.");
                    return 1;
                }
            }
            if (!Installer.hasVanilla189(mcDir)) {
                System.out.println("Minecraft 1.8.9 absent de : " + mcDir);
                return 1;
            }
            Path self = ownJar();
            Installer.install(mcDir, self);
            System.out.println("LH2 Loader installé dans : " + mcDir);
            System.out.println("Profil à sélectionner dans le launcher : " + Installer.PROFILE_ID);
            return 0;
        } catch (Exception e) {
            System.out.println("Erreur : " + e.getMessage());
            return 2;
        }
    }

    /** Chemin du JAR qui contient cet installeur (il embarque la bibliothèque LH2). */
    static Path ownJar() throws Exception {
        File f = new File(Main.class.getProtectionDomain().getCodeSource().getLocation().toURI());
        return f.toPath();
    }

    private static boolean isWindows() {
        return System.getProperty("os.name", "").toLowerCase().contains("win");
    }
}
