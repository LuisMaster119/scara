#ifndef ENSAMBLADOR_H
#define ENSAMBLADOR_H

#include "parser.h"

int generar_ensamblador(const Instruccion* programa, int longitud,
                        const char* ruta_salida);

#endif