package lh2;

import java.io.StringReader;

/** Vérification minimale sans framework de test : exit != 0 si un cas échoue. */
public final class SettingsCheck {
    public static void main(String[] args) throws Exception {
        LH2Settings d = LH2Settings.defaults();
        check(d.culling && d.maxChunks == 512 && !d.vsync && "gl21".equals(d.glProfile), "défauts");

        LH2Settings s = LH2Settings.parse(new StringReader(
                "culling=false\nmax_chunks=10\nvsync=true\ngl_profile=gl33\n"));
        check(!s.culling, "culling");
        check(s.maxChunks == 64, "max_chunks borné à 64");
        check(s.vsync, "vsync");
        check("gl33".equals(s.glProfile), "gl_profile gl33");

        LH2Settings bad = LH2Settings.parse(new StringReader(
                "max_chunks=abc\ngl_profile=vulkan\n"));
        check(bad.maxChunks == 512, "max_chunks invalide -> défaut");
        check("gl21".equals(bad.glProfile), "gl_profile inconnu -> gl21");

        System.out.println("SettingsCheck OK");
    }

    private static void check(boolean ok, String name) {
        if (!ok) {
            throw new AssertionError("échec : " + name);
        }
    }
}
