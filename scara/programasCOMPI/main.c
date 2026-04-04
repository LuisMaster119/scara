#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lexer.h"
#include "parser.h"
#include "simbolos.h"
#include "errores.h"
#include "vm.h"
#include "cinematica.h"

// --- Leer archivo completo a string ----------------------

char* leer_archivo(const char* ruta) {
    FILE* f = fopen(ruta, "rb");
    if (!f) {
        fprintf(stderr, "Error: no se puede abrir '%s'\n", ruta);
        exit(1);
    }

    fseek(f, 0, SEEK_END);
    long tam = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* contenido = (char*)malloc(tam + 1);
    if (!contenido) {
        fprintf(stderr, "Error: memoria insuficiente\n");
        exit(1);
    }
    size_t leidos = fread(contenido, 1, tam, f);
    contenido[leidos] = '\0';
    fclose(f);

    return contenido;
}

// --- Main ------------------------------------------------

static int ejecutar_ik_selftest(void) {
    const int tol_deg = 2;
    int total = 0;
    int ok = 0;
    int fail = 0;
    const int modos[2] = { CIN_MODO_CODO_ARRIBA, CIN_MODO_CODO_ABAJO };
    const char* modo_txt[2] = { "UP", "DOWN" };

    printf("=== IK SELFTEST C vs ASM ===\n");
    for (int x = -350; x <= 350; x += 25) {
        for (int y = -350; y <= 350; y += 25) {
            if (!cinematica_xy_en_alcance(x, y)) continue;

            for (int m = 0; m < 2; m++) {
                int ok_c = 0, ok_asm = 0;
                int q1_c = 0, q2_c = 0;
                int q1_asm = 0, q2_asm = 0;

                total++;
                if (cinematica_comparar_c_vs_asm_modo(x, y, modos[m], tol_deg,
                                                      &ok_c, &ok_asm,
                                                      &q1_c, &q2_c,
                                                      &q1_asm, &q2_asm)) {
                    ok++;
                } else {
                    fail++;
                    printf("[IK MISMATCH %s] XY=(%d,%d) C=(%d,%d) ASM=(%d,%d) okC=%d okASM=%d\n",
                           modo_txt[m], x, y, q1_c, q2_c, q1_asm, q2_asm, ok_c, ok_asm);
                }
            }
        }
    }

    printf("[IK SELFTEST] total=%d ok=%d fail=%d\n", total, ok, fail);
    return (fail == 0) ? 0 : 1;
}

int main(int argc, char* argv[]) {

    if (argc >= 2 && strcmp(argv[1], "--ik-selftest") == 0) {
        return ejecutar_ik_selftest();
    }

    // verificar argumento
    if (argc < 2) {
        fprintf(stderr, "Uso: scara.exe <archivo.scara>\n");
        fprintf(stderr, "Ejemplo: scara.exe programa.scara\n");
        return 1;
    }

    // leer archivo fuente
    char* programa = leer_archivo(argv[1]);

    // inicializar sistema de errores con el codigo fuente
    error_init(programa);

    printf("=== COMPILANDO: %s ===\n", argv[1]);

    // inicializar lexer
    Lexer lex;
    lexer_init(&lex, programa);

    // inicializar parser y compilar
    Parser parser;
    parser_init(&parser, &lex);
    parsear_programa(&parser);

    // mostrar tabla de simbolos
    printf("\n=== TABLA DE SIMBOLOS ===\n");
    tabla_imprimir();

    // mostrar bytecode generado
    printf("\n=== BYTECODE GENERADO ===\n");
        printf("%-4s %-14s %-6s %-6s %-6s %-5s %-14s %-14s %-14s\n",
            "PC", "OPCODE", "ARG1", "ARG2", "ARG3", "FLAGS", "SVAL", "SVAL2", "SVAL3");
        printf("-----------------------------------------------------------------------------------------\n");

    for (int i = 0; i < bytecode_len; i++) {
        Instruccion* ins = &bytecode[i];
         printf("%-4d %-14s %-6d %-6d %-6d %-5d %-14s %-14s %-14s\n",
             i, opcode_a_texto(ins->opcode),
               ins->arg1, ins->arg2, ins->arg3,
             ins->flags,
             ins->sval,
             ins->sval2,
             ins->sval3);
    }

        printf("\n=== EJECUCION VM (FASE 3) ===\n");
        if (vm_ejecutar(bytecode, bytecode_len, 1) != 0) {
            free(programa);
            return 1;
        }

    free(programa);
    return 0;
}
