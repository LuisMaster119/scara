#ifndef VM_H
#define VM_H

#include "parser.h"

typedef struct {
	int x;
	int y;
	int z;
	int pinza_abierta;
	int velocidad;
} VmEstado;

int vm_ejecutar(Instruccion* programa, int longitud, int traza);
const VmEstado* vm_obtener_traza_estados(int* out_len);

#endif
