// lh2installer installe le profil LH2 Loader pour Minecraft 1.8.9.
//
// Usage :
//
//	lh2installer -dir <dossier .minecraft> -jar <lh2-loader.jar>
//
// Le profil est écrit dans versions/1.8.9-LH2/ et hérite de 1.8.9, donc
// Minecraft 1.8.9 doit être installé dans le même dossier.
// Compatible avec les dossiers .minecraft d'Amethyst (Android) : passer le
// dossier de jeu Amethyst avec -dir.
package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"log"
	"os"
	"path/filepath"
)

const (
	profileID   = "1.8.9-LH2"
	parentID    = "1.8.9"
	mainClass   = "net.minecraft.client.main.Main"
	mavenGroup  = "lh2"
	mavenArtif  = "lh2-loader"
	mavenVer    = "1.0.0"
	defaultConf = `# LH2 Loader : réglages
# culling          : off | normal | aggressive (cache les chunks hors vue)
# render_distance  : 2-32 chunks
# max_chunks       : 64-4096 chunks gardés en mémoire
# texture_cache_mb : 16-1024 Mo pour l'atlas de textures
# vsync            : true | false
# fps_limit        : 0 (illimité) ou 1-1000
# gl_profile       : gl21 (compatibilité) | gl33 (optionnel, non encore actif)
# fast_graphics    : true | false (désactive les effets coûteux)
# smooth_lighting  : true | false
# clouds           : true | false
# particles        : off | reduced | full
# entity_distance  : 16-256 blocs
# periodic_gc      : true | false (GC forcé périodiquement ; utile seulement si la mémoire monte)
# reduce_allocations : true | false (réutilise les objets dans les boucles de rendu)
culling=normal
render_distance=8
max_chunks=512
texture_cache_mb=128
vsync=false
fps_limit=0
gl_profile=gl21
fast_graphics=true
smooth_lighting=false
clouds=false
particles=reduced
entity_distance=96
periodic_gc=false
reduce_allocations=true
`
)

// Library décrit une entrée "libraries" d'un profil de version Minecraft.
type Library struct {
	Name string `json:"name"`
}

// Profile est le JSON de version lu par le launcher.
type Profile struct {
	ID                 string    `json:"id"`
	InheritsFrom       string    `json:"inheritsFrom"`
	Type               string    `json:"type"`
	MainClass          string    `json:"mainClass"`
	Libraries          []Library `json:"libraries"`
	MinecraftArguments string    `json:"minecraftArguments,omitempty"`
}

// NewProfile construit le profil LH2 hérité de 1.8.9.
func NewProfile() Profile {
	return Profile{
		ID:           profileID,
		InheritsFrom: parentID,
		Type:         "release",
		MainClass:    mainClass,
		Libraries: []Library{
			{Name: fmt.Sprintf("%s:%s:%s", mavenGroup, mavenArtif, mavenVer)},
		},
	}
}

// libraryPath renvoie le chemin relatif Maven de la bibliothèque LH2.
func libraryPath() string {
	return filepath.Join(mavenGroup, mavenArtif, mavenVer,
		fmt.Sprintf("%s-%s.jar", mavenArtif, mavenVer))
}

func copyFile(src, dst string) error {
	if err := os.MkdirAll(filepath.Dir(dst), 0o755); err != nil {
		return err
	}
	in, err := os.Open(src)
	if err != nil {
		return err
	}
	defer in.Close()
	out, err := os.Create(dst)
	if err != nil {
		return err
	}
	defer out.Close()
	_, err = io.Copy(out, in)
	return err
}

// Install écrit le profil, copie le JAR et crée les réglages par défaut.
func Install(mcDir, jarPath string) error {
	if _, err := os.Stat(jarPath); err != nil {
		return fmt.Errorf("JAR introuvable : %w", err)
	}
	if _, err := os.Stat(filepath.Join(mcDir, "versions", parentID)); err != nil {
		return fmt.Errorf("Minecraft %s n'est pas installé dans %s (lancez-le une fois avant)", parentID, mcDir)
	}

	profileDir := filepath.Join(mcDir, "versions", profileID)
	if err := os.MkdirAll(profileDir, 0o755); err != nil {
		return err
	}
	data, err := json.MarshalIndent(NewProfile(), "", "  ")
	if err != nil {
		return err
	}
	if err := os.WriteFile(filepath.Join(profileDir, profileID+".json"), data, 0o644); err != nil {
		return err
	}

	dst := filepath.Join(mcDir, "libraries", libraryPath())
	if err := copyFile(jarPath, dst); err != nil {
		return fmt.Errorf("copie du JAR : %w", err)
	}

	confPath := filepath.Join(mcDir, "config", "lh2loader.properties")
	if _, err := os.Stat(confPath); os.IsNotExist(err) {
		if err := os.MkdirAll(filepath.Dir(confPath), 0o755); err != nil {
			return err
		}
		if err := os.WriteFile(confPath, []byte(defaultConf), 0o644); err != nil {
			return err
		}
	}
	return nil
}

func main() {
	dir := flag.String("dir", "", "dossier .minecraft (ou dossier de jeu Amethyst)")
	jar := flag.String("jar", "lh2-loader.jar", "chemin du JAR lh2-loader")
	flag.Parse()
	if *dir == "" {
		log.Fatal("option -dir requise")
	}
	if err := Install(*dir, *jar); err != nil {
		log.Fatal(err)
	}
	fmt.Println("LH2 Loader installé : profil", profileID)
}
