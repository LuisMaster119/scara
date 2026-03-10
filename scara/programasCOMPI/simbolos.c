#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "simbolos.h"

// --- La tabla en memoria ----------------------------------

Simbolo tabla_simbolos[MAX_SIMBOLOS];
int     tabla_len = 0;

// --- Agregar un simbolo -----------------------------------

void tabla_agregar(const char* nombre, TipoSimbolo tipo,
                   int v1, int v2, int v3) {
    if (tabla_buscar(nombre) != NULL) return; // duplicado manejado en parser
    if (tabla_len >= MAX_SIMBOLOS) {
        fprintf(stderr, "Error: tabla de simbolos llena\n");
        exit(1);
    }
    strcpy(tabla_simbolos[tabla_len].nombre, nombre);
    tabla_simbolos[tabla_len].tipo = tipo;
    tabla_simbolos[tabla_len].val1 = v1;
    tabla_simbolos[tabla_len].val2 = v2;
    tabla_simbolos[tabla_len].val3 = v3;
    tabla_len++;
}

// --- Buscar un simbolo ------------------------------------

Simbolo* tabla_buscar(const char* nombre) {
    for (int i = 0; i < tabla_len; i++)
        if (strcmp(tabla_simbolos[i].nombre, nombre) == 0)
            return &tabla_simbolos[i];
    return NULL;
}

// --- Imprimir la tabla ------------------------------------

void tabla_imprimir(void) {
    printf("\n%-15s %-8s %-6s %-6s %-6s\n",
           "NOMBRE", "TIPO", "VAL1", "VAL2", "VAL3");
    printf("-----------------------------------------\n");
    for (int i = 0; i < tabla_len; i++) {
        Simbolo* s = &tabla_simbolos[i];
        printf("%-15s %-8s %-6d %-6d %-6d\n",
               s->nombre,
               s->tipo == SIM_VAR ? "VAR" : "POINT",
               s->val1, s->val2, s->val3);
    }
}
