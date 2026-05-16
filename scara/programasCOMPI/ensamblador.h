#ifndef ENSAMBLADOR_H
#define ENSAMBLADOR_H

#include "parser.h"
#include <stdio.h>

/* Construye la traza y escribe los arrays como C estático en `ruta_salida`.
   El archivo generado se incluye (#include) en el _vis.c — no necesita NASM. */
int generar_ensamblador(const Instruccion* programa, int longitud,
                        const char* ruta_salida);

/* Escribe solo los arrays (sin abrir archivo) en el FILE ya abierto `f`.
   Usado por main.c para incrustar la traza dentro del _vis.c generado. */
void generar_traza_c(FILE* f);

#endif