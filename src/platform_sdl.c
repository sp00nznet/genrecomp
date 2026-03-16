/*
 * SDL2 platform layer — windowing, framebuffer presentation, audio output.
 */

#include "genrecomp/platform.h"

#include <SDL.h>
#include <stdio.h>
#include <string.h>

static SDL_Window   *s_window   = NULL;
static SDL_Renderer *s_renderer = NULL;
static SDL_Texture  *s_texture  = NULL;
static SDL_AudioDeviceID s_audio_dev = 0;
static uint64_t s_frame_start = 0;
static bool s_initialized = false;

bool platform_init(const char *window_title, int scale) {
    if (s_initialized) return true;

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
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) return false;
        if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) return false;
    }
    return true;
}

void platform_frame_sync(void) {
    /* VSync handles timing when renderer has PRESENTVSYNC.
     * This provides a fallback for non-vsync cases. */
    uint64_t now = SDL_GetPerformanceCounter();
    uint64_t freq = SDL_GetPerformanceFrequency();
    uint64_t target = s_frame_start + freq / 60;

    while (SDL_GetPerformanceCounter() < target) {
        SDL_Delay(0);
    }

    s_frame_start = SDL_GetPerformanceCounter();
}

void platform_shutdown(void) {
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
