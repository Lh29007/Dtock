// AlphaCraft v2 : prototype Nintendo 3DS
//
// - Menu graphique (écran du bas, boutons tactiles) : nouvelle partie, continuer,
//   éditeur de blocs, quitter.
// - Sauvegarde réelle : sdmc:/3ds/AlphaCraft/saves/
// - Éditeur de blocs : dessine une face 32x32 (tactile), sauvegarde-la comme bloc
//   utilisable dans le jeu (8 emplacements).
// - Rendu : textures 32x32, ciel en dégradé, brouillard, ombrage par face, bordure
//   de bloc, arrêt anticipé des rayons hors du monde (plus rapide).
//
// Contrôles en jeu :
//   Circle Pad      : se déplacer
//   Écran du bas    : glisser = tourner la caméra ; toucher la barre de blocs = choisir
//   Croix < / >     : changer de bloc
//   A               : sauter
//   R               : poser un bloc      L : casser un bloc
//   SELECT          : sauvegarder
//   X               : sauvegarder et retour au menu
//   START           : sauvegarder et quitter
//
// Contrôles dans l'éditeur : toucher le carré pour dessiner, la palette pour
// choisir une couleur, < / > pour changer d'emplacement, B pour revenir.

#include <3ds.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "textures.h"
#include "font.h"

// ---------------------------------------------------------------------------
// Constantes
// ---------------------------------------------------------------------------
#define TOP_W   400
#define BOT_W   320
#define SCR_H   240

#define TEX_N        32          // taille des textures à l'exécution
#define NUM_BLOCKS   12          // 0 = air, 1..3 = intégrés, 4..11 = perso
#define CUSTOM_BASE  4
#define MAX_CUSTOM   8

#define WORLD_X 32
#define WORLD_Y 16
#define WORLD_Z 32

#define RAY_W 200                // rendu basse résolution, doublé à l'affichage
#define RAY_H 120
#define MAX_DIST 40.0f
#define REACH    6.0f

#define PLAYER_RADIUS 0.3f
#define PLAYER_HEIGHT 1.7f
#define GRAVITY       0.02f
#define JUMP_SPEED    0.32f
#define MOVE_SPEED    0.09f

#define SAVE_ROOT   "sdmc:/3ds/AlphaCraft"
#define SAVE_DIR    "sdmc:/3ds/AlphaCraft/saves"
#define BLOCK_DIR   "sdmc:/3ds/AlphaCraft/saves/blocks"
#define WORLD_FILE  "sdmc:/3ds/AlphaCraft/saves/world.dat"

enum { BLOCK_AIR = 0, BLOCK_STONE = 1, BLOCK_DIRT = 2, BLOCK_PLANKS = 3 };
enum { MENU_NEW = 0, MENU_CONTINUE, MENU_EDITOR, MENU_QUIT, MENU_COUNT };
enum { EXIT_MENU = 0, EXIT_APP = 1 };

// ---------------------------------------------------------------------------
// Blocs et textures
// ---------------------------------------------------------------------------
static unsigned char block_tex[NUM_BLOCKS][TEX_N * TEX_N * 3];
static int custom_valid[MAX_CUSTOM];
static int slot_ids[NUM_BLOCKS];   // blocs choisissables, dans l'ordre de la barre
static int slot_count = 0;

static void upscale16(const unsigned char *src, unsigned char *dst)
{
    for (int y = 0; y < TEX_N; y++) {
        for (int x = 0; x < TEX_N; x++) {
            const unsigned char *s = src + ((y / 2) * TEX_SIZE + (x / 2)) * 3;
            unsigned char *d = dst + (y * TEX_N + x) * 3;
            d[0] = s[0]; d[1] = s[1]; d[2] = s[2];
        }
    }
}

static void load_builtin_textures(void)
{
    upscale16(tex_stone,  block_tex[BLOCK_STONE]);
    upscale16(tex_dirt,   block_tex[BLOCK_DIRT]);
    upscale16(tex_planks, block_tex[BLOCK_PLANKS]);
}

static void rebuild_slots(void)
{
    slot_count = 0;
    slot_ids[slot_count++] = BLOCK_STONE;
    slot_ids[slot_count++] = BLOCK_DIRT;
    slot_ids[slot_count++] = BLOCK_PLANKS;
    for (int i = 0; i < MAX_CUSTOM; i++) {
        if (custom_valid[i]) slot_ids[slot_count++] = CUSTOM_BASE + i;
    }
}

static void block_name(int id, char *out, size_t n)
{
    switch (id) {
        case BLOCK_STONE:  snprintf(out, n, "PIERRE"); break;
        case BLOCK_DIRT:   snprintf(out, n, "TERRE"); break;
        case BLOCK_PLANKS: snprintf(out, n, "BOIS"); break;
        default:           snprintf(out, n, "PERSO %d", id - CUSTOM_BASE + 1); break;
    }
}

// ---------------------------------------------------------------------------
// Sauvegardes
// ---------------------------------------------------------------------------
static void saves_init(void)
{
    mkdir(SAVE_ROOT, 0777);
    mkdir(SAVE_DIR, 0777);
    mkdir(BLOCK_DIR, 0777);
}

static void custom_load_all(void)
{
    for (int i = 0; i < MAX_CUSTOM; i++) {
        char path[160];
        snprintf(path, sizeof path, "%s/b%d.bin", BLOCK_DIR, i);
        custom_valid[i] = 0;
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        char magic[4];
        if (fread(magic, 1, 4, f) == 4 && memcmp(magic, "ACB1", 4) == 0 &&
            fread(block_tex[CUSTOM_BASE + i], 1, TEX_N * TEX_N * 3, f) == TEX_N * TEX_N * 3) {
            custom_valid[i] = 1;
        }
        fclose(f);
    }
    rebuild_slots();
}

static int custom_save(int slot, const unsigned char *rgb)
{
    char path[160];
    snprintf(path, sizeof path, "%s/b%d.bin", BLOCK_DIR, slot);
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    fwrite("ACB1", 1, 4, f);
    size_t n = fwrite(rgb, 1, TEX_N * TEX_N * 3, f);
    fclose(f);
    if (n != TEX_N * TEX_N * 3) return 0;
    memcpy(block_tex[CUSTOM_BASE + slot], rgb, TEX_N * TEX_N * 3);
    custom_valid[slot] = 1;
    rebuild_slots();
    return 1;
}

// ---------------------------------------------------------------------------
// Monde et joueur
// ---------------------------------------------------------------------------
static unsigned char world[WORLD_X][WORLD_Y][WORLD_Z];

static float pos_x = 16.5f, pos_y = 12.0f, pos_z = 16.5f;
static float vel_y = 0.0f;
static float yaw = 0.0f;
static float pitch = -0.3f;
static int on_ground = 0;
static int selected = 0;   // index dans slot_ids

static void world_generate(void)
{
    for (int x = 0; x < WORLD_X; x++) {
        for (int z = 0; z < WORLD_Z; z++) {
            int h = 4 + (int)((sinf(x * 0.35f) + cosf(z * 0.27f) + 2.0f) * 0.9f);
            for (int y = 0; y < WORLD_Y; y++) {
                if (y < h - 2)  world[x][y][z] = BLOCK_STONE;
                else if (y < h) world[x][y][z] = BLOCK_DIRT;
                else            world[x][y][z] = BLOCK_AIR;
            }
        }
    }
}

typedef struct {
    char magic[4];       // "ACW1"
    float x, y, z, yaw, pitch;
    int selected;
} WorldHeader;

static int world_save(void)
{
    FILE *f = fopen(WORLD_FILE, "wb");
    if (!f) return 0;
    WorldHeader h;
    memcpy(h.magic, "ACW1", 4);
    h.x = pos_x; h.y = pos_y; h.z = pos_z;
    h.yaw = yaw; h.pitch = pitch; h.selected = selected;
    int ok = fwrite(&h, sizeof h, 1, f) == 1;
    ok = ok && fwrite(world, sizeof world, 1, f) == 1;
    fclose(f);
    return ok;
}

static int world_load(void)
{
    FILE *f = fopen(WORLD_FILE, "rb");
    if (!f) return 0;
    WorldHeader h;
    int ok = fread(&h, sizeof h, 1, f) == 1 && memcmp(h.magic, "ACW1", 4) == 0;
    ok = ok && fread(world, sizeof world, 1, f) == 1;
    fclose(f);
    if (!ok) return 0;
    pos_x = h.x; pos_y = h.y; pos_z = h.z;
    yaw = h.yaw; pitch = h.pitch;
    selected = h.selected;
    vel_y = 0.0f;
    if (selected < 0 || selected >= slot_count) selected = 0;
    return 1;
}

static void reset_player(void)
{
    pos_x = 16.5f; pos_y = 12.0f; pos_z = 16.5f;
    vel_y = 0.0f;
    yaw = 0.0f; pitch = -0.3f;
    selected = 0;
}

static int block_at(int x, int y, int z)
{
    if (y < 0) return BLOCK_STONE;
    if (x < 0 || x >= WORLD_X || y >= WORLD_Y || z < 0 || z >= WORLD_Z) return BLOCK_AIR;
    return world[x][y][z];
}

static int solid_at(int x, int y, int z)
{
    return block_at(x, y, z) != BLOCK_AIR;
}

static int player_collides(float px, float py, float pz)
{
    int x0 = (int)floorf(px - PLAYER_RADIUS), x1 = (int)floorf(px + PLAYER_RADIUS);
    int z0 = (int)floorf(pz - PLAYER_RADIUS), z1 = (int)floorf(pz + PLAYER_RADIUS);
    int y0 = (int)floorf(py), y1 = (int)floorf(py + PLAYER_HEIGHT - 0.01f);
    for (int x = x0; x <= x1; x++)
        for (int y = y0; y <= y1; y++)
            for (int z = z0; z <= z1; z++)
                if (solid_at(x, y, z)) return 1;
    return 0;
}

static void player_move(float dx, float dz)
{
    if (!player_collides(pos_x + dx, pos_y, pos_z)) pos_x += dx;
    if (!player_collides(pos_x, pos_y, pos_z + dz)) pos_z += dz;
}

static void player_physics(void)
{
    vel_y -= GRAVITY;
    float ny = pos_y + vel_y;
    if (vel_y < 0.0f) {
        if (player_collides(pos_x, ny, pos_z)) {
            vel_y = 0.0f;
            on_ground = 1;
            pos_y = floorf(pos_y + 0.001f);
        } else {
            pos_y = ny;
            on_ground = 0;
        }
    } else {
        if (player_collides(pos_x, ny, pos_z)) vel_y = 0.0f;
        else pos_y = ny;
        on_ground = 0;
    }
    if (pos_y < -10.0f) {
        pos_x = 16.5f; pos_y = 12.0f; pos_z = 16.5f; vel_y = 0.0f;
    }
}

// ---------------------------------------------------------------------------
// Ray-casting (DDA sur la grille)
// ---------------------------------------------------------------------------
typedef struct {
    int hit;
    int bx, by, bz;
    int face;          // 0:-X 1:+X 2:-Y 3:+Y 4:-Z 5:+Z
    float u, v;
    float dist;
    int block;
} RayHit;

static const int face_offset[6][3] = {
    {-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}
};

static void cast_ray(float ox, float oy, float oz, float dx, float dy, float dz,
                     float max_dist, RayHit *out)
{
    out->hit = 0;
    int x = (int)floorf(ox), y = (int)floorf(oy), z = (int)floorf(oz);
    int sx = dx > 0 ? 1 : -1, sy = dy > 0 ? 1 : -1, sz = dz > 0 ? 1 : -1;
    const float inf = 1e9f;
    float tdx = fabsf(dx) > 1e-6f ? fabsf(1.0f / dx) : inf;
    float tdy = fabsf(dy) > 1e-6f ? fabsf(1.0f / dy) : inf;
    float tdz = fabsf(dz) > 1e-6f ? fabsf(1.0f / dz) : inf;
    float tmx = dx > 0 ? (x + 1 - ox) * tdx : (ox - x) * tdx;
    float tmy = dy > 0 ? (y + 1 - oy) * tdy : (oy - y) * tdy;
    float tmz = dz > 0 ? (z + 1 - oz) * tdz : (oz - z) * tdz;
    int face = 0;
    float t = 0.0f;

    for (int i = 0; i < 160; i++) {
        if (tmx < tmy && tmx < tmz) {
            t = tmx; tmx += tdx; x += sx;
            face = sx > 0 ? 0 : 1;
        } else if (tmy < tmz) {
            t = tmy; tmy += tdy; y += sy;
            face = sy > 0 ? 2 : 3;
        } else {
            t = tmz; tmz += tdz; z += sz;
            face = sz > 0 ? 4 : 5;
        }
        if (t > max_dist) break;

        // Arrêt anticipé : au-dessus du monde, ou sorti latéralement en s'éloignant
        if (y >= WORLD_Y && sy > 0) break;
        if ((x < 0 && sx < 0) || (x >= WORLD_X && sx > 0) ||
            (z < 0 && sz < 0) || (z >= WORLD_Z && sz > 0)) break;

        int b = block_at(x, y, z);
        if (b != BLOCK_AIR) {
            out->hit = 1;
            out->bx = x; out->by = y; out->bz = z;
            out->face = face;
            out->dist = t;
            out->block = b;
            float px = ox + dx * t, py = oy + dy * t, pz = oz + dz * t;
            float fx = px - floorf(px), fy = py - floorf(py), fz = pz - floorf(pz);
            if (face < 2)      { out->u = fz; out->v = 1.0f - fy; }
            else if (face < 4) { out->u = fx; out->v = fz; }
            else               { out->u = fx; out->v = 1.0f - fy; }
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// Dessin bas niveau (framebuffers 3DS : colonnes de 240 px, BGR8)
// ---------------------------------------------------------------------------
static inline void px_set(u8 *fb, int fbw, int x, int y, u8 r, u8 g, u8 b)
{
    if (x < 0 || y < 0 || x >= fbw || y >= SCR_H) return;
    u8 *p = fb + (x * SCR_H + (SCR_H - 1 - y)) * 3;
    p[0] = b; p[1] = g; p[2] = r;
}

static void fb_rect(u8 *fb, int fbw, int x, int y, int w, int h, u8 r, u8 g, u8 b)
{
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = (x + w > fbw) ? fbw : x + w;
    int y1 = (y + h > SCR_H) ? SCR_H : y + h;
    for (int yy = y0; yy < y1; yy++)
        for (int xx = x0; xx < x1; xx++)
            px_set(fb, fbw, xx, yy, r, g, b);
}

static void fb_text(u8 *fb, int fbw, int x, int y, int scale, const char *s,
                    u8 r, u8 g, u8 b)
{
    for (const char *p = s; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c < 32 || c > 126) c = '?';
        const unsigned char *glyph = font8x8[c - 32];
        for (int row = 0; row < 8; row++) {
            for (int col = 0; col < 8; col++) {
                if (glyph[row] & (1 << (7 - col))) {
                    fb_rect(fb, fbw, x + col * scale, y + row * scale, scale, scale, r, g, b);
                }
            }
        }
        x += 8 * scale;
    }
}

static int text_width(const char *s, int scale)
{
    return (int)strlen(s) * 8 * scale;
}

static void fb_text_center(u8 *fb, int fbw, int cx, int y, int scale, const char *s,
                           u8 r, u8 g, u8 b)
{
    fb_text(fb, fbw, cx - text_width(s, scale) / 2, y, scale, s, r, g, b);
}

static void draw_button(u8 *fb, int fbw, int x, int y, int w, int h, const char *label,
                        int highlighted)
{
    if (highlighted) {
        fb_rect(fb, fbw, x, y, w, h, 96, 170, 72);
        fb_rect(fb, fbw, x, y, w, 2, 200, 240, 170);
    } else {
        fb_rect(fb, fbw, x, y, w, h, 62, 62, 72);
        fb_rect(fb, fbw, x, y, w, 2, 110, 110, 124);
    }
    fb_text(fb, fbw, x + (w - text_width(label, 1)) / 2, y + (h - 8) / 2, 1, label,
            255, 255, 255);
}

static int in_rect(int px, int py, int x, int y, int w, int h)
{
    return px >= x && px < x + w && py >= y && py < y + h;
}

// Sampling d'une texture 32x32 : renvoie la couleur dans out[3]
static inline void tex_sample(const unsigned char *tex, float u, float v, unsigned char *out)
{
    int tx = (int)(u * TEX_N), ty = (int)(v * TEX_N);
    if (tx < 0) tx = 0;
    if (tx >= TEX_N) tx = TEX_N - 1;
    if (ty < 0) ty = 0;
    if (ty >= TEX_N) ty = TEX_N - 1;
    const unsigned char *c = tex + (ty * TEX_N + tx) * 3;
    out[0] = c[0]; out[1] = c[1]; out[2] = c[2];
}

// ---------------------------------------------------------------------------
// Rendu 3D (écran du haut)
// ---------------------------------------------------------------------------
static void render_world(u8 *fb)
{
    float fwd_x = cosf(pitch) * sinf(yaw);
    float fwd_y = sinf(pitch);
    float fwd_z = cosf(pitch) * cosf(yaw);
    float rlen = sqrtf(fwd_z * fwd_z + fwd_x * fwd_x);
    float right_x = -fwd_z / rlen, right_z = fwd_x / rlen;
    float up_x = -right_z * fwd_y;
    float up_y = right_z * fwd_x - right_x * fwd_z;
    float up_z = right_x * fwd_y;

    const float k = 0.62f;
    const float aspect = (float)RAY_W / RAY_H;
    const float eye_y = pos_y + PLAYER_HEIGHT - 0.2f;

    const float hor_r = 190, hor_g = 215, hor_b = 255;   // horizon
    const float top_r = 70,  top_g = 130, top_b = 230;   // ciel haut

    for (int py = 0; py < RAY_H; py++) {
        float sy = (1.0f - (py + 0.5f) / RAY_H * 2.0f) * k;
        for (int px = 0; px < RAY_W; px++) {
            float sx = ((px + 0.5f) / RAY_W * 2.0f - 1.0f) * aspect * k;
            float dx = fwd_x + right_x * sx + up_x * sy;
            float dy = fwd_y + up_y * sy;
            float dz = fwd_z + right_z * sx + up_z * sy;

            RayHit h;
            cast_ray(pos_x, eye_y, pos_z, dx, dy, dz, MAX_DIST, &h);

            float r, g, b;
            if (!h.hit) {
                float s = (dy + 0.1f) * 2.0f;
                if (s < 0) s = 0;
                if (s > 1) s = 1;
                r = hor_r + (top_r - hor_r) * s;
                g = hor_g + (top_g - hor_g) * s;
                b = hor_b + (top_b - hor_b) * s;
            } else {
                unsigned char c[3];
                tex_sample(block_tex[h.block], h.u, h.v, c);

                float shade;
                switch (h.face) {
                    case 3:  shade = 1.0f;  break;   // dessus
                    case 2:  shade = 0.5f;  break;   // dessous
                    case 0:
                    case 1:  shade = 0.8f;  break;   // côtés X
                    default: shade = 0.65f; break;   // côtés Z
                }

                // bordure de bloc (lisibilité du relief)
                if (h.u < 0.04f || h.u > 0.96f || h.v < 0.04f || h.v > 0.96f) shade *= 0.78f;

                // brouillard vers l'horizon
                float fog = h.dist / (MAX_DIST * 0.7f);
                if (fog > 1) fog = 1;

                r = c[0] * shade * (1 - fog) + hor_r * fog;
                g = c[1] * shade * (1 - fog) + hor_g * fog;
                b = c[2] * shade * (1 - fog) + hor_b * fog;
            }

            int X = px * 2, Y = py * 2;
            u8 R = (u8)r, G = (u8)g, B = (u8)b;
            px_set(fb, TOP_W, X,     Y,     R, G, B);
            px_set(fb, TOP_W, X + 1, Y,     R, G, B);
            px_set(fb, TOP_W, X,     Y + 1, R, G, B);
            px_set(fb, TOP_W, X + 1, Y + 1, R, G, B);
        }
    }

    // Viseur
    for (int d = -4; d <= 4; d++) {
        px_set(fb, TOP_W, TOP_W / 2 + d, SCR_H / 2, 255, 255, 255);
        px_set(fb, TOP_W, TOP_W / 2, SCR_H / 2 + d, 255, 255, 255);
    }
}

// ---------------------------------------------------------------------------
// Menu principal
// ---------------------------------------------------------------------------
static const char *menu_labels[MENU_COUNT] = {
    "NOUVELLE PARTIE", "CONTINUER", "EDITEUR DE BLOCS", "QUITTER"
};

#define MENU_BTN_X 40
#define MENU_BTN_Y 60
#define MENU_BTN_W 240
#define MENU_BTN_H 36
#define MENU_BTN_STEP 45

static void draw_menu_top(u8 *top)
{
    fb_rect(top, TOP_W, 0, 0, TOP_W, SCR_H, 120, 170, 240);
    fb_rect(top, TOP_W, 0, 150, TOP_W, 90, 96, 150, 70);   // herbe
    fb_text_center(top, TOP_W, TOP_W / 2, 40, 4, "ALPHACRAFT", 255, 255, 255);
    fb_text_center(top, TOP_W, TOP_W / 2, 92, 2, "PROTOTYPE 1.2.6", 230, 240, 255);

    // Trois blocs en aperçu, agrandis x3
    const int tile = 96, gap = 16;
    int x0 = (TOP_W - (3 * tile + 2 * gap)) / 2;
    int ids[3] = { BLOCK_STONE, BLOCK_DIRT, BLOCK_PLANKS };
    for (int t = 0; t < 3; t++) {
        for (int y = 0; y < tile; y++) {
            for (int x = 0; x < tile; x++) {
                unsigned char c[3];
                tex_sample(block_tex[ids[t]], (x + 0.5f) / tile, (y + 0.5f) / tile, c);
                px_set(top, TOP_W, x0 + t * (tile + gap) + x, 120 + y, c[0], c[1], c[2]);
            }
        }
    }
}

static void draw_menu_bottom(u8 *bot, int cursor)
{
    fb_rect(bot, BOT_W, 0, 0, BOT_W, SCR_H, 28, 28, 34);
    for (int i = 0; i < MENU_COUNT; i++) {
        draw_button(bot, BOT_W, MENU_BTN_X, MENU_BTN_Y + i * MENU_BTN_STEP,
                    MENU_BTN_W, MENU_BTN_H, menu_labels[i], i == cursor);
    }
    fb_text_center(bot, BOT_W, BOT_W / 2, 226, 1, "HAUT/BAS: CHOISIR   A: VALIDER",
                   180, 180, 190);
}

static int run_menu(void)
{
    int cursor = MENU_CONTINUE;
    int was_touch = 0;
    while (aptMainLoop()) {
        hidScanInput();
        u32 down = hidKeysDown();
        u32 held = hidKeysHeld();
        touchPosition tp;
        hidTouchRead(&tp);
        int touched = (held & KEY_TOUCH) != 0;
        int pressed = touched && !was_touch;
        was_touch = touched;

        if (down & KEY_DUP)   cursor = (cursor + MENU_COUNT - 1) % MENU_COUNT;
        if (down & KEY_DDOWN) cursor = (cursor + 1) % MENU_COUNT;

        int choice = -1;
        if (down & KEY_A) choice = cursor;
        if (down & KEY_START) choice = MENU_QUIT;
        if (pressed) {
            for (int i = 0; i < MENU_COUNT; i++) {
                if (in_rect(tp.px, tp.py, MENU_BTN_X, MENU_BTN_Y + i * MENU_BTN_STEP,
                            MENU_BTN_W, MENU_BTN_H)) {
                    cursor = i;
                    choice = i;
                }
            }
        }

        u8 *top = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
        u8 *bot = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
        draw_menu_top(top);
        draw_menu_bottom(bot, cursor);

        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();

        if (choice >= 0) return choice;
    }
    return MENU_QUIT;
}

// ---------------------------------------------------------------------------
// Jeu
// ---------------------------------------------------------------------------
#define HOTBAR_Y     196
#define HOTBAR_ICON  24
#define HOTBAR_STEP  30

static int hotbar_x0(void)
{
    int total = slot_count * HOTBAR_STEP - (HOTBAR_STEP - HOTBAR_ICON);
    return (BOT_W - total) / 2;
}

static int hotbar_index_at(int x, int y)
{
    if (y < HOTBAR_Y - 4 || y > HOTBAR_Y + HOTBAR_ICON + 4) return -1;
    int x0 = hotbar_x0();
    for (int i = 0; i < slot_count; i++) {
        int ix = x0 + i * HOTBAR_STEP;
        if (x >= ix && x < ix + HOTBAR_ICON) return i;
    }
    return -1;
}

static void draw_game_bottom(u8 *bot, int flash)
{
    fb_rect(bot, BOT_W, 0, 0, BOT_W, SCR_H, 0, 0, 0);
    fb_text(bot, BOT_W, 8, 8,  1, "CIRCLE: MARCHER   TOUCHE: REGARDER", 200, 200, 200);
    fb_text(bot, BOT_W, 8, 20, 1, "A: SAUTER  R: POSER  L: CASSER", 200, 200, 200);
    fb_text(bot, BOT_W, 8, 32, 1, "< >: BLOC  SELECT: SAUVER", 200, 200, 200);
    fb_text(bot, BOT_W, 8, 44, 1, "X: MENU  START: QUITTER", 200, 200, 200);

    if (flash > 0) {
        fb_text_center(bot, BOT_W, BOT_W / 2, 100, 2, "SAUVEGARDE OK", 120, 255, 120);
    }

    int x0 = hotbar_x0();
    for (int i = 0; i < slot_count; i++) {
        int id = slot_ids[i];
        int ix = x0 + i * HOTBAR_STEP;
        if (i == selected) {
            fb_rect(bot, BOT_W, ix - 3, HOTBAR_Y - 3, HOTBAR_ICON + 6, 2, 255, 255, 255);
            fb_rect(bot, BOT_W, ix - 3, HOTBAR_Y + HOTBAR_ICON + 1, HOTBAR_ICON + 6, 2, 255, 255, 255);
            fb_rect(bot, BOT_W, ix - 3, HOTBAR_Y - 3, 2, HOTBAR_ICON + 6, 255, 255, 255);
            fb_rect(bot, BOT_W, ix + HOTBAR_ICON + 1, HOTBAR_Y - 3, 2, HOTBAR_ICON + 6, 255, 255, 255);
        }
        for (int yy = 0; yy < HOTBAR_ICON; yy++) {
            for (int xx = 0; xx < HOTBAR_ICON; xx++) {
                unsigned char c[3];
                tex_sample(block_tex[id], (xx + 0.5f) / HOTBAR_ICON, (yy + 0.5f) / HOTBAR_ICON, c);
                px_set(bot, BOT_W, ix + xx, HOTBAR_Y + yy, c[0], c[1], c[2]);
            }
        }
    }

    char name[24];
    block_name(slot_ids[selected], name, sizeof name);
    fb_text_center(bot, BOT_W, BOT_W / 2, 176, 1, name, 255, 255, 255);
}

// Retourne EXIT_MENU (X) ou EXIT_APP (START)
static int run_game(void)
{
    int touching = 0;
    int ui_touch = 0;
    int last_tx = 0, last_ty = 0;
    int flash = 0;
    int was_touch = 0;

    while (aptMainLoop()) {
        hidScanInput();
        u32 down = hidKeysDown();
        u32 held = hidKeysHeld();

        if (down & KEY_START) return EXIT_APP;
        if (down & KEY_X)     return EXIT_MENU;
        if (down & KEY_SELECT) {
            if (world_save()) flash = 90;
        }

        if (down & KEY_DLEFT)  selected = (selected + slot_count - 1) % slot_count;
        if (down & KEY_DRIGHT) selected = (selected + 1) % slot_count;

        if ((down & KEY_A) && on_ground) vel_y = JUMP_SPEED;

        // Tactile : barre de blocs ou caméra
        touchPosition tp;
        hidTouchRead(&tp);
        int touched = (held & KEY_TOUCH) != 0;
        int pressed = touched && !was_touch;
        was_touch = touched;

        if (!touched) {
            ui_touch = 0;
            touching = 0;
        } else if (pressed) {
            int idx = hotbar_index_at(tp.px, tp.py);
            if (idx >= 0) {
                selected = idx;
                ui_touch = 1;
            }
        }
        if (touched && !ui_touch) {
            if (touching) {
                int dx = tp.px - last_tx;
                int dy = tp.py - last_ty;
                yaw += dx * 0.01f;
                pitch -= dy * 0.01f;
                if (pitch > 1.4f) pitch = 1.4f;
                if (pitch < -1.4f) pitch = -1.4f;
            }
            last_tx = tp.px; last_ty = tp.py;
            touching = 1;
        }

        // Déplacement : Circle Pad
        circlePosition cp;
        hidCircleRead(&cp);
        float cx = cp.dx / 156.0f, cy = cp.dy / 156.0f;
        if (fabsf(cx) < 0.15f) cx = 0.0f;
        if (fabsf(cy) < 0.15f) cy = 0.0f;
        float fwd_x = sinf(yaw), fwd_z = cosf(yaw);
        float right_x = -fwd_z, right_z = fwd_x;
        float mx = (fwd_x * cy + right_x * cx) * MOVE_SPEED;
        float mz = (fwd_z * cy + right_z * cx) * MOVE_SPEED;
        player_move(mx, mz);
        player_physics();

        // Casser / poser
        if (down & (KEY_L | KEY_R)) {
            RayHit h;
            float eye_y = pos_y + PLAYER_HEIGHT - 0.2f;
            float cfx = cosf(pitch) * sinf(yaw);
            float cfy = sinf(pitch);
            float cfz = cosf(pitch) * cosf(yaw);
            cast_ray(pos_x, eye_y, pos_z, cfx, cfy, cfz, REACH, &h);
            if (h.hit) {
                if (down & KEY_L) {
                    if (h.by >= 0) world[h.bx][h.by][h.bz] = BLOCK_AIR;
                } else {
                    int nx = h.bx + face_offset[h.face][0];
                    int ny = h.by + face_offset[h.face][1];
                    int nz = h.bz + face_offset[h.face][2];
                    if (nx >= 0 && nx < WORLD_X && ny >= 0 && ny < WORLD_Y &&
                        nz >= 0 && nz < WORLD_Z &&
                        world[nx][ny][nz] == BLOCK_AIR &&
                        !player_collides(pos_x, pos_y, pos_z)) {
                        world[nx][ny][nz] = (unsigned char)slot_ids[selected];
                    }
                }
            }
        }

        u8 *top = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
        u8 *bot = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
        render_world(top);
        draw_game_bottom(bot, flash);
        if (flash > 0) flash--;

        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }
    return EXIT_APP;
}

// ---------------------------------------------------------------------------
// Éditeur de blocs
// ---------------------------------------------------------------------------
static const unsigned char editor_palette[16][3] = {
    {0, 0, 0},       {255, 255, 255}, {128, 128, 128}, {64, 64, 64},
    {139, 90, 43},   {101, 67, 33},   {34, 139, 34},   {100, 180, 60},
    {60, 120, 200},  {30, 60, 140},   {230, 200, 60},  {200, 40, 40},
    {240, 140, 40},  {160, 100, 200}, {210, 180, 140}, {90, 40, 20}
};

#define ED_CANVAS_X 12
#define ED_CANVAS_Y 20
#define ED_CELL     6            // 32 * 6 = 192
#define ED_PAL_X    216
#define ED_PAL_Y    20
#define ED_PAL_CELL 18
#define ED_SLOT_Y   100

static void editor_load(int slot, unsigned char *canvas)
{
    if (custom_valid[slot]) {
        memcpy(canvas, block_tex[CUSTOM_BASE + slot], TEX_N * TEX_N * 3);
    } else {
        memset(canvas, 128, TEX_N * TEX_N * 3);
    }
}

static void draw_editor(u8 *top, u8 *bot, const unsigned char *canvas, int slot,
                        int color, int flash)
{
    // Écran du bas : canevas, palette, boutons
    fb_rect(bot, BOT_W, 0, 0, BOT_W, SCR_H, 24, 24, 28);
    for (int y = 0; y < TEX_N; y++) {
        for (int x = 0; x < TEX_N; x++) {
            const unsigned char *c = canvas + (y * TEX_N + x) * 3;
            fb_rect(bot, BOT_W, ED_CANVAS_X + x * ED_CELL, ED_CANVAS_Y + y * ED_CELL,
                    ED_CELL, ED_CELL, c[0], c[1], c[2]);
        }
    }
    for (int i = 0; i < 16; i++) {
        int cx = ED_PAL_X + (i % 4) * ED_PAL_CELL;
        int cy = ED_PAL_Y + (i / 4) * ED_PAL_CELL;
        fb_rect(bot, BOT_W, cx, cy, 16, 16, editor_palette[i][0], editor_palette[i][1],
                editor_palette[i][2]);
        if (i == color) fb_rect(bot, BOT_W, cx - 1, cy - 1, 18, 2, 255, 255, 255);
    }
    draw_button(bot, BOT_W, 216, ED_SLOT_Y, 20, 20, "<", 0);
    draw_button(bot, BOT_W, 292, ED_SLOT_Y, 20, 20, ">", 0);
    char label[16];
    snprintf(label, sizeof label, "SLOT %d", slot + 1);
    fb_text_center(bot, BOT_W, 262, ED_SLOT_Y + 6, 1, label, 255, 255, 255);
    draw_button(bot, BOT_W, 216, 130, 96, 20, "EFFACER", 0);
    draw_button(bot, BOT_W, 216, 156, 96, 20, "SAUVER", 0);
    draw_button(bot, BOT_W, 216, 182, 96, 20, "RETOUR", 0);
    fb_text(bot, BOT_W, 12, 226, 1, "TOUCHE: DESSINER   B: RETOUR", 170, 170, 180);

    // Écran du haut : aperçu agrandi x5 et infos
    fb_rect(top, TOP_W, 0, 0, TOP_W, SCR_H, 20, 20, 26);
    fb_text_center(top, TOP_W, TOP_W / 2, 10, 3, "EDITEUR DE BLOCS", 255, 255, 255);
    for (int y = 0; y < 160; y++) {
        for (int x = 0; x < 160; x++) {
            unsigned char c[3];
            tex_sample(canvas, (x + 0.5f) / 160.0f, (y + 0.5f) / 160.0f, c);
            px_set(top, TOP_W, 120 + x, 50 + y, c[0], c[1], c[2]);
        }
    }
    fb_text_center(top, TOP_W, TOP_W / 2, 216, 1, "APERCU : UNE FACE, REPETEE SUR LE BLOC", 180, 180, 200);
    if (flash > 0) {
        fb_text_center(top, TOP_W, TOP_W / 2, 30, 2, "BLOC SAUVE", 120, 255, 120);
    }
}

static void run_editor(void)
{
    unsigned char canvas[TEX_N * TEX_N * 3];
    int slot = 0, color = 1, flash = 0, was_touch = 0;
    editor_load(slot, canvas);

    while (aptMainLoop()) {
        hidScanInput();
        u32 down = hidKeysDown();
        u32 held = hidKeysHeld();
        touchPosition tp;
        hidTouchRead(&tp);
        int touched = (held & KEY_TOUCH) != 0;
        int pressed = touched && !was_touch;
        was_touch = touched;

        if (down & KEY_B) break;
        if (down & KEY_DLEFT)  { slot = (slot + MAX_CUSTOM - 1) % MAX_CUSTOM; editor_load(slot, canvas); }
        if (down & KEY_DRIGHT) { slot = (slot + 1) % MAX_CUSTOM; editor_load(slot, canvas); }

        if (touched) {
            int x = tp.px, y = tp.py;
            // Dessin sur le canevas
            if (in_rect(x, y, ED_CANVAS_X, ED_CANVAS_Y, TEX_N * ED_CELL, TEX_N * ED_CELL)) {
                int cx = (x - ED_CANVAS_X) / ED_CELL;
                int cy = (y - ED_CANVAS_Y) / ED_CELL;
                unsigned char *d = canvas + (cy * TEX_N + cx) * 3;
                d[0] = editor_palette[color][0];
                d[1] = editor_palette[color][1];
                d[2] = editor_palette[color][2];
            }
            if (pressed) {
                if (in_rect(x, y, ED_PAL_X, ED_PAL_Y, 4 * ED_PAL_CELL, 4 * ED_PAL_CELL)) {
                    int col = (x - ED_PAL_X) / ED_PAL_CELL;
                    int row = (y - ED_PAL_Y) / ED_PAL_CELL;
                    color = row * 4 + col;
                }
                if (in_rect(x, y, 216, ED_SLOT_Y, 20, 20)) {
                    slot = (slot + MAX_CUSTOM - 1) % MAX_CUSTOM;
                    editor_load(slot, canvas);
                }
                if (in_rect(x, y, 292, ED_SLOT_Y, 20, 20)) {
                    slot = (slot + 1) % MAX_CUSTOM;
                    editor_load(slot, canvas);
                }
                if (in_rect(x, y, 216, 130, 96, 20)) {
                    memset(canvas, 128, TEX_N * TEX_N * 3);
                }
                if (in_rect(x, y, 216, 156, 96, 20)) {
                    if (custom_save(slot, canvas)) flash = 90;
                }
                if (in_rect(x, y, 216, 182, 96, 20)) {
                    break;
                }
            }
        }

        u8 *top = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
        u8 *bot = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
        draw_editor(top, bot, canvas, slot, color, flash);
        if (flash > 0) flash--;

        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }
}

// ---------------------------------------------------------------------------
// Point d'entrée
// ---------------------------------------------------------------------------
int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    gfxInitDefault();
    gfxSetDoubleBuffering(GFX_TOP, true);
    gfxSetDoubleBuffering(GFX_BOTTOM, true);

    saves_init();
    load_builtin_textures();
    custom_load_all();
    world_generate();
    reset_player();

    while (aptMainLoop()) {
        int choice = run_menu();
        if (choice == MENU_QUIT) break;
        if (choice == MENU_EDITOR) {
            run_editor();
            continue;
        }

        if (choice == MENU_NEW) {
            world_generate();
            reset_player();
        } else if (!world_load()) {
            world_generate();
            reset_player();
        }
        selected = selected >= slot_count ? 0 : selected;

        int exit_code = run_game();
        world_save();
        if (exit_code == EXIT_APP) break;
    }

    gfxExit();
    return 0;
}
