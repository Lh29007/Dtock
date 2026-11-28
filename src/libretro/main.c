// Butterscotch -> core libretro (Android armeabi-v7a, GLES3, rendu matériel).
//
// Cycle de vie calqué sur src/android/main.c de Butterscotch :
//   DataWin_parse -> VM_create -> GLRenderer_create -> Runner_create -> Runner_initFirstRoom
// puis, à chaque frame, les mêmes phases de dessin que stepAndDraw().
//
// La création du runner est différée jusqu'à context_reset : le contexte GL
// n'existe qu'à ce moment-là pour un core en RETRO_ENV_SET_HW_RENDER.
//
// v1 : audio désactivé (NoopAudioSystem) pour ne pas ouvrir de pilote audio
// bloquant ; la sortie PCM vers audio_batch_cb est une étape suivante.

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <GLES3/gl3.h>

#include "libretro.h"

#include "common.h"
#include "data_win.h"
#include "vm.h"
#include "runner.h"
#include "log.h"
#include "overlay_file_system.h"
#include "noop_audio_system.h"
#include "gl/gl_renderer.h"
#include "runner_keyboard.h"
#include "runner_gamepad.h"

// ===[ Callbacks frontend ]===

static retro_environment_t env_cb = NULL;
static retro_video_refresh_t video_cb = NULL;
static retro_audio_sample_batch_t audio_batch_cb = NULL;
static retro_input_poll_t input_poll_cb = NULL;
static retro_input_state_t input_state_cb = NULL;

// ===[ État du contexte matériel ]===

static struct retro_hw_render_callback hw;
static bool hw_ready = false;     // context_reset reçu et contexte GL valide
static bool game_loaded = false;  // retro_load_game a réussi

static char game_path[PATH_MAX];
static char game_dir[PATH_MAX];
static char save_dir[PATH_MAX];

// ===[ État Butterscotch ]===

static DataWin* dataWin = NULL;
static VMContext* vm = NULL;
static Renderer* renderer = NULL;
static Runner* runner = NULL;

// Mémoire de l'état précédent des touches envoyées au clavier GML.
static bool prevKeyDown[GML_KEY_COUNT];

// Seuil de déport analogique pour simuler la croix directionnelle.
#define ANALOG_DEADZONE 0.25f

// ===[ Hooks Runner ]===

static bool libretro_get_window_size(int32_t* outW, int32_t* outH) {
    if (dataWin == NULL) return false;
    if (outW != NULL) *outW = (int32_t) dataWin->gen8.defaultWindowWidth;
    if (outH != NULL) *outH = (int32_t) dataWin->gen8.defaultWindowHeight;
    return true;
}

static void libretro_set_window_title(const char* title) {
    (void) title; // RetroArch affiche le titre lui-même.
}

// ===[ Démarrage / arrêt du runner ]===

static DataWin* parse_data_win(const char* path) {
    return DataWin_parse(
        path,
        (DataWinParserOptions) {
            .parseGen8 = true,
            .parseOptn = true,
            .parseLang = true,
            .parseExtn = true,
            .parseSond = true,
            .parseAgrp = true,
            .parseSprt = true,
            .parseBgnd = true,
            .parsePath = true,
            .parseScpt = true,
            .parseGlob = true,
            .parseShdr = true,
            .parseFont = true,
            .parseTmln = true,
            .parseObjt = true,
            .parseRoom = true,
            .parseTgin = true,
            .parseTpag = true,
            .parseCode = true,
            .parseVari = true,
            .parseFunc = true,
            .parseStrg = true,
            .parseTxtr = true,
            .parseAudo = true,
            .skipLoadingPreciseMasksForNonPreciseSprites = true,
            .loadType = DATAWINLOADTYPE_LOAD_IN_MEMORY_AHEAD_OF_TIME,
            .lazyLoadRooms = false,
            .eagerlyLoadedRooms = NULL
        }
    );
}

static bool start_runner(void) {
    if (runner != NULL) return true;
    if (!hw_ready || dataWin == NULL) return false;

    // Dossier de sauvegarde : <save_dir>/butterscotch, sinon à côté du jeu.
    char savesPath[PATH_MAX];
    const char* base = save_dir[0] != '\0' ? save_dir : game_dir;
    snprintf(savesPath, sizeof(savesPath), "%s/butterscotch", base);
    if (mkdir(savesPath, 0777) != 0 && errno != EEXIST) {
        logWarn("Impossible de créer le dossier de sauvegarde %s", savesPath);
    }

    vm = VM_create(dataWin);
    renderer = GLRenderer_create();
    ((GLModernRenderer*) renderer)->hostFramebuffer = (GLuint) hw.get_current_framebuffer();
    ((GLRenderer*) renderer)->textureFormat = GL_RGBA;

    OverlayFileSystem* overlayFs = OverlayFileSystem_create(game_dir, savesPath);
    AudioSystem* audio = (AudioSystem*) NoopAudioSystem_create();

    runner = Runner_create(dataWin, vm, renderer, (FileSystem*) overlayFs, audio, 0);
    runner->osType = OS_ANDROID;
    runner->setWindowTitle = libretro_set_window_title;
    runner->windowHasFocus = NULL;
    runner->getWindowSize = libretro_get_window_size;

    // Une seule manette virtuelle, branchée d'office (RetroArch gère le pad).
    if (runner->gamepads != NULL) {
        GamepadSlot* slot = &runner->gamepads->slots[0];
        if (!slot->connected) {
            slot->connected = true;
            runner->gamepads->connectedCount++;
        }
        snprintf(slot->description, sizeof(slot->description), "RetroArch");
    }

    Runner_initFirstRoom(runner);
    logInfo("Runner libretro démarré");
    return true;
}

static void stop_runner(void) {
    if (runner == NULL) return;

    runner->audioSystem->vtable->destroy(runner->audioSystem);
    runner->audioSystem = NULL;
    renderer->vtable->destroy(renderer);
    renderer = NULL;

    Runner_free(runner);
    runner = NULL;
    VM_free(vm);
    vm = NULL;
}

// ===[ Entrées ]===

// Chaque bouton logique active à la fois la touche GML et l'index manette correspondant.
// Index manette = gmlButtonToIndex() de runner_gamepad.c : FACE1=0 ... PADR=15.
static void poll_input(void) {
    if (runner == NULL) return;

    RunnerKeyboardState* kb = runner->keyboard;
    RunnerGamepadState* gp = runner->gamepads;

    input_poll_cb();

    bool wantKey[GML_KEY_COUNT];
    memset(wantKey, 0, sizeof(wantKey));
    bool wantPad[GP_BUTTON_COUNT];
    memset(wantPad, 0, sizeof(wantPad));

    // Croix directionnelle.
    bool up    = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP);
    bool down  = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN);
    bool left  = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT);
    bool right = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT);

    // Stick analogique gauche : axes GML + directions simulées.
    float lx = input_state_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X) / 32767.0f;
    float ly = input_state_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y) / 32767.0f;
    float rx = input_state_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X) / 32767.0f;
    float ry = input_state_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y) / 32767.0f;

    if (ly < -ANALOG_DEADZONE) up = true;
    if (ly >  ANALOG_DEADZONE) down = true;
    if (lx < -ANALOG_DEADZONE) left = true;
    if (lx >  ANALOG_DEADZONE) right = true;

    if (up)    { wantKey[VK_UP] = true;    wantPad[12] = true; }
    if (down)  { wantKey[VK_DOWN] = true;  wantPad[13] = true; }
    if (left)  { wantKey[VK_LEFT] = true;  wantPad[14] = true; }
    if (right) { wantKey[VK_RIGHT] = true; wantPad[15] = true; }

    // Boutons : B=Z/Entrée/face1, A=X/Shift/face2, Y=C/Ctrl/face3, X=V/Espace/face4.
    bool b = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B);
    bool a = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A);
    bool y = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y);
    bool x = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X);
    bool l1 = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L);
    bool r1 = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R);
    bool l2 = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2);
    bool r2 = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2);
    bool l3 = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3);
    bool r3 = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3);
    bool start  = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START);
    bool select = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT);

    if (b)  { wantKey['Z'] = true; wantKey[VK_ENTER] = true; wantPad[0] = true; }
    if (a)  { wantKey['X'] = true; wantKey[VK_SHIFT] = true; wantPad[1] = true; }
    if (y)  { wantKey['C'] = true; wantKey[VK_CONTROL] = true; wantPad[2] = true; }
    if (x)  { wantKey['V'] = true; wantKey[VK_SPACE] = true; wantPad[3] = true; }
    if (l1) { wantKey['A'] = true; wantPad[4] = true; }
    if (r1) { wantKey['S'] = true; wantPad[5] = true; }
    if (l2) { wantKey['Q'] = true; wantPad[6] = true; }
    if (r2) { wantKey['W'] = true; wantPad[7] = true; }
    if (select) { wantKey[VK_TAB] = true; wantPad[8] = true; }
    if (start)  { wantKey[VK_ESCAPE] = true; wantPad[9] = true; }
    if (l3) wantPad[10] = true;
    if (r3) wantPad[11] = true;

    // Clavier GML : envoi des transitions uniquement.
    for (int k = 0; k < GML_KEY_COUNT; k++) {
        if (wantKey[k] == prevKeyDown[k]) continue;
        if (wantKey[k]) RunnerKeyboard_onKeyDown(kb, k);
        else RunnerKeyboard_onKeyUp(kb, k);
        prevKeyDown[k] = wantKey[k];
    }

    // Manette GML : boutons (avec fronts pressed/released) et axes.
    if (gp != NULL) {
        GamepadSlot* slot = &gp->slots[0];
        for (int i = 0; i < GP_BUTTON_COUNT; i++) {
            bool isDown = wantPad[i];
            bool wasDown = slot->buttonDown[i];
            if (isDown && !wasDown) slot->buttonPressed[i] = true;
            if (!isDown && wasDown) slot->buttonReleased[i] = true;
            slot->buttonDown[i] = isDown;
            slot->buttonValue[i] = isDown ? 1.0f : 0.0f;
        }
        slot->axisValue[0] = lx;
        slot->axisValue[1] = ly;
        slot->axisValue[2] = rx;
        slot->axisValue[3] = ry;
    }
}

// ===[ Callbacks contexte matériel ]===

static void on_context_reset(void) {
    hw_ready = true;
    start_runner();
}

static void on_context_destroy(void) {
    hw_ready = false;
    stop_runner();
}

// ===[ API Libretro ]===

void retro_set_environment(retro_environment_t cb) {
    env_cb = cb;

    bool noGame = false;
    env_cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &noGame);

    enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
    env_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt);

    static const struct retro_input_descriptor descs[] = {
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "Haut" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "Bas" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "Gauche" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Droite" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "B (Confirmer / Entrée / Z)" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "A (Annuler / Courir / X)" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "Y (Action / C)" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "X (Action / V)" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "L1 (A)" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "R1 (S)" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,    "L2 (Q)" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,    "R2 (W)" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Start (Échap)" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,"Select (Tab)" },
        { 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "Stick gauche X" },
        { 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y, "Stick gauche Y" },
        { 0, 0, 0, 0, NULL }
    };
    env_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, (void*) descs);
}

void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }
void retro_set_audio_sample(retro_audio_sample_t cb) { (void) cb; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }
void retro_set_input_poll(retro_input_poll_t cb) { input_poll_cb = cb; }
void retro_set_input_state(retro_input_state_t cb) { input_state_cb = cb; }

void retro_init(void) {
    memset(&hw, 0, sizeof(hw));
    hw.context_type = RETRO_HW_CONTEXT_OPENGLES3;
    hw.version_major = 3;
    hw.version_minor = 0;
    hw.context_reset = on_context_reset;
    hw.context_destroy = on_context_destroy;
    hw.depth = false;
    hw.stencil = false;
    hw.bottom_left_origin = true;
    hw.cache_context = true;
    hw.debug_context = false;

    if (!env_cb(RETRO_ENVIRONMENT_SET_HW_RENDER, &hw)) {
        logError("Le frontend refuse le rendu OpenGL ES 3 : le core ne peut pas s'afficher");
    }
}

void retro_deinit(void) {
    stop_runner();
    if (dataWin != NULL) {
        DataWin_free(dataWin);
        dataWin = NULL;
    }
    game_loaded = false;
}

unsigned retro_api_version(void) { return RETRO_API_VERSION; }

void retro_get_system_info(struct retro_system_info* info) {
    memset(info, 0, sizeof(*info));
    info->library_name = "Butterscotch";
    info->library_version = "1.0";
    info->valid_extensions = "win|droid|unx|ios";
    info->need_fullpath = true;
    info->block_extract = false;
}

void retro_get_system_av_info(struct retro_system_av_info* info) {
    int32_t w = 640;
    int32_t h = 480;
    if (dataWin != NULL) {
        w = (int32_t) dataWin->gen8.defaultWindowWidth;
        h = (int32_t) dataWin->gen8.defaultWindowHeight;
    }
    info->geometry.base_width = (unsigned) w;
    info->geometry.base_height = (unsigned) h;
    info->geometry.max_width = (unsigned) w;
    info->geometry.max_height = (unsigned) h;
    info->geometry.aspect_ratio = (float) w / (float) h;
    info->timing.fps = 60.0;
    info->timing.sample_rate = 44100.0;
}

void retro_set_controller_port_device(unsigned port, unsigned device) {
    (void) port;
    (void) device;
}

void retro_reset(void) {
    stop_runner();
    start_runner();
}

void retro_run(void) {
    if (!game_loaded) return;
    if (runner == NULL && !start_runner()) return;

    RunnerKeyboard_beginFrame(runner->keyboard);
    RunnerGamepad_beginFrame(runner->gamepads);
    poll_input();

    // Le FBO fourni par RetroArch change selon le frontend : on le relit à chaque frame.
    GLuint hostFbo = (GLuint) hw.get_current_framebuffer();
    ((GLModernRenderer*) renderer)->hostFramebuffer = hostFbo;

    int32_t winW = (int32_t) dataWin->gen8.defaultWindowWidth;
    int32_t winH = (int32_t) dataWin->gen8.defaultWindowHeight;
    if (winW < 1) winW = 1;
    if (winH < 1) winH = 1;

    Runner_step(runner);

    // Caméra libre désactivée : identité.
    runner->freeCamPanX = 0.0f;
    runner->freeCamPanY = 0.0f;
    runner->freeCamZoom = 1.0f;

    // Même logique de taille d'application que stepAndDraw().
    if (!runner->appSurfaceEnabled) {
        runner->applicationWidth = winW;
        runner->applicationHeight = winH;
        runner->usingAppSurface = false;
    } else {
        if (runner->applicationWidth <= 0 || runner->applicationHeight <= 0) {
            runner->applicationWidth = (int32_t) dataWin->gen8.defaultWindowWidth;
            runner->applicationHeight = (int32_t) dataWin->gen8.defaultWindowHeight;
        }
        runner->usingAppSurface = true;
    }

    int32_t gameW = runner->applicationWidth;
    int32_t gameH = runner->applicationHeight;

    runner->widescreenExtraWidth = 0;
    runner->widescreenExtraHeight = 0;

    glBindFramebuffer(GL_FRAMEBUFFER, hostFbo);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    Runner_drawPre(runner, winW, winH);
    Runner_beginFrame(runner, gameW, gameH, winW, winH, winW, winH);
    Runner_drawViews(runner, gameW, gameH, false);
    runner->renderer->vtable->endFrameInit(runner->renderer);
    Runner_drawPost(runner, winW, winH);
    runner->renderer->vtable->endFrameEnd(runner->renderer);
    Runner_drawGUI(runner, winW, winH, gameW, gameH);
    Runner_handlePendingRoomChange(runner);

    // Frame déjà écrite dans le FBO du frontend.
    video_cb(RETRO_HW_FRAME_BUFFER_VALID, (unsigned) winW, (unsigned) winH, 0);
}

bool retro_load_game(const struct retro_game_info* info) {
    if (info == NULL || info->path == NULL) {
        logError("retro_load_game: aucun chemin de jeu");
        return false;
    }

    snprintf(game_path, sizeof(game_path), "%s", info->path);

    // Répertoire du jeu : les assets voisins (.ogg, .ini, etc.) sont relatifs à data.win.
    snprintf(game_dir, sizeof(game_dir), "%s", game_path);
    char* slash = strrchr(game_dir, '/');
    if (slash != NULL) {
        *slash = '\0';
    } else {
        snprintf(game_dir, sizeof(game_dir), ".");
    }
    if (chdir(game_dir) != 0) {
        logWarn("Impossible de se placer dans %s", game_dir);
    }

    // Sauvegardes dans <save_dir>/butterscotch/, fourni par le frontend.
    const char* saveDirFromFrontend = NULL;
    if (env_cb != NULL && env_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &saveDirFromFrontend) && saveDirFromFrontend != NULL) {
        snprintf(save_dir, sizeof(save_dir), "%s", saveDirFromFrontend);
    } else {
        save_dir[0] = '\0';
    }

    if (dataWin != NULL) {
        DataWin_free(dataWin);
        dataWin = NULL;
    }
    dataWin = parse_data_win(game_path);
    if (dataWin == NULL) {
        logError("Échec du chargement de %s", game_path);
        return false;
    }

    enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
    env_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt);

    game_loaded = true;
    // Si le contexte GL existe déjà, le runner démarre maintenant ; sinon ce sera au context_reset.
    start_runner();
    return true;
}

bool retro_load_game_special(unsigned type, const struct retro_game_info* info, size_t num) {
    (void) type;
    (void) num;
    return retro_load_game(info);
}

void retro_unload_game(void) {
    stop_runner();
    if (dataWin != NULL) {
        DataWin_free(dataWin);
        dataWin = NULL;
    }
    game_loaded = false;
}

unsigned retro_get_region(void) { return RETRO_REGION_NTSC; }

void* retro_get_memory_data(unsigned id) {
    (void) id;
    return NULL;
}

size_t retro_get_memory_size(unsigned id) {
    (void) id;
    return 0;
}

size_t retro_serialize_size(void) { return 0; }
bool retro_serialize(void* data, size_t size) {
    (void) data;
    (void) size;
    return false;
}
bool retro_unserialize(const void* data, size_t size) {
    (void) data;
    (void) size;
    return false;
}

void retro_cheat_reset(void) {}
void retro_cheat_set(unsigned index, bool enabled, const char* code) {
    (void) index;
    (void) enabled;
    (void) code;
}
