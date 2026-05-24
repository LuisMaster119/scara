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
    if (argc < 2) {
        fprintf(stderr, "Uso: scara.exe <archivo.scara>\n");
        fprintf(stderr, "Ejemplo: scara.exe programa.scara\n");
        return 1;
    }
    const char* ruta_programa = argv[1];
    int modo_servidor = (argc >= 3 && strcmp(argv[2], "--no-run") == 0);

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

    /* Si hubo errores de compilacion, mostrar tablas y salir */
    if (error_hubo()) {
        printf("\n=== TABLA DE SIMBOLOS ===\n");
        tabla_imprimir();
        printf("\n=== BYTECODE GENERADO ===\n");
        printf("%-4s %-14s %-6s %-6s %-6s %-5s %-14s %-14s %-14s\n",
               "PC", "OPCODE", "ARG1", "ARG2", "ARG3", "FLAGS", "SVAL", "SVAL2", "SVAL3");
        printf("-----------------------------------------------------------------------------------------\n");
        for (int i = 0; i < bytecode_len; i++) {
            Instruccion* ins = &bytecode[i];
            printf("%-4d %-14s %-6d %-6d %-6d %-5d %-14s %-14s %-14s\n",
                   i, opcode_a_texto(ins->opcode),
                   ins->arg1, ins->arg2, ins->arg3, ins->flags,
                   ins->sval, ins->sval2, ins->sval3);
        }
        free(programa);
        return 1;
    }

    printf("\n=== TABLA DE SIMBOLOS ===\n");
    tabla_imprimir();

    printf("\n=== BYTECODE GENERADO ===\n");
    printf("%-4s %-14s %-6s %-6s %-6s %-5s %-14s %-14s %-14s\n",
           "PC", "OPCODE", "ARG1", "ARG2", "ARG3", "FLAGS", "SVAL", "SVAL2", "SVAL3");
    printf("-----------------------------------------------------------------------------------------\n");
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
    printf("%-4s %-14s %-6s %-6s %-6s %-5s %-14s %-14s %-14s\n",
           "PC", "OPCODE", "ARG1", "ARG2", "ARG3", "FLAGS", "SVAL", "SVAL2", "SVAL3");
    printf("-----------------------------------------------------------------------------------------\n");
    for (int i = 0; i < opt_len; i++) {
        Instruccion* ins = &bytecode_opt[i];
        printf("%-4d %-14s %-6d %-6d %-6d %-5d %-14s %-14s %-14s\n",
               i, opcode_a_texto(ins->opcode),
               ins->arg1, ins->arg2, ins->arg3, ins->flags,
               ins->sval, ins->sval2, ins->sval3);
    }

    // ── 4. Generar .asm completo (datos de traza + SDL2 en NASM puro) ──
    char base[512];
    {
        const char* ext = strrchr(ruta_programa, '.');
        if (ext) {
            size_t n = (size_t)(ext - ruta_programa);
            if (n >= sizeof(base)) n = sizeof(base) - 1;
            memcpy(base, ruta_programa, n);
            base[n] = '\0';
        } else {
            snprintf(base, sizeof(base), "%s", ruta_programa);
        }
    }

    char ruta_asm[512];
    snprintf(ruta_asm, sizeof(ruta_asm), "%s.asm", base);
    char ruta_exe[512];
    snprintf(ruta_exe, sizeof(ruta_exe), "%s.exe", base);

    if (generar_ensamblador(bytecode_opt, opt_len, ruta_asm) != 0) {
        fprintf(stderr, "Error: no se pudo generar '%s'\n", ruta_asm);
        free(programa); return 1;
    }
    printf("\n=== ASM COMPLETO GENERADO: %s ===\n", ruta_asm);

    // ── 5. Ensamblar .asm → .obj con NASM ──
    char ruta_obj[512];
    snprintf(ruta_obj, sizeof(ruta_obj), "%.*s.obj", (int)(strlen(ruta_asm)-4), ruta_asm);

    char cmd_nasm[1024];
    snprintf(cmd_nasm, sizeof(cmd_nasm),
             "nasm -f win64 \"%s\" -o \"%s\"", ruta_asm, ruta_obj);
    printf("\n=== ENSAMBLANDO: %s ===\n", cmd_nasm);
    if (system(cmd_nasm) != 0) {
        fprintf(stderr, "Error: NASM fallo\n");
        free(programa); return 1;
    }

    // ── 6. Enlazar .obj → .exe con GCC + SDL2 (sin _vis.c) ──
    char cmd_ld[1024];
    snprintf(cmd_ld, sizeof(cmd_ld),
             "gcc \"%s\" -o \"%s\" "
             "-L\"C:/msys64/mingw64/lib\" "
             "-lSDL2 -lSDL2_ttf -lmingw32 -lm "
             "-mwindows",
             ruta_obj, ruta_exe);
    printf("\n=== ENLAZANDO: %s ===\n", cmd_ld);
    if (system(cmd_ld) != 0) {
        fprintf(stderr, "Error: enlazado fallo\n");
        free(programa); return 1;
    }

    // ── 7. Ejecutar (solo si no viene del servidor) ──
    if (!modo_servidor) {
        printf("\n=== EJECUTANDO: %s ===\n", ruta_exe);
        char cmd_run[512];
        snprintf(cmd_run, sizeof(cmd_run), "\"%s\"", ruta_exe);
        system(cmd_run);
    }

    free(programa);
    return 0;
}
