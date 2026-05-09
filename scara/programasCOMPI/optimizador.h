#ifndef OPTIMIZADOR_H
#define OPTIMIZADOR_H

#include "parser.h"

int optimizar_bytecode(const Instruccion* entrada, int len_entrada,
                       Instruccion* salida, int max_salida);

#endif
