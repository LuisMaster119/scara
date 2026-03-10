#include <stdio.h>
#include <stdlib.h>
#include "lexer.h"
#include "parser.h"
#include "simbolos.h"
#include "errores.h"

// --- Leer archivo completo a string ----------------------

char* leer_archivo(const char* ruta) {
    FILE* f = fopen(ruta, "r");
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
    fread(contenido, 1, tam, f);
    contenido[tam] = '\0';
    fclose(f);

    return contenido;
}

// --- Main ------------------------------------------------

int main(int argc, char* argv[]) {

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
    printf("%-4s %-12s %-6s %-6s %-6s %s\n",
           "PC", "OPCODE", "ARG1", "ARG2", "ARG3", "SVAL");
    printf("-----------------------------------------\n");

    for (int i = 0; i < bytecode_len; i++) {
        Instruccion* ins = &bytecode[i];
        printf("%-4d %-12d %-6d %-6d %-6d %s\n",
               i, ins->opcode,
               ins->arg1, ins->arg2, ins->arg3,
               ins->sval);
    }

    free(programa);
    return 0;
}
