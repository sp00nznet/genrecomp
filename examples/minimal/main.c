/*
 * Minimal genrecomp example.
 *
 * Demonstrates the basic lifecycle:
 *   1. Initialize genrecomp (creates window, sets up hardware)
 *   2. Load a Genesis ROM
 *   3. Register recompiled functions
 *   4. Run the main loop (begin_frame / recompiled code / end_frame)
 *   5. Shutdown
 *
 * This example doesn't include any actual recompiled game code —
 * it just shows the API in action.
 */

#include <genrecomp/genrecomp.h>
#include <stdio.h>

/* Example: a simple recompiled function that clears RAM */
static void func_clear_ram(void) {
    uint8_t *ram = bus_get_ram();
    if (ram) {
        for (int i = 0; i < 0x10000; i++) {
            ram[i] = 0;
        }
    }
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    printf("genrecomp minimal example\n");
    printf("========================\n\n");

    /* Initialize */
    if (!genrecomp_init("genrecomp - Minimal Example", 3)) {
        fprintf(stderr, "Failed to initialize genrecomp\n");
        return 1;
    }

    /* Register a recompiled function at a fake M68K address */
    func_table_register(0x000200, func_clear_ram);

    /* Optionally load a ROM (not required for this demo) */
    if (argc > 1) {
        if (!genrecomp_load_rom(argv[1])) {
            fprintf(stderr, "Failed to load ROM: %s\n", argv[1]);
        }
    }

    printf("Running main loop (press Escape or close window to quit)...\n\n");

    /* Main loop */
    int frame = 0;
    while (genrecomp_begin_frame()) {
        /* In a real recomp project, you'd call recompiled functions here:
         *   func_table_call(0x000200);  // call the function at M68K $000200
         *   func_table_call(0x001234);  // etc.
         */

        if (frame == 0) {
            /* Call our example function on the first frame */
            func_table_call(0x000200);
            printf("Frame 0: called func_clear_ram at $000200\n");
            printf("  D0=$%08X A0=$%08X SR=$%04X\n",
                   g_m68k.d[0], g_m68k.a[0], m68k_get_sr());
        }

        genrecomp_end_frame();
        frame++;
    }

    printf("\nRan %d frames. Shutting down.\n", frame);

    /* Shutdown */
    genrecomp_shutdown();
    return 0;
}
