#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "errores.h"

// --- Estado interno ---------------------------------------

static const char* fuente_global = NULL;

void error_init(const char* fuente) {
    fuente_global = fuente;
}

// --- Obtener una linea especifica del codigo fuente -------

static void obtener_linea(int numero_linea, char* buffer, int max) {
    if (!fuente_global) {
        buffer[0] = '\0';
        return;
    }

    int linea_actual = 1;
    int i = 0, j = 0;

    while (fuente_global[i] != '\0') {
        if (linea_actual == numero_linea) {
            while (fuente_global[i] != '\n' &&
                   fuente_global[i] != '\0' &&
                   j < max - 1) {
                buffer[j++] = fuente_global[i++];
            }
            break;
        }
        if (fuente_global[i] == '\n') linea_actual++;
        i++;
    }
    buffer[j] = '\0';
}

// --- Encontrar columna del token en la linea --------------

static int encontrar_columna(const char* linea, const char* token) {
    if (!token || token[0] == '\0') return -1;
    const char* pos = strstr(linea, token);
    if (!pos) return -1;
    return (int)(pos - linea);
}

// --- Funcion principal de reporte -------------------------

void error_reportar(TipoError tipo, int linea,
                    const char* token, const char* mensaje) {

    // encabezado segun tipo
    fprintf(stderr, "\n");
    switch (tipo) {
        case ERR_LEXICO:
            fprintf(stderr, ">>> ERROR LEXICO\n");
            break;
        case ERR_SINTACTICO:
            fprintf(stderr, ">>> ERROR SINTACTICO\n");
            break;
        case ERR_SEMANTICO:
            fprintf(stderr, ">>> ERROR SEMANTICO\n");
            break;
    }

    // numero de linea y mensaje
    fprintf(stderr, "    Linea %d: %s\n", linea, mensaje);

    // mostrar la linea del codigo fuente
    char linea_texto[256];
    obtener_linea(linea, linea_texto, sizeof(linea_texto));

    if (linea_texto[0] != '\0') {
        fprintf(stderr, "\n");
        fprintf(stderr, "    | %s\n", linea_texto);

        // subrayado del token problematico
        int col = encontrar_columna(linea_texto, token);
        if (col >= 0 && token && token[0] != '\0') {
            fprintf(stderr, "    | ");
            for (int i = 0; i < col; i++)
                fprintf(stderr, " ");
            for (int i = 0; i < (int)strlen(token); i++)
                fprintf(stderr, "^");
            fprintf(stderr, "\n");
        }
    }

    fprintf(stderr, "\n");
    exit(1);
}
