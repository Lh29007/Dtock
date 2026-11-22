package main

import (
	"encoding/json"
	"os"
	"path/filepath"
	"testing"
)

func TestNewProfileInheritsVanilla(t *testing.T) {
	p := NewProfile()
	if p.InheritsFrom != "1.8.9" {
		t.Fatalf("inheritsFrom = %q", p.InheritsFrom)
	}
	if len(p.Libraries) != 1 || p.Libraries[0].Name != "lh2:lh2-loader:1.0.0" {
		t.Fatalf("libraries = %+v", p.Libraries)
	}
}

func TestInstallWritesEverything(t *testing.T) {
	mc := t.TempDir()
	if err := os.MkdirAll(filepath.Join(mc, "versions", "1.8.9"), 0o755); err != nil {
		t.Fatal(err)
	}
	jar := filepath.Join(t.TempDir(), "lh2-loader.jar")
	if err := os.WriteFile(jar, []byte("jar"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := Install(mc, jar); err != nil {
		t.Fatal(err)
	}
	raw, err := os.ReadFile(filepath.Join(mc, "versions", profileID, profileID+".json"))
	if err != nil {
		t.Fatal(err)
	}
	var p Profile
	if err := json.Unmarshal(raw, &p); err != nil || p.ID != profileID {
		t.Fatalf("profil invalide : %v", err)
	}
	if _, err := os.Stat(filepath.Join(mc, "libraries", libraryPath())); err != nil {
		t.Fatalf("JAR non copié : %v", err)
	}
	if _, err := os.Stat(filepath.Join(mc, "config", "lh2loader.properties")); err != nil {
		t.Fatalf("réglages absents : %v", err)
	}
}

func TestInstallRequiresVanilla(t *testing.T) {
	jar := filepath.Join(t.TempDir(), "x.jar")
	os.WriteFile(jar, []byte("x"), 0o644)
	if err := Install(t.TempDir(), jar); err == nil {
		t.Fatal("attendu une erreur sans 1.8.9 installé")
	}
}
