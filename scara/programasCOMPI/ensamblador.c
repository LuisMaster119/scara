/*
 * ensamblador.c
 * Fase 6 — Generación de código final en NASM x64 real.
 *
 * Dos backends:


 *
 * Ensamblar:  nasm -f win64 programa.asm -o programa.obj
 * Enlazar:    ld programa.obj -o programa.exe -lkernel32          (ascii)
 *             ld programa.obj -o programa.exe -lSDL2 -lmingw32    (sdl2)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "ensamblador.h"
#include "cinematica.h"

/* ═══════════════════════════════════════════════════════════════
 * SECCIÓN 1 — Evaluador de bytecode para construir la traza
 *
 * Ejecutamos el bytecode optimizado directamente en C para obtener
 * la secuencia de estados (x,y,z,pinza,vel) que luego se incrustan
 * como datos estáticos en el .asm generado.
 * ═══════════════════════════════════════════════════════════════ */

#define MAX_TRAZA          4096
#define MAX_VARS           128
#define MAX_CODE           256
#define APPROACH_CLEARANCE 50

typedef struct { int x, y, z, pinza, velocidad; } Estado;
typedef struct { char nombre[64]; int valor; int usado; } Var;

static Estado traza[MAX_TRAZA];
static int    traza_len = 0;

static Var    vars[MAX_VARS];
static int    pos_x = 0, pos_y = 0, pos_z = 0;
static int    pinza  = 1;
static int    vel    = 100;

static void traza_push(void) {
    if (traza_len >= MAX_TRAZA) return;
    traza[traza_len].x         = pos_x;
    traza[traza_len].y         = pos_y;
    traza[traza_len].z         = pos_z;
    traza[traza_len].pinza     = pinza;
    traza[traza_len].velocidad = vel;
    traza_len++;
}

static int pasos_mov(void) {
    int p = 11 - vel / 10;
    if (p < 3)  p = 3;
    if (p > 10) p = 10;
    return p;
}

static void mover_hacia(int tx, int ty, int tz, int pasos) {
    int x0 = pos_x, y0 = pos_y, z0 = pos_z;
    for (int i = 1; i <= pasos; i++) {
        pos_x = x0 + ((tx - x0) * i) / pasos;
        pos_y = y0 + ((ty - y0) * i) / pasos;
        pos_z = z0 + ((tz - z0) * i) / pasos;
        traza_push();
    }
}

static int var_buscar(const char* n) {
    for (int i = 0; i < MAX_VARS; i++)
        if (vars[i].usado && strcmp(vars[i].nombre, n) == 0) return i;
    return -1;
}

static int var_crear(const char* n, int v) {
    int idx = var_buscar(n);
    if (idx >= 0) { vars[idx].valor = v; return idx; }
    for (int i = 0; i < MAX_VARS; i++) {
        if (!vars[i].usado) {
            vars[i].usado = 1;
            strcpy(vars[i].nombre, n);
            vars[i].valor = v;
            return i;
        }
    }
    return -1;
}

static int operando_val(const Instruccion* ins, int es_izq) {
    if (es_izq) {
        if (ins->flags & INS_F_ARG1_VAR) {
            int idx = var_buscar(ins->sval2);
            return idx >= 0 ? vars[idx].valor : 0;
        }
        return ins->arg1;
    }
    if (ins->flags & INS_F_ARG3_VAR) {
        int idx = var_buscar(ins->sval3);
        return idx >= 0 ? vars[idx].valor : 0;
    }
    return ins->arg3;
}

static int cond_val(const Instruccion* ins, int es_izq) {
    if (es_izq) {
        if (ins->flags & INS_F_ARG1_VAR) {
            int idx = var_buscar(ins->sval);
            return idx >= 0 ? vars[idx].valor : 0;
        }
        return ins->arg1;
    }
    if (ins->flags & INS_F_ARG3_VAR) {
        int idx = var_buscar(ins->sval2);
        return idx >= 0 ? vars[idx].valor : 0;
    }
    return ins->arg3;
}

static int evaluar_cond(const Instruccion* ins) {
    int l = cond_val(ins, 1), r = cond_val(ins, 0);
    switch (ins->arg2) {
        case TOK_LESS:    return l < r;
        case TOK_GREATER: return l > r;
        case TOK_EQUAL:   return l == r;
        default:          return 0;
    }
}

/* Pre-calcular saltos igual que la VM original */
typedef struct { int if_idx; int else_idx; } IfFr;
static int jmp_end_while[MAX_CODE], jmp_back_while[MAX_CODE];
static int jmp_if_false[MAX_CODE],  jmp_else_end[MAX_CODE];
static int jmp_end_rep[MAX_CODE],   jmp_back_rep[MAX_CODE];

static int precalcular_saltos(const Instruccion* prog, int len) {
    int ws[MAX_CODE], rs[MAX_CODE]; IfFr is[MAX_CODE];
    int wt = -1, rt = -1, it = -1;
    for (int i = 0; i < len; i++) {
        jmp_end_while[i] = jmp_back_while[i] = jmp_if_false[i] =
        jmp_else_end[i]  = jmp_end_rep[i]    = jmp_back_rep[i] = -1;
    }
    for (int pc = 0; pc < len; pc++) {
        switch (prog[pc].opcode) {
            case OP_WHILE:
                ws[++wt] = pc; break;
            case OP_END_WHILE: {
                int i = ws[wt--];
                jmp_end_while[i] = pc; jmp_back_while[pc] = i; break;
            }
            case OP_REPEAT:
                rs[++rt] = pc; break;
            case OP_END_REPEAT: {
                int i = rs[rt--];
                jmp_end_rep[i] = pc; jmp_back_rep[pc] = i; break;
            }
            case OP_IF:
                is[++it].if_idx = pc; is[it].else_idx = -1; break;
            case OP_ELSE:
                is[it].else_idx = pc;
                jmp_if_false[is[it].if_idx] = pc + 1; break;
            case OP_END_IF: {
                IfFr fr = is[it--];
                if (fr.else_idx >= 0) jmp_else_end[fr.else_idx] = pc + 1;
                else                  jmp_if_false[fr.if_idx]   = pc + 1;
                break;
            }
            default: break;
        }
    }
    return (wt < 0 && rt < 0 && it < 0) ? 1 : 0;
}

static int construir_traza(const Instruccion* prog, int len) {
    memset(vars, 0, sizeof(vars));
    pos_x = pos_y = pos_z = 0;
    pinza = 1; vel = 100; traza_len = 0;
    traza_push();  /* estado HOME inicial */

    if (!precalcular_saltos(prog, len)) return 0;

    int rep_rest[MAX_CODE];
    for (int i = 0; i < MAX_CODE; i++) rep_rest[i] = -1;

    int pc = 0;
    while (pc >= 0 && pc < len) {
        const Instruccion* ins = &prog[pc];
        switch (ins->opcode) {

            case OP_VAR:   var_crear(ins->sval, ins->arg1); break;
            case OP_POINT: break;

            case OP_ASSIGN: {
                int idx = var_buscar(ins->sval);
                if (idx < 0) break;
                int lhs = operando_val(ins, 1);
                if      (ins->arg2 ==  1) vars[idx].valor = lhs + operando_val(ins, 0);
                else if (ins->arg2 == -1) vars[idx].valor = lhs - operando_val(ins, 0);
                else                      vars[idx].valor = lhs;
                break;
            }

            case OP_SPEED:  vel = ins->arg1; traza_push(); break;
            case OP_HOME:   pos_x = pos_y = pos_z = 0; traza_push(); break;
            case OP_OPEN:   pinza = 1; traza_push(); break;
            case OP_CLOSE:  pinza = 0; traza_push(); break;

            case OP_MOVE:
                mover_hacia(ins->arg1, ins->arg2, ins->arg3, pasos_mov());
                break;

            case OP_MOVEJ: {
                int p  = pasos_mov();
                int zs = (pos_z > ins->arg3 ? pos_z : ins->arg3) + APPROACH_CLEARANCE;
                mover_hacia(pos_x,     pos_y,     zs,          p / 2);
                mover_hacia(ins->arg1, ins->arg2, zs,          p);
                mover_hacia(ins->arg1, ins->arg2, ins->arg3,   p / 2);
                break;
            }

            case OP_APPROACH: {
                int zs = ins->arg3 + APPROACH_CLEARANCE;
                mover_hacia(ins->arg1, ins->arg2, zs,        pasos_mov());
                mover_hacia(ins->arg1, ins->arg2, ins->arg3, pasos_mov());
                break;
            }

            case OP_DEPART:
                pos_z += ins->arg1; traza_push(); break;

            case OP_WAIT:
                for (int w = 0; w < ins->arg1 * 3; w++) traza_push();
                break;

            case OP_PRINT: break;

            case OP_WHILE:
                if (!evaluar_cond(ins)) { pc = jmp_end_while[pc] + 1; continue; }
                break;
            case OP_END_WHILE:
                pc = jmp_back_while[pc]; continue;

            case OP_IF:
                if (!evaluar_cond(ins)) { pc = jmp_if_false[pc]; continue; }
                break;
            case OP_ELSE:  pc = jmp_else_end[pc]; continue;
            case OP_END_IF: break;

            case OP_REPEAT:
                if (rep_rest[pc] < 0)   rep_rest[pc] = ins->arg1;
                if (rep_rest[pc] <= 0) {
                    rep_rest[pc] = -1;
                    pc = jmp_end_rep[pc] + 1; continue;
                }
                break;
            case OP_END_REPEAT:
                rep_rest[jmp_back_rep[pc]]--;
                if (rep_rest[jmp_back_rep[pc]] > 0) {
                    pc = jmp_back_rep[pc] + 1; continue;
                }
                rep_rest[jmp_back_rep[pc]] = -1;
                break;

            case OP_HALT: goto traza_done;
            default: break;
        }
        pc++;
    }
traza_done:
    return traza_len;
}

/* ═══════════════════════════════════════════════════════════════
 * SECCIÓN 2 — Emitir datos de traza como C puro
 *
 * Se abandona el puente ASM→C porque en Windows x64 las relocaciones
 * RIP-relative de 32 bits en objetos NASM enlazados con GCC pueden
 * apuntar a direcciones incorrectas cuando la imagen supera 2 GB de
 * espacio de direcciones virtual.  Emitir los arrays directamente en
 * el _vis.c elimina el problema de ABI y relocation por completo.
 * ═══════════════════════════════════════════════════════════════ */

static void emit_array_c(FILE* f, const char* nombre, int* arr, int n) {
    fprintf(f, "static const int %s[%d] = {\n    ", nombre, n);
    for (int i = 0; i < n; i++) {
        fprintf(f, "%d", arr[i]);
        if (i < n - 1) {
            fprintf(f, ",");
            if ((i + 1) % 16 == 0)
                fprintf(f, "\n    ");
            else
                fprintf(f, " ");
        }
    }
    fprintf(f, "\n};\n");
}

/* Escribe en `f` las declaraciones C de todos los arrays de traza. */
void generar_traza_c(FILE* f) {
    int tmp[MAX_TRAZA];

    fprintf(f, "/* --- Traza generada por el compilador SCARA --- */\n");
    fprintf(f, "static const int TRAZA_LEN = %d;\n\n", traza_len);

    for (int i = 0; i < traza_len; i++) tmp[i] = traza[i].x;
    emit_array_c(f, "traza_x", tmp, traza_len);

    for (int i = 0; i < traza_len; i++) tmp[i] = traza[i].y;
    emit_array_c(f, "traza_y", tmp, traza_len);

    for (int i = 0; i < traza_len; i++) tmp[i] = traza[i].z;
    emit_array_c(f, "traza_z", tmp, traza_len);

    for (int i = 0; i < traza_len; i++) tmp[i] = traza[i].pinza;
    emit_array_c(f, "traza_pinza", tmp, traza_len);

    for (int i = 0; i < traza_len; i++) tmp[i] = traza[i].velocidad;
    emit_array_c(f, "traza_vel", tmp, traza_len);

    fprintf(f, "\n");
}

/* ═══════════════════════════════════════════════════════════════
 * SECCIÓN 3 — Emitir datos de traza como NASM (para el .asm)
 * ═══════════════════════════════════════════════════════════════ */

static void emit_array_dd(FILE* f, const char* nombre, int* arr, int n) {
    fprintf(f, "    %-14s dd  ", nombre);
    for (int i = 0; i < n; i++) {
        fprintf(f, "%d", arr[i]);
        if (i < n - 1) {
            fprintf(f, ", ");
            if ((i + 1) % 16 == 0)
                fprintf(f, "\\\n                    ");
        }
    }
    fprintf(f, "\n");
}

static void emit_asm_sdl2(FILE* f) {
    int tmp[MAX_TRAZA];

    fprintf(f,
        "; ============================================================\n"
        "; Generado por compilador SCARA — datos de traza NASM\n"
        "; Los datos se exportan como simbolos C para el visualizador SDL2.\n"
        "; ============================================================\n\n"
        "default rel\n\n"
        "section .data\n\n"
        "    traza_len  dd  %d\n", traza_len);

    for (int i = 0; i < traza_len; i++) tmp[i] = traza[i].x;
    emit_array_dd(f, "traza_x", tmp, traza_len);

    for (int i = 0; i < traza_len; i++) tmp[i] = traza[i].y;
    emit_array_dd(f, "traza_y", tmp, traza_len);

    for (int i = 0; i < traza_len; i++) tmp[i] = traza[i].z;
    emit_array_dd(f, "traza_z", tmp, traza_len);

    for (int i = 0; i < traza_len; i++) tmp[i] = traza[i].pinza;
    emit_array_dd(f, "traza_pinza", tmp, traza_len);

    for (int i = 0; i < traza_len; i++) tmp[i] = traza[i].velocidad;
    emit_array_dd(f, "traza_vel", tmp, traza_len);

    fprintf(f, "\n; (El visualizador SDL2 usa los datos del _vis.c generado)\n");
}

/* ═══════════════════════════════════════════════════════════════
 * SECCIÓN 4 — Punto de entrada público
 * ═══════════════════════════════════════════════════════════════ */

int generar_ensamblador(const Instruccion* programa, int longitud,
                        const char* ruta_salida) {
    /* 1. Ejecutar el bytecode para construir la traza */
    int n = construir_traza(programa, longitud);
    if (n <= 0) {
        fprintf(stderr, "Error: traza vacia — revisa el programa fuente\n");
        return 1;
    }
    printf("[GEN] Traza construida: %d estados\n", n);

    /* 2. Abrir archivo de salida (.asm) */
    FILE* f = fopen(ruta_salida, "w");
    if (!f) return 1;

    /* 3. Emitir NASM con los datos */
    emit_asm_sdl2(f);

    fclose(f);
    return 0;
}