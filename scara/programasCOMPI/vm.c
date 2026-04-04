#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <windows.h>
#include "vm.h"
#include "cinematica.h"

#define VM_MAX_VARS 128
#define VM_MAX_CODE 256
#define VM_APPROACH_CLEARANCE 50

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

typedef struct {
    int if_idx;
    int else_idx;
} IfFrame;

static void vm_imprimir_posicion(const char* etiqueta) {
    printf("[VM] %s -> posicion (%d,%d,%d)\n", etiqueta, pos_x, pos_y, pos_z);
}

static int vm_calcular_pasos_movimiento(void) {
    /* A mayor velocidad, menos pasos de simulacion. */
    int pasos = 11 - (velocidad_actual / 10);
    if (pasos < 3) pasos = 3;
    if (pasos > 10) pasos = 10;
    return pasos;
}

static void vm_trazar_hacia(const char* etiqueta, int x_obj, int y_obj, int z_obj, int pasos) {
    int x0 = pos_x;
    int y0 = pos_y;
    int z0 = pos_z;

    if (pasos < 1) pasos = 1;
    for (int i = 1; i <= pasos; i++) {
        pos_x = x0 + ((x_obj - x0) * i) / pasos;
        pos_y = y0 + ((y_obj - y0) * i) / pasos;
        pos_z = z0 + ((z_obj - z0) * i) / pasos;
        printf("[VM] %s paso %d/%d -> posicion (%d,%d,%d)\n",
               etiqueta, i, pasos, pos_x, pos_y, pos_z);
    }
}

static int vm_imprimir_ik(int x, int y) {
    const int tol_deg = 2;
    int q1_up = 0, q2_up = 0;
    int q1_dn = 0, q2_dn = 0;
    int q1_asm_up = 0, q2_asm_up = 0;
    int q1_asm_dn = 0, q2_asm_dn = 0;
    int ok_c = 0, ok_asm = 0;
    int coincide_up = 0, coincide_dn = 0;

    if (!cinematica_ik_xy_modo(x, y, CIN_MODO_CODO_ARRIBA, &q1_up, &q2_up) ||
        !cinematica_ik_xy_modo(x, y, CIN_MODO_CODO_ABAJO, &q1_dn, &q2_dn)) {
        fprintf(stderr, "VM ERROR: IK fuera de dominio para XY=(%d,%d)\n", x, y);
        return 0;
    }

    coincide_up = cinematica_comparar_c_vs_asm_modo(
        x, y,
        CIN_MODO_CODO_ARRIBA, tol_deg,
        &ok_c, &ok_asm,
        &q1_up, &q2_up,
        &q1_asm_up, &q2_asm_up
    );

    if (!ok_c || !ok_asm || !coincide_up) {
        fprintf(stderr,
                "VM ERROR: inconsistencia IK_UP C/ASM en XY=(%d,%d) C=(%d,%d) ASM=(%d,%d)\n",
                x, y, q1_up, q2_up, q1_asm_up, q2_asm_up);
        return 0;
    }

    coincide_dn = cinematica_comparar_c_vs_asm_modo(
        x, y,
        CIN_MODO_CODO_ABAJO, tol_deg,
        &ok_c, &ok_asm,
        &q1_dn, &q2_dn,
        &q1_asm_dn, &q2_asm_dn
    );

    if (!ok_c || !ok_asm || !coincide_dn) {
        fprintf(stderr,
                "VM ERROR: inconsistencia IK_DOWN C/ASM en XY=(%d,%d) C=(%d,%d) ASM=(%d,%d)\n",
                x, y, q1_dn, q2_dn, q1_asm_dn, q2_asm_dn);
        return 0;
    }

    printf("[VM] IK_UP   -> q1=%d q2=%d\n", q1_up, q2_up);
    printf("[VM] IK_DOWN -> q1=%d q2=%d\n", q1_dn, q2_dn);
    printf("[VM] IK_ASM_UP   -> q1=%d q2=%d\n", q1_asm_up, q2_asm_up);
    printf("[VM] IK_ASM_DOWN -> q1=%d q2=%d\n", q1_asm_dn, q2_asm_dn);
    return 1;
}

static int vm_validar_objetivo_movimiento(int x, int y, int z, const char* op) {
    int L1 = 200, L2 = 150;
    int d2 = x * x + y * y;
    int dmax = (L1 + L2) * (L1 + L2);
    int dmin = (L1 - L2) * (L1 - L2);

    if (d2 < dmin || d2 > dmax) {
        fprintf(stderr,
                "VM ERROR: %s fuera de alcance XY (%d,%d), rango radial [%d,%d] mm\n",
                op, x, y, L1 - L2, L1 + L2);
        return 0;
    }

    if (z < 0) {
        fprintf(stderr, "VM ERROR: %s con Z invalida (%d), debe ser >= 0\n", op, z);
        return 0;
    }

    return 1;
}

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

static int vm_valor_por_nombre(const char* nombre, int* out) {
    int idx = vm_buscar_var(nombre);
    if (idx < 0) {
        fprintf(stderr, "VM ERROR: variable '%s' no existe\n", nombre);
        return 0;
    }
    *out = vm_vars[idx].valor;
    return 1;
}

static int vm_obtener_valor_condicion(const Instruccion* ins, int es_arg1, int* out) {
    if (es_arg1) {
        if (ins->flags & INS_F_ARG1_VAR) {
            return vm_valor_por_nombre(ins->sval, out);
        }
        *out = ins->arg1;
        return 1;
    }

    if (ins->flags & INS_F_ARG3_VAR) {
        return vm_valor_por_nombre(ins->sval2, out);
    }
    *out = ins->arg3;
    return 1;
}

static int vm_evaluar_condicion(const Instruccion* ins, int* out) {
    int lhs = 0, rhs = 0;
    if (!vm_obtener_valor_condicion(ins, 1, &lhs)) return 0;
    if (!vm_obtener_valor_condicion(ins, 0, &rhs)) return 0;

    switch (ins->arg2) {
        case TOK_LESS:    *out = (lhs < rhs);  return 1;
        case TOK_GREATER: *out = (lhs > rhs);  return 1;
        case TOK_EQUAL:   *out = (lhs == rhs); return 1;
        default:
            fprintf(stderr, "VM ERROR: operador de condicion invalido: %d\n", ins->arg2);
            return 0;
    }
}

static int vm_precalcular_saltos(Instruccion* programa, int longitud,
                                 int* jump_end_while,
                                 int* jump_back_while,
                                 int* jump_if_false,
                                 int* jump_else_end,
                                 int* jump_end_repeat,
                                 int* jump_back_repeat) {
    int while_stack[VM_MAX_CODE];
    int repeat_stack[VM_MAX_CODE];
    IfFrame if_stack[VM_MAX_CODE];
    int wtop = -1, rtop = -1, itop = -1;

    for (int i = 0; i < longitud; i++) {
        jump_end_while[i] = -1;
        jump_back_while[i] = -1;
        jump_if_false[i] = -1;
        jump_else_end[i] = -1;
        jump_end_repeat[i] = -1;
        jump_back_repeat[i] = -1;
    }

    for (int pc = 0; pc < longitud; pc++) {
        switch (programa[pc].opcode) {
            case OP_WHILE:
                if (++wtop >= VM_MAX_CODE) {
                    fprintf(stderr, "VM ERROR: pila WHILE desbordada\n");
                    return 0;
                }
                while_stack[wtop] = pc;
                break;

            case OP_END_WHILE: {
                if (wtop < 0) {
                    fprintf(stderr, "VM ERROR: OP_END_WHILE sin OP_WHILE\n");
                    return 0;
                }
                int ini = while_stack[wtop--];
                jump_end_while[ini] = pc;
                jump_back_while[pc] = ini;
                break;
            }

            case OP_REPEAT:
                if (++rtop >= VM_MAX_CODE) {
                    fprintf(stderr, "VM ERROR: pila REPEAT desbordada\n");
                    return 0;
                }
                repeat_stack[rtop] = pc;
                break;

            case OP_END_REPEAT: {
                if (rtop < 0) {
                    fprintf(stderr, "VM ERROR: OP_END_REPEAT sin OP_REPEAT\n");
                    return 0;
                }
                int ini = repeat_stack[rtop--];
                jump_end_repeat[ini] = pc;
                jump_back_repeat[pc] = ini;
                break;
            }

            case OP_IF:
                if (++itop >= VM_MAX_CODE) {
                    fprintf(stderr, "VM ERROR: pila IF desbordada\n");
                    return 0;
                }
                if_stack[itop].if_idx = pc;
                if_stack[itop].else_idx = -1;
                break;

            case OP_ELSE:
                if (itop < 0) {
                    fprintf(stderr, "VM ERROR: OP_ELSE sin OP_IF\n");
                    return 0;
                }
                if_stack[itop].else_idx = pc;
                jump_if_false[if_stack[itop].if_idx] = pc + 1;
                break;

            case OP_END_IF: {
                if (itop < 0) {
                    fprintf(stderr, "VM ERROR: OP_END_IF sin OP_IF\n");
                    return 0;
                }
                IfFrame fr = if_stack[itop--];
                if (fr.else_idx >= 0) {
                    jump_else_end[fr.else_idx] = pc + 1;
                } else {
                    jump_if_false[fr.if_idx] = pc + 1;
                }
                break;
            }

            default:
                break;
        }
    }

    if (wtop >= 0 || rtop >= 0 || itop >= 0) {
        fprintf(stderr, "VM ERROR: bloques de control no balanceados\n");
        return 0;
    }

    return 1;
}

int vm_ejecutar(Instruccion* programa, int longitud, int traza) {
    vm_reset_estado();

    int jump_end_while[VM_MAX_CODE];
    int jump_back_while[VM_MAX_CODE];
    int jump_if_false[VM_MAX_CODE];
    int jump_else_end[VM_MAX_CODE];
    int jump_end_repeat[VM_MAX_CODE];
    int jump_back_repeat[VM_MAX_CODE];
    int repeat_restante[VM_MAX_CODE];

    for (int i = 0; i < VM_MAX_CODE; i++) {
        repeat_restante[i] = -1;
    }

    if (longitud > VM_MAX_CODE) {
        fprintf(stderr, "VM ERROR: bytecode excede limite (%d)\n", VM_MAX_CODE);
        return 1;
    }

    if (!vm_precalcular_saltos(programa, longitud,
                               jump_end_while,
                               jump_back_while,
                               jump_if_false,
                               jump_else_end,
                               jump_end_repeat,
                               jump_back_repeat)) {
        return 1;
    }

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
                if (vm_crear_var(ins->sval, ins->arg1) < 0) return 1;
                break;
            }

            case OP_POINT:
                /* En Fase 3, las declaraciones POINT no cambian estado runtime. */
                break;

            case OP_ASSIGN: {
                int idx_dest = vm_buscar_var(ins->sval);
                if (idx_dest < 0) {
                    fprintf(stderr, "VM ERROR: destino '%s' no existe\n", ins->sval);
                    return 1;
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
                vm_imprimir_posicion("HOME");
                break;

            case OP_MOVE:
                if (!vm_validar_objetivo_movimiento(ins->arg1, ins->arg2, ins->arg3, "MOVE")) {
                    return 1;
                }
                vm_trazar_hacia("MOVE_LIN", ins->arg1, ins->arg2, ins->arg3,
                               vm_calcular_pasos_movimiento());
                vm_imprimir_posicion("MOVE");
                if (!vm_imprimir_ik(pos_x, pos_y)) return 1;
                break;

            case OP_MOVEJ:
                if (!vm_validar_objetivo_movimiento(ins->arg1, ins->arg2, ins->arg3, "MOVEJ")) {
                    return 1;
                }
                printf("[VM] MOVEJ: trayectoria articular (stub)\n");

                {
                    int pasos = vm_calcular_pasos_movimiento();
                    int z_segura = ((pos_z > ins->arg3) ? pos_z : ins->arg3) + VM_APPROACH_CLEARANCE;

                    vm_trazar_hacia("MOVEJ_SEG1", pos_x, pos_y, z_segura, pasos / 2);
                    vm_trazar_hacia("MOVEJ_SEG2", ins->arg1, ins->arg2, z_segura, pasos);
                    vm_trazar_hacia("MOVEJ_SEG3", ins->arg1, ins->arg2, ins->arg3, pasos / 2);
                }

                vm_imprimir_posicion("MOVEJ");
                if (!vm_imprimir_ik(pos_x, pos_y)) return 1;
                break;

            case OP_APPROACH:
                if (!vm_validar_objetivo_movimiento(ins->arg1, ins->arg2, ins->arg3, "APPROACH")) {
                    return 1;
                }
                /* Aproximacion en dos etapas: altura segura y descenso al objetivo. */
                pos_x = ins->arg1;
                pos_y = ins->arg2;
                pos_z = (pos_z > ins->arg3 + VM_APPROACH_CLEARANCE)
                            ? pos_z
                            : (ins->arg3 + VM_APPROACH_CLEARANCE);
                vm_imprimir_posicion("APPROACH_SAFE");

                pos_x = ins->arg1;
                pos_y = ins->arg2;
                pos_z = ins->arg3;
                vm_imprimir_posicion("APPROACH");
                if (!vm_imprimir_ik(pos_x, pos_y)) return 1;
                break;

            case OP_DEPART:
                pos_z += ins->arg1;
                vm_imprimir_posicion("DEPART");
                break;

            case OP_SPEED:
                if (ins->arg1 < 0 || ins->arg1 > 100) {
                    fprintf(stderr, "VM ERROR: SPEED fuera de rango: %d\n", ins->arg1);
                    return 1;
                }
                velocidad_actual = ins->arg1;
                printf("[VM] SPEED = %d%%\n", velocidad_actual);
                break;

            case OP_WAIT:
                if (ins->arg1 <= 0) {
                    fprintf(stderr, "VM ERROR: WAIT invalido: %d\n", ins->arg1);
                    return 1;
                }
                printf("[VM] WAIT %d s\n", ins->arg1);
                Sleep((DWORD)ins->arg1 * 1000);
                break;

            case OP_WHILE: {
                int cond = 0;
                if (!vm_evaluar_condicion(ins, &cond)) return 1;
                if (!cond) {
                    int fin = jump_end_while[pc];
                    if (fin < 0) {
                        fprintf(stderr, "VM ERROR: WHILE sin END_WHILE en PC=%d\n", pc);
                        return 1;
                    }
                    pc = fin + 1;
                    continue;
                }
                break;
            }

            case OP_END_WHILE: {
                int ini = jump_back_while[pc];
                if (ini < 0) {
                    fprintf(stderr, "VM ERROR: END_WHILE sin WHILE en PC=%d\n", pc);
                    return 1;
                }
                pc = ini;
                continue;
            }

            case OP_IF: {
                int cond = 0;
                if (!vm_evaluar_condicion(ins, &cond)) return 1;
                if (!cond) {
                    int dest = jump_if_false[pc];
                    if (dest < 0 || dest > longitud) {
                        fprintf(stderr, "VM ERROR: IF sin destino de salto en PC=%d\n", pc);
                        return 1;
                    }
                    pc = dest;
                    continue;
                }
                break;
            }

            case OP_ELSE: {
                int dest = jump_else_end[pc];
                if (dest < 0 || dest > longitud) {
                    fprintf(stderr, "VM ERROR: ELSE sin END_IF en PC=%d\n", pc);
                    return 1;
                }
                pc = dest;
                continue;
            }

            case OP_END_IF:
                break;

            case OP_REPEAT: {
                if (repeat_restante[pc] < 0) {
                    repeat_restante[pc] = ins->arg1;
                }
                if (repeat_restante[pc] <= 0) {
                    int fin = jump_end_repeat[pc];
                    if (fin < 0) {
                        fprintf(stderr, "VM ERROR: REPEAT sin END_REPEAT en PC=%d\n", pc);
                        return 1;
                    }
                    repeat_restante[pc] = -1;
                    pc = fin + 1;
                    continue;
                }
                break;
            }

            case OP_END_REPEAT: {
                int ini = jump_back_repeat[pc];
                if (ini < 0) {
                    fprintf(stderr, "VM ERROR: END_REPEAT sin REPEAT en PC=%d\n", pc);
                    return 1;
                }
                repeat_restante[ini]--;
                if (repeat_restante[ini] > 0) {
                    pc = ini + 1;
                    continue;
                }
                repeat_restante[ini] = -1;
                break;
            }

            case OP_HALT:
                printf("[VM] HALT\n");
                return 0;

            default:
                printf("[VM] OP no implementado aun: %s\n", opcode_a_texto(ins->opcode));
                break;
        }

        pc++;
    }

    fprintf(stderr, "VM ERROR: PC fuera de rango (%d)\n", pc);
    return 1;
}
