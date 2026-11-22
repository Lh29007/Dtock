package lh2.installer;

import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/** Vérifications sans framework pour l'installeur. */
public final class InstallerCheck {
    public static void main(String[] args) throws Exception {
        Path tmp = Files.createTempDirectory("lh2check");
        Path home = tmp.resolve("home");
        Path mc = home.resolve(".minecraft");

        // Pas de 1.8.9 -> aucun dossier détecté
        Map<String, String> env = new HashMap<String, String>();
        List<Path> cands = Installer.candidateDirs(env, home.toString(), "Linux");
        check(Installer.findVanilla189(cands) == null, "rien détecté sans 1.8.9");
        check(cands.contains(mc), "~/.minecraft dans les candidats");

        // Installation vanilla simulée
        Path v = mc.resolve("versions").resolve("1.8.9");
        Files.createDirectories(v);
        Files.write(v.resolve("1.8.9.json"), "{}".getBytes("UTF-8"));
        Files.write(v.resolve("1.8.9.jar"), new byte[] {1, 2, 3});
        check(Installer.findVanilla189(cands).equals(mc), "1.8.9 détecté automatiquement");

        // Windows : APPDATA
        Map<String, String> winEnv = new HashMap<String, String>();
        winEnv.put("APPDATA", tmp.resolve("appdata").toString());
        List<Path> win = Installer.candidateDirs(winEnv, home.toString(), "Windows 10");
        check(win.get(0).equals(tmp.resolve("appdata").resolve(".minecraft")), "APPDATA prioritaire sous Windows");

        // Installation : profil, JAR, réglages
        Path source = tmp.resolve("lh2-loader-1.0.0.jar");
        Files.write(source, new byte[] {9, 9});
        Installer.install(mc, source);
        check(Files.isRegularFile(mc.resolve("versions/1.8.9-LH2/1.8.9-LH2.json")), "profil écrit");
        String json = new String(Files.readAllBytes(mc.resolve("versions/1.8.9-LH2/1.8.9-LH2.json")), "UTF-8");
        check(json.contains("\"inheritsFrom\": \"1.8.9\""), "profil hérite de 1.8.9");
        check(Files.isRegularFile(mc.resolve("libraries").resolve(Installer.LIB_PATH)), "JAR copié");
        check(Files.isRegularFile(mc.resolve("config/lh2loader.properties")), "réglages créés");

        // Réinstallation : les réglages existants ne sont pas écrasés
        Files.write(mc.resolve("config/lh2loader.properties"), "culling=off\n".getBytes("UTF-8"));
        Installer.install(mc, source);
        String kept = new String(Files.readAllBytes(mc.resolve("config/lh2loader.properties")), "UTF-8");
        check(kept.equals("culling=off\n"), "réglages existants conservés");

        // Refus sans 1.8.9
        boolean refused = false;
        try {
            Installer.install(home.resolve("vide"), source);
        } catch (java.io.IOException e) {
            refused = true;
        }
        check(refused, "refus sans 1.8.9");

        System.out.println("InstallerCheck OK");
    }

    private static void check(boolean ok, String name) {
        if (!ok) throw new AssertionError("échec : " + name);
    }
}
