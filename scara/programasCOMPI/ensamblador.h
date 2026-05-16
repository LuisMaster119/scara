#ifndef ENSAMBLADOR_H
#define ENSAMBLADOR_H

#include "parser.h"

typedef enum {
    OUTPUT_SDL2,
    OUTPUT_ASCII
} OutputMode;

int generar_ensamblador(const Instruccion* programa, int longitud,
                        const char* ruta_salida, OutputMode modo);

#endif