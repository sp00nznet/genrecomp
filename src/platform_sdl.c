/*
 * SDL2 platform layer — windowing, framebuffer presentation, audio output.
 */

#include "genrecomp/platform.h"
#include "genrecomp/bus.h"

#include <SDL.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static SDL_Window   *s_window   = NULL;
static SDL_Renderer *s_renderer = NULL;
static SDL_Texture  *s_texture  = NULL;
static SDL_AudioDeviceID s_audio_dev = 0;
static uint64_t s_frame_start = 0;
static bool s_initialized = false;

/* Headless/record options (see platform_parse_args). Headless exists so a
 * recomp can run where no window may open (over RDP, in CI): frames go to
 * ffmpeg instead of a screen, and scripted presses stand in for a player. */
static bool s_headless = false;
static const char *s_record_path = NULL;
static FILE *s_record = NULL;
static long s_max_frames = 0;   /* 0 = unlimited */
static long s_frame_no = 0;
#define MAX_PRESSES 64
static struct { long start, len; uint16_t buttons; } s_presses[MAX_PRESSES];
static int s_press_count = 0;
static const char *s_ram_dir = NULL;  /* --ram-dump DIR:EVERY */
static long s_ram_every = 60;

static uint16_t parse_buttons(const char *s) {
    static const struct { const char *name; uint16_t bit; } map[] = {
        {"UP",0x0001},{"DOWN",0x0002},{"LEFT",0x0004},{"RIGHT",0x0008},
        {"B",0x0010},{"C",0x0020},{"A",0x0040},{"START",0x0080},
        {"X",0x0100},{"Y",0x0200},{"Z",0x0400},{"MODE",0x0800}};
    uint16_t b = 0;
    char buf[128];
    strncpy(buf, s, sizeof(buf) - 1); buf[sizeof(buf) - 1] = 0;
    for (char *tok = strtok(buf, "+"); tok; tok = strtok(NULL, "+"))
        for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++)
            if (!strcmp(tok, map[i].name)) b |= map[i].bit;
    return b;
}

int platform_parse_args(int argc, char **argv) {
    int out = 1;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--headless")) s_headless = true;
        else if (!strcmp(argv[i], "--record") && i + 1 < argc) s_record_path = argv[++i];
        else if (!strcmp(argv[i], "--ram-dump") && i + 1 < argc) {
            /* DIR[:EVERY] -- writes DIR/ram_NNNNNN.bin (64KB work RAM) */
            static char dir[512];
            strncpy(dir, argv[++i], sizeof(dir) - 1);
            char *colon = strrchr(dir, ':');
            if (colon && colon > dir + 1) { *colon = 0; s_ram_every = atol(colon + 1); }
            if (s_ram_every < 1) s_ram_every = 1;
            s_ram_dir = dir;
        }
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) s_max_frames = atol(argv[++i]);
        else if (!strcmp(argv[i], "--press") && i + 1 < argc) {
            /* FRAME:BUTTONS[:LEN], e.g. 600:START or 900:A+RIGHT:30 */
            char name[64] = {0};
            long f = 0, len = 6;
            if (sscanf(argv[++i], "%ld:%63[^:]:%ld", &f, name, &len) >= 2 &&
                s_press_count < MAX_PRESSES) {
                s_presses[s_press_count].start = f;
                s_presses[s_press_count].len = len;
                s_presses[s_press_count].buttons = parse_buttons(name);
                s_press_count++;
            }
        }
        else argv[out++] = argv[i];
    }
    argv[out] = NULL;
    return out;
}

bool platform_is_headless(void) { return s_headless; }
long platform_frame_number(void) { return s_frame_no; }

uint16_t platform_scripted_buttons(void) {
    uint16_t b = 0;
    for (int i = 0; i < s_press_count; i++)
        if (s_frame_no >= s_presses[i].start &&
            s_frame_no < s_presses[i].start + s_presses[i].len)
            b |= s_presses[i].buttons;
    return b;
}

bool platform_init(const char *window_title, int scale) {
    if (s_initialized) return true;

    if (s_record_path) {
        char cmd[1024];
        /* GPGX renders XRGB8888 little-endian: bytes are B,G,R,X */
        snprintf(cmd, sizeof(cmd),
            "ffmpeg -loglevel error -y -f rawvideo -pix_fmt bgr0 -s %dx%d -r 60 -i - "
            "-vf scale=iw*3:ih*3:flags=neighbor -c:v libx264 -pix_fmt yuv420p \"%s\"",
            GEN_RENDER_WIDTH, GEN_RENDER_HEIGHT, s_record_path);
#ifdef _WIN32
        s_record = _popen(cmd, "wb");
#else
        s_record = popen(cmd, "w");
#endif
        if (!s_record) fprintf(stderr, "platform: could not start ffmpeg for --record\n");
    }

    if (s_headless) {
        /* No window, no audio device, no vsync: run as fast as the CPU allows */
        if (SDL_Init(0) < 0) {
            fprintf(stderr, "platform: SDL_Init failed: %s\n", SDL_GetError());
            return false;
        }
        s_initialized = true;
        return true;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) < 0) {
        fprintf(stderr, "platform: SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    int w = GEN_RENDER_WIDTH * scale;
    int h = GEN_RENDER_HEIGHT * scale;

    s_window = SDL_CreateWindow(
        window_title,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        w, h,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );
    if (!s_window) {
        fprintf(stderr, "platform: SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    s_renderer = SDL_CreateRenderer(s_window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!s_renderer) {
        fprintf(stderr, "platform: SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(s_window);
        SDL_Quit();
        return false;
    }

    /* Set logical size for aspect-ratio-correct scaling */
    SDL_RenderSetLogicalSize(s_renderer, GEN_RENDER_WIDTH, GEN_RENDER_HEIGHT);

    s_texture = SDL_CreateTexture(s_renderer,
        SDL_PIXELFORMAT_RGBX8888,
        SDL_TEXTUREACCESS_STREAMING,
        GEN_RENDER_WIDTH, GEN_RENDER_HEIGHT);
    if (!s_texture) {
        fprintf(stderr, "platform: SDL_CreateTexture failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(s_renderer);
        SDL_DestroyWindow(s_window);
        SDL_Quit();
        return false;
    }

    /* Audio setup */
    SDL_AudioSpec want = {0};
    want.freq = 44100;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = NULL; /* Using queue-based audio */

    SDL_AudioSpec have;
    s_audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (s_audio_dev > 0) {
        SDL_PauseAudioDevice(s_audio_dev, 0);
    }

    s_frame_start = SDL_GetPerformanceCounter();
    s_initialized = true;
    return true;
}

void platform_present_frame(const uint8_t *framebuffer) {
    if (s_record)
        fwrite(framebuffer, 4, GEN_RENDER_WIDTH * GEN_RENDER_HEIGHT, s_record);
    if (s_ram_dir && s_frame_no % s_ram_every == 0) {
        char path[600];
        snprintf(path, sizeof(path), "%s/ram_%06ld.bin", s_ram_dir, s_frame_no);
        FILE *f = fopen(path, "wb");
        if (f) { bus_dump_state(f); fclose(f); }
    }
    s_frame_no++;
    if (s_max_frames && s_frame_no >= s_max_frames) {
        printf("platform: reached --frames %ld, exiting\n", s_max_frames);
        platform_shutdown();
        exit(0);
    }
    if (!s_texture || !s_renderer) return;

    SDL_UpdateTexture(s_texture, NULL, framebuffer, GEN_RENDER_WIDTH * 4);
    SDL_RenderClear(s_renderer);
    SDL_RenderCopy(s_renderer, s_texture, NULL, NULL);
    SDL_RenderPresent(s_renderer);
}

void platform_queue_audio(const int16_t *samples, int sample_count) {
    if (s_audio_dev > 0 && samples) {
        SDL_QueueAudio(s_audio_dev, samples, (uint32_t)(sample_count * 2 * sizeof(int16_t)));
    }
}

bool platform_poll_events(void) {
    SDL_Event ev;
    if (s_headless) return true;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) return false;
        if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) return false;
    }
    return true;
}

void platform_frame_sync(void) {
    /* VSync handles timing when renderer has PRESENTVSYNC.
     * This provides a fallback for non-vsync cases. */
    if (s_headless) return;
    uint64_t now = SDL_GetPerformanceCounter();
    uint64_t freq = SDL_GetPerformanceFrequency();
    uint64_t target = s_frame_start + freq / 60;

    while (SDL_GetPerformanceCounter() < target) {
        SDL_Delay(0);
    }

    s_frame_start = SDL_GetPerformanceCounter();
}

void platform_shutdown(void) {
    if (s_record) {
#ifdef _WIN32
        _pclose(s_record);
#else
        pclose(s_record);
#endif
        s_record = NULL;
    }
    if (!s_initialized) return;

    if (s_audio_dev > 0) {
        SDL_CloseAudioDevice(s_audio_dev);
        s_audio_dev = 0;
    }
    if (s_texture)  { SDL_DestroyTexture(s_texture);   s_texture  = NULL; }
    if (s_renderer) { SDL_DestroyRenderer(s_renderer); s_renderer = NULL; }
    if (s_window)   { SDL_DestroyWindow(s_window);     s_window   = NULL; }

    SDL_Quit();
    s_initialized = false;
}
