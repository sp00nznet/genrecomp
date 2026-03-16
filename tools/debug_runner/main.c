/*
 * genrecomp debug runner.
 *
 * Standalone executable for debugging recompiled Genesis games:
 *   1. Links genrecomp with GENRECOMP_DEBUG=1
 *   2. Loads ROM + recompiled function table
 *   3. Starts GDB stub on configurable TCP port
 *   4. Supports stepping, breakpoints, register/memory inspection
 *
 * Usage:
 *   genrecomp_debug_runner <rom.md> [--gdb-port 9001] [--trace]
 */

#include <genrecomp/genrecomp.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef GENRECOMP_DEBUG

static void print_usage(const char *prog) {
    printf("Usage: %s <rom.md> [options]\n", prog);
    printf("Options:\n");
    printf("  --gdb-port <port>   Start GDB stub on TCP port (default: 9001)\n");
    printf("  --trace             Enable function execution tracing\n");
    printf("  --break <addr>      Add breakpoint at M68K address (hex)\n");
    printf("  --frames <count>    Run for N frames then exit\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char *rom_path = argv[1];
    uint16_t gdb_port = 9001;
    bool trace = false;
    int max_frames = 0;

    /* Parse arguments */
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--gdb-port") == 0 && i + 1 < argc) {
            gdb_port = (uint16_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--trace") == 0) {
            trace = true;
        } else if (strcmp(argv[i], "--break") == 0 && i + 1 < argc) {
            uint32_t addr = (uint32_t)strtoul(argv[++i], NULL, 16);
            debug_add_breakpoint(addr);
        } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            max_frames = atoi(argv[++i]);
        }
    }

    printf("genrecomp debug runner\n");
    printf("======================\n\n");

    /* Initialize */
    if (!genrecomp_init("genrecomp Debug Runner", 3)) {
        fprintf(stderr, "Failed to initialize genrecomp\n");
        return 1;
    }

    /* Initialize debug system */
    debug_init();
    debug_set_trace_enabled(trace);

    /* Load ROM */
    if (!genrecomp_load_rom(rom_path)) {
        fprintf(stderr, "Failed to load ROM: %s\n", rom_path);
        genrecomp_shutdown();
        return 1;
    }

    /* Dump initial CPU state */
    printf("\nInitial state after ROM load:\n");
    debug_dump_cpu();
    printf("\n");

    /* Start GDB stub */
    if (gdb_port > 0) {
        debug_gdb_start(gdb_port);
    }

    /* Main loop */
    int frame = 0;
    while (genrecomp_begin_frame()) {
        /* Poll GDB for commands */
        debug_gdb_poll();

        /* Run recompiled frame code here (loaded from function table) */

        genrecomp_end_frame();
        frame++;

        if (max_frames > 0 && frame >= max_frames) {
            printf("\nReached frame limit (%d frames).\n", max_frames);
            break;
        }
    }

    printf("\nFinal state after %d frames:\n", frame);
    debug_dump_cpu();

    /* Cleanup */
    debug_gdb_stop();
    debug_shutdown();
    genrecomp_shutdown();
    return 0;
}

#else /* !GENRECOMP_DEBUG */

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    fprintf(stderr, "Error: debug runner must be compiled with GENRECOMP_DEBUG=1\n");
    return 1;
}

#endif
