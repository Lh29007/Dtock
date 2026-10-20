// AlphaCraft : prototype Nintendo 3DS
// Menu, rendu voxel logiciel (ray-casting), 3 blocs (pierre, terre, bois).
//
// Contrôles :
//   Circle Pad      : se déplacer
//   Écran tactile   : tourner la caméra (glisser sur l'écran du bas)
//   Croix <- / ->   : changer de bloc
//   A               : sauter
//   R               : poser un bloc
//   L               : casser un bloc
//   X               : retour au menu
//   START           : quitter l'application

#include <3ds.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "textures.h"

// ---------------------------------------------------------------------------
// Monde
// ---------------------------------------------------------------------------
#define WORLD_X 32
#define WORLD_Y 16
#define WORLD_Z 32

enum { BLOCK_AIR = 0, BLOCK_STONE = 1, BLOCK_DIRT = 2, BLOCK_PLANKS = 3 };

static unsigned char world[WORLD_X][WORLD_Y][WORLD_Z];

static const char *hotbar_names[3] = { "Pierre", "Terre", "Bois" };
static const int hotbar_blocks[3] = { BLOCK_STONE, BLOCK_DIRT, BLOCK_PLANKS };

static void world_generate(void)
{
    for (int x = 0; x < WORLD_X; x++) {
        for (int z = 0; z < WORLD_Z; z++) {
            // Relief simple : hauteur entre 4 et 7
            int h = 4 + (int)((sinf(x * 0.35f) + cosf(z * 0.27f) + 2.0f) * 0.9f);
            for (int y = 0; y < WORLD_Y; y++) {
                if (y < h - 2)      world[x][y][z] = BLOCK_STONE;
                else if (y < h)     world[x][y][z] = BLOCK_DIRT;
                else                world[x][y][z] = BLOCK_AIR;
            }
        }
    }
}

// Renvoie le bloc aux coordonnées données (air hors du monde, sauf sous le sol)
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

// ---------------------------------------------------------------------------
// Joueur / caméra
// ---------------------------------------------------------------------------
static float pos_x = 16.5f, pos_y = 9.0f, pos_z = 16.5f;
static float vel_y = 0.0f;
static float yaw = 0.0f;     // rotation horizontale
static float pitch = -0.3f;  // rotation verticale
static int on_ground = 0;
static int selected = 0;     // index dans hotbar

#define PLAYER_RADIUS 0.3f
#define PLAYER_HEIGHT 1.7f
#define GRAVITY       0.02f
#define JUMP_SPEED    0.32f
#define MOVE_SPEED    0.09f
#define REACH         6.0f

// Test de collision : le joueur (boîte) chevauche-t-il un bloc solide ?
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
    // Axe X
    if (!player_collides(pos_x + dx, pos_y, pos_z)) pos_x += dx;
    // Axe Z
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
        if (player_collides(pos_x, ny, pos_z)) {
            vel_y = 0.0f;
        } else {
            pos_y = ny;
        }
        on_ground = 0;
    }
    if (pos_y < -10.0f) {
        pos_x = 16.5f; pos_y = 12.0f; pos_z = 16.5f; vel_y = 0.0f;
    }
}

// ---------------------------------------------------------------------------
// Ray-casting (DDA sur la grille de voxels)
// ---------------------------------------------------------------------------
typedef struct {
    int hit;
    int bx, by, bz;   // bloc touché
    int face;         // 0:-X 1:+X 2:-Y 3:+Y 4:-Z 5:+Z
    float u, v;       // coordonnées de texture 0..1
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

    for (int i = 0; i < 128; i++) {
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

        int b = block_at(x, y, z);
        if (b != BLOCK_AIR) {
            out->hit = 1;
            out->bx = x; out->by = y; out->bz = z;
            out->face = face;
            out->dist = t;
            out->block = b;
            float px = ox + dx * t, py = oy + dy * t, pz = oz + dz * t;
            float fx = px - floorf(px), fy = py - floorf(py), fz = pz - floorf(pz);
            // UV selon la face touchée
            if (face < 2)      { out->u = fz; out->v = 1.0f - fy; }
            else if (face < 4) { out->u = fx; out->v = fz; }
            else               { out->u = fx; out->v = 1.0f - fy; }
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// Framebuffer haut (400x240, stockage colonne par colonne, BGR8)
// ---------------------------------------------------------------------------
#define TOP_W 400
#define TOP_H 240
#define RAY_W 200   // rendu basse résolution, doublé à l'affichage
#define RAY_H 120

static inline void put_pixel(u8 *fb, int x, int y, u8 r, u8 g, u8 b)
{
    u8 *p = fb + (x * TOP_H + (TOP_H - 1 - y)) * 3;
    p[0] = b; p[1] = g; p[2] = r;
}

static void render_world(u8 *fb)
{
    float fwd_x = cosf(pitch) * sinf(yaw);
    float fwd_y = sinf(pitch);
    float fwd_z = cosf(pitch) * cosf(yaw);
    // right = normalize(cross(fwd, up))
    float rlen = sqrtf(fwd_z * fwd_z + fwd_x * fwd_x);
    float right_x = -fwd_z / rlen, right_z = fwd_x / rlen;
    // up = cross(right, fwd)
    float up_x = -right_z * fwd_y;
    float up_y = right_z * fwd_x - right_x * fwd_z;
    float up_z = right_x * fwd_y;

    const float k = 0.62f;                 // ~ champ de vision
    const float aspect = (float)RAY_W / RAY_H;
    const float eye_y = pos_y + PLAYER_HEIGHT - 0.2f;

    for (int py = 0; py < RAY_H; py++) {
        float sy = (1.0f - (py + 0.5f) / RAY_H * 2.0f) * k;
        for (int px = 0; px < RAY_W; px++) {
            float sx = ((px + 0.5f) / RAY_W * 2.0f - 1.0f) * aspect * k;
            float dx = fwd_x + right_x * sx + up_x * sy;
            float dy = fwd_y + up_y * sy;
            float dz = fwd_z + right_z * sx + up_z * sy;

            RayHit h;
            cast_ray(pos_x, eye_y, pos_z, dx, dy, dz, 40.0f, &h);

            u8 r = 110, g = 170, b = 255;  // ciel
            if (h.hit) {
                int tx = (int)(h.u * TEX_SIZE);
                int ty = (int)(h.v * TEX_SIZE);
                if (tx < 0) tx = 0;
                if (tx >= TEX_SIZE) tx = TEX_SIZE - 1;
                if (ty < 0) ty = 0;
                if (ty >= TEX_SIZE) ty = TEX_SIZE - 1;
                const unsigned char *tex = block_textures[h.block];
                const unsigned char *c = tex + (ty * TEX_SIZE + tx) * 3;
                // Ombrage par face
                float shade = (h.face >= 2 && h.face < 4) ? 1.0f
                            : (h.face < 2 ? 0.8f : 0.6f);
                if (h.face == 3) shade = 1.0f;
                r = (u8)(c[0] * shade);
                g = (u8)(c[1] * shade);
                b = (u8)(c[2] * shade);
            }
            // Affichage 2x2
            int X = px * 2, Y = py * 2;
            put_pixel(fb, X, Y, r, g, b);
            put_pixel(fb, X + 1, Y, r, g, b);
            put_pixel(fb, X, Y + 1, r, g, b);
            put_pixel(fb, X + 1, Y + 1, r, g, b);
        }
    }

    // Viseur
    for (int d = -4; d <= 4; d++) {
        put_pixel(fb, TOP_W / 2 + d, TOP_H / 2, 255, 255, 255);
        put_pixel(fb, TOP_W / 2, TOP_H / 2 + d, 255, 255, 255);
    }
}

// ---------------------------------------------------------------------------
// Menu
// ---------------------------------------------------------------------------
typedef enum { MENU_PLAY = 0, MENU_QUIT } MenuChoice;

static void draw_menu(int cursor)
{
    consoleClear();
    printf("\x1b[1;1HALPHACRAFT\n");
    printf("\x1b[3;3H%s JOUER\n", cursor == MENU_PLAY ? ">" : " ");
    printf("\x1b[5;3H%s QUITTER\n", cursor == MENU_QUIT ? ">" : " ");
    printf("\x1b[8;1HHaut/Bas: choisir  A: valider\n");
}

// Retourne 1 si le joueur a choisi de jouer, 0 pour quitter
static int run_menu(void)
{
    int cursor = MENU_PLAY;
    consoleInit(GFX_BOTTOM, NULL);
    draw_menu(cursor);

    while (aptMainLoop()) {
        hidScanInput();
        u32 down = hidKeysDown();
        touchPosition tp;
        hidTouchRead(&tp);

        if (down & KEY_DUP)   { cursor = MENU_PLAY; draw_menu(cursor); }
        if (down & KEY_DDOWN) { cursor = MENU_QUIT; draw_menu(cursor); }
        if (down & KEY_START) return 0;
        if (down & KEY_A) return cursor == MENU_PLAY;
        if (down & KEY_TOUCH) {
            if (tp.py < 120) return 1;   // moitié haute : jouer
            return 0;                    // moitié basse : quitter
        }

        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }
    return 0;
}

// ---------------------------------------------------------------------------
// HUD (écran du bas)
// ---------------------------------------------------------------------------
static void draw_hud(void)
{
    consoleClear();
    printf("\x1b[1;1HBloc :\n");
    for (int i = 0; i < 3; i++) {
        printf("\x1b[%d;3H%s %s\n", 3 + i,
               i == selected ? ">" : " ", hotbar_names[i]);
    }
    printf("\x1b[8;1HCircle: marcher  Touch: regarder\n");
    printf("A: sauter  R: poser  L: casser  X: menu\n");
}

// ---------------------------------------------------------------------------
// Boucle de jeu
// ---------------------------------------------------------------------------
static void reset_player(void)
{
    pos_x = 16.5f; pos_y = 12.0f; pos_z = 16.5f;
    vel_y = 0.0f;
    yaw = 0.0f; pitch = -0.3f;
    selected = 0;
}

// Retourne 0 si le joueur veut revenir au menu
static int run_game(void)
{
    consoleInit(GFX_BOTTOM, NULL);
    draw_hud();
    reset_player();

    int touching = 0;
    int last_tx = 0, last_ty = 0;
    int hud_dirty = 0;

    while (aptMainLoop()) {
        hidScanInput();
        u32 down = hidKeysDown();
        u32 held = hidKeysHeld();

        if (down & KEY_START) return 1;   // quitter l'application
        if (down & KEY_X)     return 0;   // retour au menu

        // Changer de bloc
        if (down & KEY_DLEFT)  { selected = (selected + 2) % 3; hud_dirty = 1; }
        if (down & KEY_DRIGHT) { selected = (selected + 1) % 3; hud_dirty = 1; }

        // Saut
        if ((down & KEY_A) && on_ground) vel_y = JUMP_SPEED;

        // Caméra : tactile sur l'écran du bas
        touchPosition tp;
        hidTouchRead(&tp);
        if (held & KEY_TOUCH) {
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
        } else {
            touching = 0;
        }

        // Déplacement : circle pad
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

        // Physique (gravité, saut)
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
                    // Ne pas poser dans le joueur ni hors du monde
                    if (nx >= 0 && nx < WORLD_X && ny >= 0 && ny < WORLD_Y &&
                        nz >= 0 && nz < WORLD_Z &&
                        world[nx][ny][nz] == BLOCK_AIR &&
                        !player_collides(pos_x, pos_y, pos_z)) {
                        world[nx][ny][nz] = (unsigned char)hotbar_blocks[selected];
                    }
                }
            }
        }

        // Rendu du monde (écran du haut)
        u8 *fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
        render_world(fb);

        if (hud_dirty) { draw_hud(); hud_dirty = 0; }

        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }
    return 1;
}

// ---------------------------------------------------------------------------
// Point d'entrée
// ---------------------------------------------------------------------------
int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    gfxInitDefault();
    gfxSetDoubleBuffering(GFX_TOP, true);
    world_generate();

    int quit = 0;
    while (aptMainLoop() && !quit) {
        if (!run_menu()) break;          // QUITTER depuis le menu
        if (run_game()) quit = 1;        // START en jeu : quitter l'app
    }

    gfxExit();
    return 0;
}
