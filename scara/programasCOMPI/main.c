#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lexer.h"
#include "parser.h"
#include "simbolos.h"
#include "errores.h"
#include "optimizador.h"
#include "ensamblador.h"
#include "cinematica.h"

// ── leer archivo ──────────────────────────────────────────
char* leer_archivo(const char* ruta) {
    FILE* f = fopen(ruta, "rb");
    if (!f) { fprintf(stderr, "Error: no se puede abrir '%s'\n", ruta); exit(1); }
    fseek(f, 0, SEEK_END);
    long tam = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = malloc(tam + 1);
    if (!buf) { fprintf(stderr, "Error: memoria insuficiente\n"); exit(1); }
    buf[fread(buf, 1, tam, f)] = '\0';
    fclose(f);
    return buf;
}

// ── main ──────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    OutputMode modo = OUTPUT_SDL2;   // default
    const char* ruta_programa = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            i++;
            if      (strcmp(argv[i], "sdl2")  == 0) modo = OUTPUT_SDL2;
            else if (strcmp(argv[i], "ascii") == 0) modo = OUTPUT_ASCII;
            else {
                fprintf(stderr, "Error: modo desconocido '%s'\n", argv[i]);
                fprintf(stderr, "Modos validos: sdl2 | ascii\n");
                return 1;
            }
        } else if (argv[i][0] != '-') {
            ruta_programa = argv[i];
        } else {
            fprintf(stderr, "Error: opcion no reconocida: %s\n", argv[i]);
            return 1;
        }
    }

    if (!ruta_programa) {
        fprintf(stderr, "Uso: scara.exe --output [sdl2|ascii] <archivo.scara>\n");
        return 1;
    }

    // ── 1. Leer fuente ──
    char* programa = leer_archivo(ruta_programa);
    error_init(programa);
    printf("=== COMPILANDO: %s ===\n", ruta_programa);

    // ── 2. Lexer + Parser ──
    Lexer lex;
    lexer_init(&lex, programa);
    Parser parser;
    parser_init(&parser, &lex);
    parsear_programa(&parser);

    printf("\n=== TABLA DE SIMBOLOS ===\n");
    tabla_imprimir();

    printf("\n=== BYTECODE GENERADO ===\n");
    for (int i = 0; i < bytecode_len; i++) {
        Instruccion* ins = &bytecode[i];
        printf("%-4d %-14s %-6d %-6d %-6d %-5d %-14s %-14s %-14s\n",
               i, opcode_a_texto(ins->opcode),
               ins->arg1, ins->arg2, ins->arg3, ins->flags,
               ins->sval, ins->sval2, ins->sval3);
    }

    // ── 3. Optimizar ──
    Instruccion bytecode_opt[MAX_INSTRUCCIONES];
    int opt_len = optimizar_bytecode(bytecode, bytecode_len,
                                     bytecode_opt, MAX_INSTRUCCIONES);
    if (opt_len < 0) {
        fprintf(stderr, "Error: fallo en optimizacion\n");
        free(programa); return 1;
    }

    printf("\n=== BYTECODE OPTIMIZADO ===\n");
    for (int i = 0; i < opt_len; i++) {
        Instruccion* ins = &bytecode_opt[i];
        printf("%-4d %-14s %-6d %-6d %-6d %-5d %-14s %-14s %-14s\n",
               i, opcode_a_texto(ins->opcode),
               ins->arg1, ins->arg2, ins->arg3, ins->flags,
               ins->sval, ins->sval2, ins->sval3);
    }

    // ── 4. Generar .asm ──
    char ruta_asm[512];
    const char* ext = strrchr(ruta_programa, '.');
    if (ext) {
        size_t n = ext - ruta_programa;
        if (n >= sizeof(ruta_asm)) n = sizeof(ruta_asm) - 1;
        memcpy(ruta_asm, ruta_programa, n);
        ruta_asm[n] = '\0';
    } else {
        snprintf(ruta_asm, sizeof(ruta_asm), "%s", ruta_programa);
    }
    strcat(ruta_asm, ".asm");

    if (generar_ensamblador(bytecode_opt, opt_len, ruta_asm, modo) != 0) {
        fprintf(stderr, "Error: no se pudo generar '%s'\n", ruta_asm);
        free(programa); return 1;
    }
    printf("\n=== ASM GENERADO: %s (modo: %s) ===\n",
           ruta_asm, modo == OUTPUT_SDL2 ? "sdl2" : "ascii");

    // ── 5. Ensamblar con NASM ──
    char ruta_obj[512];
    snprintf(ruta_obj, sizeof(ruta_obj), "%.*s.obj",
             (int)(strlen(ruta_asm) - 4), ruta_asm);

    char cmd_nasm[1024];
    snprintf(cmd_nasm, sizeof(cmd_nasm),
             "nasm -f win64 \"%s\" -o \"%s\"", ruta_asm, ruta_obj);
    printf("\n=== ENSAMBLANDO: %s ===\n", cmd_nasm);
    if (system(cmd_nasm) != 0) {
        fprintf(stderr, "Error: nasm fallo\n");
        free(programa); return 1;
    }

    // ── 6. Enlazar con ld ──
    char ruta_exe[512];
    snprintf(ruta_exe, sizeof(ruta_exe), "%.*s.exe",
             (int)(strlen(ruta_asm) - 4), ruta_asm);

    char cmd_ld[1024];
    if (modo == OUTPUT_ASCII) {
        // ASCII solo necesita kernel32
        snprintf(cmd_ld, sizeof(cmd_ld),
                 "ld \"%s\" -o \"%s\" -lkernel32 "
                 "-L\"C:/msys64/mingw64/lib\" "
                 "--subsystem console",
                 ruta_obj, ruta_exe);
    } else {
        // SDL2 necesita SDL2 + mingw runtime
        snprintf(cmd_ld, sizeof(cmd_ld),
                 "ld \"%s\" -o \"%s\" -lSDL2 -lSDL2main -lmingw32 "
                 "-L\"C:/msys64/mingw64/lib\" "
                 "--subsystem console",
                 ruta_obj, ruta_exe);
    }
    printf("\n=== ENLAZANDO: %s ===\n", cmd_ld);
    if (system(cmd_ld) != 0) {
        fprintf(stderr, "Error: ld fallo\n");
        free(programa); return 1;
    }

    // ── 7. Ejecutar el binario generado ──
    printf("\n=== EJECUTANDO: %s ===\n", ruta_exe);
    char cmd_run[512];
    snprintf(cmd_run, sizeof(cmd_run), "\"%s\"", ruta_exe);
    system(cmd_run);

    free(programa);
    return 0;
}