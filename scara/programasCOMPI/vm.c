#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <windows.h>
#include "vm.h"

#define VM_MAX_VARS 128

typedef struct {
    char nombre[64];
    int  valor;
    int  usado;
} VmVar;

static VmVar vm_vars[VM_MAX_VARS];
static int velocidad_actual = 100;
static int pinza_abierta = 1;
static int pos_x = 0;
static int pos_y = 0;
static int pos_z = 0;

static void vm_reset_estado(void) {
    memset(vm_vars, 0, sizeof(vm_vars));
    velocidad_actual = 100;
    pinza_abierta = 1;
    pos_x = 0;
    pos_y = 0;
    pos_z = 0;
}

static int vm_buscar_var(const char* nombre) {
    for (int i = 0; i < VM_MAX_VARS; i++) {
        if (vm_vars[i].usado && strcmp(vm_vars[i].nombre, nombre) == 0) {
            return i;
        }
    }
    return -1;
}

static int vm_crear_var(const char* nombre, int valor) {
    int idx = vm_buscar_var(nombre);
    if (idx >= 0) {
        vm_vars[idx].valor = valor;
        return idx;
    }

    for (int i = 0; i < VM_MAX_VARS; i++) {
        if (!vm_vars[i].usado) {
            vm_vars[i].usado = 1;
            strcpy(vm_vars[i].nombre, nombre);
            vm_vars[i].valor = valor;
            return i;
        }
    }

    fprintf(stderr, "VM ERROR: tabla de variables llena\n");
    return -1;
}

static int vm_obtener_valor_operando(const Instruccion* ins, int es_arg1) {
    if (es_arg1) {
        if (ins->flags & INS_F_ARG1_VAR) {
            int idx = vm_buscar_var(ins->sval2);
            if (idx < 0) {
                fprintf(stderr, "VM ERROR: variable '%s' no existe\n", ins->sval2);
                return 0;
            }
            return vm_vars[idx].valor;
        }
        return ins->arg1;
    }

    if (ins->flags & INS_F_ARG3_VAR) {
        int idx = vm_buscar_var(ins->sval3);
        if (idx < 0) {
            fprintf(stderr, "VM ERROR: variable '%s' no existe\n", ins->sval3);
            return 0;
        }
        return vm_vars[idx].valor;
    }
    return ins->arg3;
}

void vm_ejecutar(Instruccion* programa, int longitud, int traza) {
    vm_reset_estado();

    int pc = 0;
    while (pc >= 0 && pc < longitud) {
        Instruccion* ins = &programa[pc];

        if (traza) {
            printf("[VM] PC=%d OP=%s A1=%d A2=%d A3=%d F=%d S='%s' S2='%s' S3='%s'\n",
                   pc, opcode_a_texto(ins->opcode), ins->arg1, ins->arg2, ins->arg3,
                   ins->flags, ins->sval, ins->sval2, ins->sval3);
        }

        switch (ins->opcode) {
            case OP_VAR: {
                if (vm_crear_var(ins->sval, ins->arg1) < 0) return;
                break;
            }

            case OP_ASSIGN: {
                int idx_dest = vm_buscar_var(ins->sval);
                if (idx_dest < 0) {
                    fprintf(stderr, "VM ERROR: destino '%s' no existe\n", ins->sval);
                    return;
                }

                int lhs = vm_obtener_valor_operando(ins, 1);
                int resultado = lhs;

                if (ins->arg2 == 1 || ins->arg2 == -1) {
                    int rhs = vm_obtener_valor_operando(ins, 0);
                    resultado = (ins->arg2 == 1) ? (lhs + rhs) : (lhs - rhs);
                }

                vm_vars[idx_dest].valor = resultado;
                break;
            }

            case OP_PRINT:
                printf("[VM PRINT] %s\n", ins->sval);
                break;

            case OP_OPEN:
                pinza_abierta = 1;
                printf("[VM] Pinza: OPEN\n");
                break;

            case OP_CLOSE:
                pinza_abierta = 0;
                printf("[VM] Pinza: CLOSE\n");
                break;

            case OP_HOME:
                pos_x = 0;
                pos_y = 0;
                pos_z = 0;
                printf("[VM] HOME -> posicion (0,0,0)\n");
                break;

            case OP_SPEED:
                if (ins->arg1 < 0 || ins->arg1 > 100) {
                    fprintf(stderr, "VM ERROR: SPEED fuera de rango: %d\n", ins->arg1);
                    return;
                }
                velocidad_actual = ins->arg1;
                printf("[VM] SPEED = %d%%\n", velocidad_actual);
                break;

            case OP_WAIT:
                if (ins->arg1 <= 0) {
                    fprintf(stderr, "VM ERROR: WAIT invalido: %d\n", ins->arg1);
                    return;
                }
                printf("[VM] WAIT %d s\n", ins->arg1);
                Sleep((DWORD)ins->arg1 * 1000);
                break;

            case OP_HALT:
                printf("[VM] HALT\n");
                return;

            default:
                printf("[VM] OP no implementado en Fase 1: %s\n", opcode_a_texto(ins->opcode));
                break;
        }

        pc++;
    }

    fprintf(stderr, "VM ERROR: PC fuera de rango (%d)\n", pc);
}
