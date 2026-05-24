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

/* Tipos de evento para keyframes */
#define KF_NONE     0
#define KF_HOME     1
#define KF_MOVE     2
#define KF_MOVEJ    3
#define KF_APPROACH 4
#define KF_DEPART   5
#define KF_OPEN     6
#define KF_CLOSE    7
#define KF_SPEED    8
#define KF_WAIT     9

typedef struct { int x, y, z, pinza, velocidad; int evento; } Estado;
typedef struct { char nombre[64]; int valor; int usado; } Var;

static Estado traza[MAX_TRAZA];
static int    traza_len = 0;

/* Tabla de keyframes: indices en traza[] donde ocurre un evento importante */
#define MAX_KF 512
static int kf_idx[MAX_KF];
static int kf_tipo[MAX_KF];
static int kf_len = 0;

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
    traza[traza_len].evento    = KF_NONE;
    traza_len++;
}

/* Registra el indice actual como keyframe ANTES del proximo push */
static void kf_push(int tipo) {
    if (kf_len >= MAX_KF) return;
    kf_idx[kf_len]  = traza_len;
    kf_tipo[kf_len] = tipo;
    kf_len++;
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
    pinza = 1; vel = 100; traza_len = 0; kf_len = 0;
    kf_push(KF_HOME);
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

            case OP_SPEED: {
                kf_push(KF_SPEED);
                int sv;
                if (ins->flags & INS_F_ARG1_VAR) {
                    int idx = var_buscar(ins->sval);
                    sv = (idx >= 0) ? vars[idx].valor : 0;
                } else {
                    sv = ins->arg1;
                }
                vel = sv;
                traza_push();
                break;
            }
            case OP_HOME:   kf_push(KF_HOME);   pos_x = pos_y = pos_z = 0; traza_push(); break;
            case OP_OPEN:   kf_push(KF_OPEN);   pinza = 1; traza_push(); break;
            case OP_CLOSE:  kf_push(KF_CLOSE);  pinza = 0; traza_push(); break;

            case OP_MOVE:
                kf_push(KF_MOVE);
                mover_hacia(ins->arg1, ins->arg2, ins->arg3, pasos_mov());
                break;

            case OP_MOVEJ: {
                int p  = pasos_mov();
                int zs = (pos_z > ins->arg3 ? pos_z : ins->arg3) + APPROACH_CLEARANCE;
                kf_push(KF_MOVEJ);
                mover_hacia(pos_x,     pos_y,     zs,          p / 2);
                mover_hacia(ins->arg1, ins->arg2, zs,          p);
                mover_hacia(ins->arg1, ins->arg2, ins->arg3,   p / 2);
                break;
            }

            case OP_APPROACH: {
                int zs = ins->arg3 + APPROACH_CLEARANCE;
                kf_push(KF_APPROACH);
                mover_hacia(ins->arg1, ins->arg2, zs,        pasos_mov());
                mover_hacia(ins->arg1, ins->arg2, ins->arg3, pasos_mov());
                break;
            }

            case OP_DEPART:
                kf_push(KF_DEPART);
                pos_z += ins->arg1; traza_push(); break;

            case OP_WAIT: {
                kf_push(KF_WAIT);
                int wt;
                if (ins->flags & INS_F_ARG1_VAR) {
                    int idx = var_buscar(ins->sval);
                    wt = (idx >= 0) ? vars[idx].valor : 0;
                } else {
                    wt = ins->arg1;
                }
                for (int w = 0; w < wt * 3; w++) traza_push();
                break;
            }

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

    /* Keyframes */
    fprintf(f, "static const int TRAZA_KF_LEN = %d;\n\n", kf_len);
    emit_array_c(f, "traza_kf",   kf_idx,  kf_len);
    emit_array_c(f, "traza_kf_tipo", kf_tipo, kf_len);

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

/* emit_asm_sdl2 eliminado — reemplazado por emit_asm_completo (Nivel 2) */

/* ═══════════════════════════════════════════════════════════════
 * SECCIÓN 4 — .asm completo: datos + SDL2 en NASM puro (Paso 1)
 *
 * Genera un único archivo NASM x64 que contiene:
 *   · Los arrays de traza como datos estáticos en .data
 *   · Externs para SDL2, SDL2_ttf y el C runtime
 *   · Sección .bss para punteros (ventana, renderer, fuentes)
 *   · main: inicializa SDL2+TTF, abre ventana, muestra un frame
 *           y limpia (render loop completo se añade en pasos siguientes)
 *
 * Ensamblar: nasm -f win64 prog.asm -o prog.obj
 * Enlazar  : gcc prog.obj -o prog.exe -L"C:/msys64/mingw64/lib"
 *            -lSDL2 -lSDL2_ttf -lmingw32 -lm -mwindows
 * ═══════════════════════════════════════════════════════════════ */

static void emit_asm_completo(FILE* f) {
    int tmp[MAX_TRAZA];

    /* ── Cabecera ─────────────────────────────────────────────── */
    fputs(
"; ================================================================\n"
"; Generado por compilador SCARA  —  NASM x64 completo\n"
"; Ensamblar: nasm -f win64 prog.asm -o prog.obj\n"
"; Enlazar  : gcc prog.obj -o prog.exe -L\"C:/msys64/mingw64/lib\"\n"
";            -lSDL2 -lSDL2_ttf -lmingw32 -lm -mwindows\n"
"; ================================================================\n\n"
"default rel\n\n"
    , f);

    /* ── Externs SDL2 ─────────────────────────────────────────── */
    fputs(
"extern SDL_SetMainReady\n"
"extern SDL_Init\n"
"extern SDL_Quit\n"
"extern SDL_GetError\n"
"extern SDL_CreateWindow\n"
"extern SDL_DestroyWindow\n"
"extern SDL_CreateRenderer\n"
"extern SDL_DestroyRenderer\n"
"extern SDL_RenderClear\n"
"extern SDL_RenderPresent\n"
"extern SDL_RenderDrawLine\n"
"extern SDL_RenderDrawPoint\n"
"extern SDL_RenderFillRect\n"
"extern SDL_SetRenderDrawColor\n"
"extern SDL_SetRenderDrawBlendMode\n"
"extern SDL_RenderCopy\n"
"extern SDL_QueryTexture\n"
"extern SDL_CreateTextureFromSurface\n"
"extern SDL_DestroyTexture\n"
"extern SDL_FreeSurface\n"
"extern SDL_PollEvent\n"
"extern SDL_Delay\n"
    , f);

    /* ── Externs SDL2_ttf ─────────────────────────────────────── */
    fputs(
"extern TTF_Init\n"
"extern TTF_Quit\n"
"extern TTF_GetError\n"
"extern TTF_OpenFont\n"
"extern TTF_CloseFont\n"
"extern TTF_RenderText_Blended\n"
"extern TTF_SetFontStyle\n"
    , f);

    /* ── Externs C runtime ────────────────────────────────────── */
    fputs(
"extern sprintf\n"
"extern sqrt\n"
"extern atan2\n"
"extern acos\n"
"extern cos\n"
"extern sin\n\n"
    , f);

    /* ── .data ────────────────────────────────────────────────── */
    fputs("section .data\n\n", f);

    /* Cadenas de uso general */
    fputs(
"    s_win_title  db \"SCARA \xe2\x80\x94 Compilador de Lenguaje Robotico\", 0\n"
"    s_font_path  db \"arial.ttf\", 0\n"
"    f_deg2rad    dq 0.017453292519943295\n"
"    f_rad2deg    dq 57.29577951308232\n"
"    f_one        dq 1.0\n"
"    f_neg_one    dq -1.0\n"
"    f_L1         dq 200.0\n"
"    f_L2         dq 150.0\n"
"    f_den        dq 60000.0\n"
"    f_L1sq_L2sq  dq 62500.0\n"
"    ; ── strings HUD (panel izquierdo) ────────────────────────\n"
"    s_scara_vm   db \"SCARA VM\", 0\n"
"    s_simulacion db \"Simulacion de trayectoria\", 0\n"
"    s_s_pos      db \"POSICION\", 0\n"
"    s_s_cin      db \"CINEMATICA\", 0\n"
"    s_s_vel      db \"VELOCIDAD\", 0\n"
"    s_s_pza      db \"PINZA\", 0\n"
"    s_s_prg      db \"PROGRESO\", 0\n"
"    s_s_vis      db \"VISTA\", 0\n"
"    s_btn_iso    db \"ISO\", 0\n"
"    s_btn_top    db \"TOP\", 0\n"
"    s_btn_lado   db \"LADO\", 0\n"
"    s_v_iso      db \"Isometrica\", 0\n"
"    s_v_top      db \"Superior\", 0\n"
"    s_v_lado     db \"Lateral\", 0\n"
"    s_pinza_a    db \"ABIERTA\", 0\n"
"    s_pinza_c    db \"CERRADA\", 0\n"
"    s_fmt_x      db \"X : %d mm\", 0\n"
"    s_fmt_y      db \"Y : %d mm\", 0\n"
"    s_fmt_z      db \"Z : %d mm\", 0\n"
"    s_fmt_q1     db \"q1: %d deg\", 0\n"
"    s_fmt_q2     db \"q2: %d deg\", 0\n"
"    s_fmt_vel    db \"%d %%\", 0\n"
"    s_fmt_paso   db \"Paso %d / %d\", 0\n"
"    s_vista_iso  db \"VISTA: ISOMETRICA\", 0\n"
"    s_vista_top  db \"VISTA: SUPERIOR\", 0\n"
"    s_vista_lad  db \"VISTA: LATERAL\", 0\n"
"    s_hint1      db \"V: vista  SPACE: pausa\", 0\n"
"    s_hint2      db \"< > KF   ESC: salir\", 0\n"
"    s_fmt_kf     db \"KF %d/%d: %s\", 0\n"
"    s_kfn0       db \"\", 0\n"
"    s_kfn1       db \"HOME\", 0\n"
"    s_kfn2       db \"MOVE\", 0\n"
"    s_kfn3       db \"MOVEJ\", 0\n"
"    s_kfn4       db \"APPROACH\", 0\n"
"    s_kfn5       db \"DEPART\", 0\n"
"    s_kfn6       db \"OPEN\", 0\n"
"    s_kfn7       db \"CLOSE\", 0\n"
"    s_kfn8       db \"SPEED\", 0\n"
"    s_kfn9       db \"WAIT\", 0\n"
"    s_kfn_unk    db \"-\", 0\n"
"    s_kf_names:\n"
"    dq s_kfn0, s_kfn1, s_kfn2, s_kfn3, s_kfn4\n"
"    dq s_kfn5, s_kfn6, s_kfn7, s_kfn8, s_kfn9\n\n"
    , f);

    /* Arrays de traza */
    fprintf(f, "    global traza_len\n    traza_len dd %d\n\n", traza_len);

    fprintf(f, "    global traza_x\n");
    for (int i = 0; i < traza_len; i++) tmp[i] = traza[i].x;
    emit_array_dd(f, "traza_x", tmp, traza_len);

    fprintf(f, "    global traza_y\n");
    for (int i = 0; i < traza_len; i++) tmp[i] = traza[i].y;
    emit_array_dd(f, "traza_y", tmp, traza_len);

    fprintf(f, "    global traza_z\n");
    for (int i = 0; i < traza_len; i++) tmp[i] = traza[i].z;
    emit_array_dd(f, "traza_z", tmp, traza_len);

    fprintf(f, "    global traza_pinza\n");
    for (int i = 0; i < traza_len; i++) tmp[i] = traza[i].pinza;
    emit_array_dd(f, "traza_pinza", tmp, traza_len);

    fprintf(f, "    global traza_vel\n");
    for (int i = 0; i < traza_len; i++) tmp[i] = traza[i].velocidad;
    emit_array_dd(f, "traza_vel", tmp, traza_len);

    /* Keyframes */
    fprintf(f, "\n    traza_kf_len dd %d\n", kf_len);
    emit_array_dd(f, "traza_kf",      kf_idx,  kf_len);
    emit_array_dd(f, "traza_kf_tipo", kf_tipo, kf_len);

    /* ── .bss ─────────────────────────────────────────────────── */
    fputs(
"\nsection .bss\n\n"
"    p_win      resq 1        ; SDL_Window*\n"
"    p_ren      resq 1        ; SDL_Renderer*\n"
"    p_font     resq 1        ; TTF_Font*  normal\n"
"    p_font_b   resq 1        ; TTF_Font*  bold\n"
"    ev_buf     resb 56       ; SDL_Event (56 bytes en x64)\n"
"    modo_vista resd 1        ; 0=ISO  1=TOP  2=LATERAL\n"
"    cur_idx    resd 1        ; indice actual en la traza\n"
"    paused     resd 1        ; 1 = animacion pausada\n"
"    kf_cur     resd 1        ; indice en kf_idx[] para navegacion\n\n"
    , f);

    /* ── .text ────────────────────────────────────────────────── */
    fputs(
"section .text\n\n"
"    global main\n\n"
    , f);

    /* Funciones de acceso a traza (compatibilidad) */
    fputs(
"traza_get_len:\n"
"    lea  rax, [rel traza_len]\n"
"    mov  eax, [rax]\n"
"    ret\n\n"
"traza_get_x:\n"
"    lea  rax, [rel traza_len]\n"
"    mov  eax, [rax]\n"
"    cmp  ecx, eax\n    jge  .zx\n"
"    lea  rax, [rel traza_x]\n"
"    movsxd rcx, ecx\n"
"    mov  eax, [rax + rcx*4]\n    ret\n"
".zx: xor eax, eax\n    ret\n\n"
"traza_get_y:\n"
"    lea  rax, [rel traza_len]\n"
"    mov  eax, [rax]\n"
"    cmp  ecx, eax\n    jge  .zy\n"
"    lea  rax, [rel traza_y]\n"
"    movsxd rcx, ecx\n"
"    mov  eax, [rax + rcx*4]\n    ret\n"
".zy: xor eax, eax\n    ret\n\n"
"traza_get_z:\n"
"    lea  rax, [rel traza_len]\n"
"    mov  eax, [rax]\n"
"    cmp  ecx, eax\n    jge  .zz\n"
"    lea  rax, [rel traza_z]\n"
"    movsxd rcx, ecx\n"
"    mov  eax, [rax + rcx*4]\n    ret\n"
".zz: xor eax, eax\n    ret\n\n"
"traza_get_pinza:\n"
"    lea  rax, [rel traza_len]\n"
"    mov  eax, [rax]\n"
"    cmp  ecx, eax\n    jge  .zp\n"
"    lea  rax, [rel traza_pinza]\n"
"    movsxd rcx, ecx\n"
"    mov  eax, [rax + rcx*4]\n    ret\n"
".zp: xor eax, eax\n    ret\n\n"
"traza_get_vel:\n"
"    lea  rax, [rel traza_len]\n"
"    mov  eax, [rax]\n"
"    cmp  ecx, eax\n    jge  .zv\n"
"    lea  rax, [rel traza_vel]\n"
"    movsxd rcx, ecx\n"
"    mov  eax, [rax + rcx*4]\n    ret\n"
".zv: xor eax, eax\n    ret\n\n"
    , f);

    /* ── Paso 2: Funciones de proyección ─────────────────────────── */
    fputs(
"; ================================================================\n"
"; PROYECCIONES  (x,y,z del mundo → píxeles en pantalla)\n"
"; Convencion Windows x64 leaf: rcx rdx r8 r9 [rsp+40]\n"
"; ================================================================\n\n"

"; ── proj_iso ── codo-arriba, vista isométrica 2D ─────────────────\n"
"; proj_iso(wx, wy, wz, *sx, *sy)\n"
";   *sx = 350 + (wx - wy)*70/100\n"
";   *sy = 300 - ((wx+wy)*32/100  -  wz*85/100)\n"
"proj_iso:\n"
"    mov   r11d, edx\n"           /* wy guardado: cdq clobberea edx       */
"    mov   eax,  ecx\n"           /* eax = wx                             */
"    sub   eax,  edx\n"           /* eax = wx - wy                        */
"    imul  eax,  70\n"
"    cdq\n"
"    mov   r10d, 100\n"
"    idiv  r10d\n"                /* eax = (wx-wy)*70/100                 */
"    add   eax,  350\n"
"    mov   [r9], eax\n"           /* *sx = resultado                      */
"    mov   r10d, ecx\n"           /* r10d = wx                            */
"    add   r10d, r11d\n"          /* r10d = wx + wy  (wy desde r11d)      */
"    imul  r10d, 32\n"
"    mov   eax,  r10d\n"
"    cdq\n"
"    mov   r10d, 100\n"
"    idiv  r10d\n"                /* eax = (wx+wy)*32/100                 */
"    mov   r10d, eax\n"
"    mov   eax,  r8d\n"           /* eax = wz                             */
"    imul  eax,  85\n"
"    cdq\n"
"    mov   r11d, 100\n"
"    idiv  r11d\n"                /* eax = wz*85/100                      */
"    sub   r10d, eax\n"           /* r10d = (wx+wy)*32/100 - wz*85/100   */
"    mov   eax,  300\n"
"    sub   eax,  r10d\n"          /* eax = 300 - (...)                    */
"    mov   r11,  [rsp+40]\n"      /* r11 = ptr *sy                        */
"    mov   [r11], eax\n"
"    ret\n\n"

"; ── proj_top ── vista superior (planta) ──────────────────────────\n"
"; proj_top(wx, wy, wz, *sx, *sy)\n"
";   *sx = 350 + wx*75/100\n"
";   *sy = 300 - wy*75/100\n"
"proj_top:\n"
"    mov   r11d, edx\n"           /* wy guardado                          */
"    mov   eax,  ecx\n"           /* eax = wx                             */
"    imul  eax,  75\n"
"    cdq\n"
"    mov   r10d, 100\n"
"    idiv  r10d\n"                /* eax = wx*75/100                      */
"    add   eax,  350\n"
"    mov   [r9], eax\n"           /* *sx = resultado                      */
"    mov   eax,  r11d\n"          /* eax = wy (guardado)                  */
"    imul  eax,  75\n"
"    cdq\n"
"    mov   r10d, 100\n"
"    idiv  r10d\n"                /* eax = wy*75/100                      */
"    mov   r10d, 300\n"
"    sub   r10d, eax\n"           /* r10d = 300 - wy*75/100               */
"    mov   r11,  [rsp+40]\n"
"    mov   [r11], r10d\n"
"    ret\n\n"

"; ── proj_lado ── vista lateral (usa SSE2 sqrtsd, sigue siendo leaf)\n"
"; proj_lado(wx, wy, wz, *sx, *sy)\n"
";   r   = (int)sqrt(wx^2 + wy^2)\n"
";   *sx = 40  + r*620/370\n"
";   *sy = 560 - wz*520/370\n"
"proj_lado:\n"
"    movsxd rax, ecx\n"           /* rax = (int64)wx                      */
"    imul   rax, rax\n"           /* rax = wx^2                           */
"    movsxd r10, edx\n"           /* r10 = (int64)wy                      */
"    imul   r10, r10\n"           /* r10 = wy^2                           */
"    add    rax, r10\n"           /* rax = wx^2 + wy^2                    */
"    cvtsi2sd  xmm0, rax\n"       /* xmm0 = (double)rax                   */
"    sqrtsd    xmm0, xmm0\n"      /* xmm0 = sqrt(...)   (SSE2, sin call)  */
"    cvttsd2si eax,  xmm0\n"      /* eax  = (int)r      (truncar)         */
"    imul  eax,  620\n"           /* eax  = r*620                         */
"    cdq\n"
"    mov   r11d, 370\n"
"    idiv  r11d\n"                /* eax = r*620/370                      */
"    add   eax,  40\n"
"    mov   [r9], eax\n"           /* *sx = resultado                      */
"    mov   eax,  r8d\n"           /* eax = wz                             */
"    imul  eax,  520\n"
"    cdq\n"
"    mov   r10d, 370\n"
"    idiv  r10d\n"                /* eax = wz*520/370                     */
"    mov   r10d, 560\n"
"    sub   r10d, eax\n"           /* r10d = 560 - wz*520/370              */
"    mov   r11,  [rsp+40]\n"
"    mov   [r11], r10d\n"
"    ret\n\n"

"; ── proj ── despachador según modo_vista ─────────────────────────\n"
"; proj(wx, wy, wz, *sx, *sy)  —  no-leaf: llama a sub-función\n"
";   modo_vista: 0=ISO  1=TOP  2=LATERAL\n"
"; Stack frame: push rbp / push rbx / sub rsp,40\n"
";   rbp+48 = arg5 (*sy)   rsp+32 = 5to arg para sub-call\n"
"proj:\n"
"    push  rbp\n"
"    mov   rbp, rsp\n"
"    push  rbx\n"
"    sub   rsp, 40\n"           /* 32 shadow + 8 alineacion (64 total)  */
"    mov   rbx, r9\n"           /* rbx = ptr *sx  (callee-save)          */
"    mov   r11, [rbp+48]\n"     /* r11 = ptr *sy  (arg5 en el frame)     */
"    mov   [rsp+32], r11\n"     /* -> 5to arg para sub-llamadas           */
"    lea   rax, [rel modo_vista]\n"
"    mov   eax, [rax]\n"
"    test  eax, eax\n"
"    jz    .piso\n"
"    cmp   eax, 1\n"
"    je    .ptop\n"
"    call  proj_lado\n"
"    jmp   .pdone\n"
".piso:\n"
"    call  proj_iso\n"
"    jmp   .pdone\n"
".ptop:\n"
"    call  proj_top\n"
".pdone:\n"
"    mov   eax, [rbx]\n"        /* *sx += PANEL_W (200)                  */
"    add   eax, 200\n"
"    mov   [rbx], eax\n"
"    lea   rsp, [rbp-8]\n"      /* restaurar hasta rbx guardado          */
"    pop   rbx\n"
"    pop   rbp\n"
"    ret\n\n"
    , f);

    /* ── Helper: draw_sep (línea separadora horizontal en HUD) ──── */
    fputs(
"; ================================================================\n"
"; draw_sep(y)  — dibuja línea separadora en x=[8..192] a altura y\n"
"; rcx = y   (Win x64 leaf-like but calls SDL, so uses frame)\n"
"; Frame: push rbp/rbx  sub rsp,40  (64 bytes total)\n"
"; ================================================================\n"
"draw_sep:\n"
"    push  rbp\n"
"    mov   rbp, rsp\n"
"    push  rbx\n"
"    sub   rsp, 40\n"
"    mov   ebx, ecx\n"              /* save y                               */
"    mov   rcx, [rel p_ren]\n"
"    mov   edx, 50\n"
"    mov   r8d, 70\n"
"    mov   r9d, 110\n"
"    mov   dword [rsp+32], 255\n"
"    call  SDL_SetRenderDrawColor\n"
"    mov   rcx, [rel p_ren]\n"
"    mov   edx, 8\n"
"    mov   r8d, ebx\n"
"    mov   r9d, 192\n"
"    mov   [rsp+32], ebx\n"         /* y2 = y (línea horizontal)            */
"    call  SDL_RenderDrawLine\n"
"    lea   rsp, [rbp-8]\n"
"    pop   rbx\n"
"    pop   rbp\n"
"    ret\n\n"
    , f);

    /* ── Paso 3: draw_circle (Bresenham, 8-simetría) ─────────────── */
    fputs(
"; ================================================================\n"
"; draw_circle(cx, cy, r)  —  circunferencia entera Bresenham\n"
"; Caller must call SDL_SetRenderDrawColor before this function.\n"
"; rcx=cx  rdx=cy  r8=r\n"
"; Frame: push rbp/rbx/r12/r13/r14/r15  sub rsp,40  (96 bytes total)\n"
";   rbx=cx  r12=cy  r13=d  r14=x  r15=y\n"
"; ================================================================\n"
"draw_circle:\n"
"    push  rbp\n"
"    mov   rbp, rsp\n"
"    push  rbx\n"
"    push  r12\n"
"    push  r13\n"
"    push  r14\n"
"    push  r15\n"
"    sub   rsp, 40\n\n"

"    mov   ebx,  ecx\n"           /* rbx = cx  (zero-extendido)            */
"    mov   r12d, edx\n"           /* r12 = cy                              */
"    mov   r14d, r8d\n"           /* r14 = x  = radius                     */
"    xor   r15d, r15d\n"          /* r15 = y  = 0                          */
"    mov   r13d, 1\n"
"    sub   r13d, r8d\n"           /* r13 = d  = 1 - r                      */

".dc_loop:\n"
"    cmp   r14d, r15d\n"
"    jl    .dc_done\n\n"          /* while x >= y                          */

"    ; ─ punto (cx+x, cy+y) ─\n"
"    mov   rcx, [rel p_ren]\n"
"    mov   edx, ebx\n"
"    add   edx, r14d\n"
"    mov   r8d, r12d\n"
"    add   r8d, r15d\n"
"    call  SDL_RenderDrawPoint\n\n"

"    ; ─ punto (cx-x, cy+y) ─\n"
"    mov   rcx, [rel p_ren]\n"
"    mov   edx, ebx\n"
"    sub   edx, r14d\n"
"    mov   r8d, r12d\n"
"    add   r8d, r15d\n"
"    call  SDL_RenderDrawPoint\n\n"

"    ; ─ punto (cx+x, cy-y) ─\n"
"    mov   rcx, [rel p_ren]\n"
"    mov   edx, ebx\n"
"    add   edx, r14d\n"
"    mov   r8d, r12d\n"
"    sub   r8d, r15d\n"
"    call  SDL_RenderDrawPoint\n\n"

"    ; ─ punto (cx-x, cy-y) ─\n"
"    mov   rcx, [rel p_ren]\n"
"    mov   edx, ebx\n"
"    sub   edx, r14d\n"
"    mov   r8d, r12d\n"
"    sub   r8d, r15d\n"
"    call  SDL_RenderDrawPoint\n\n"

"    ; ─ punto (cx+y, cy+x) ─\n"
"    mov   rcx, [rel p_ren]\n"
"    mov   edx, ebx\n"
"    add   edx, r15d\n"
"    mov   r8d, r12d\n"
"    add   r8d, r14d\n"
"    call  SDL_RenderDrawPoint\n\n"

"    ; ─ punto (cx-y, cy+x) ─\n"
"    mov   rcx, [rel p_ren]\n"
"    mov   edx, ebx\n"
"    sub   edx, r15d\n"
"    mov   r8d, r12d\n"
"    add   r8d, r14d\n"
"    call  SDL_RenderDrawPoint\n\n"

"    ; ─ punto (cx+y, cy-x) ─\n"
"    mov   rcx, [rel p_ren]\n"
"    mov   edx, ebx\n"
"    add   edx, r15d\n"
"    mov   r8d, r12d\n"
"    sub   r8d, r14d\n"
"    call  SDL_RenderDrawPoint\n\n"

"    ; ─ punto (cx-y, cy-x) ─\n"
"    mov   rcx, [rel p_ren]\n"
"    mov   edx, ebx\n"
"    sub   edx, r15d\n"
"    mov   r8d, r12d\n"
"    sub   r8d, r14d\n"
"    call  SDL_RenderDrawPoint\n\n"

"    ; ── actualizar Bresenham ──────────────────────────────────\n"
"    inc   r15d\n"              /* y++ (antes de decidir rama)            */
"    test  r13d, r13d\n"
"    js    .dc_neg\n"           /* d < 0 → rama negativa                  */

"    ; d >= 0: x--,  d += 2*(y-x) + 1\n"
"    dec   r14d\n"              /* x--                                    */
"    mov   eax, r15d\n"
"    sub   eax, r14d\n"         /* eax = y - x  (con x ya decrementado)   */
"    add   eax, eax\n"          /* 2*(y-x)                                */
"    inc   eax\n"               /* +1                                     */
"    add   r13d, eax\n"
"    jmp   .dc_loop\n\n"

".dc_neg:\n"
"    ; d < 0:  d += 2*y + 1\n"
"    mov   eax, r15d\n"
"    add   eax, eax\n"          /* 2*y                                    */
"    inc   eax\n"               /* +1                                     */
"    add   r13d, eax\n"
"    jmp   .dc_loop\n\n"

".dc_done:\n"
"    add   rsp, 40\n"
"    pop   r15\n"
"    pop   r14\n"
"    pop   r13\n"
"    pop   r12\n"
"    pop   rbx\n"
"    pop   rbp\n"
"    ret\n\n"
    , f);

    /* ── Paso 4: draw_arc (cos/sin CRT, iteracion por grados) ────── */
    fputs(
"; ================================================================\n"
"; draw_arc(cx, cy, r, a0_deg, a1_deg)\n"
";   Dibuja arco de a0_deg a a1_deg (inclusive) usando cos/sin CRT\n"
";   rcx=cx  rdx=cy  r8=r  r9=a0_deg  [rsp+40]=a1_deg  (en caller)\n"
"; Frame: push rbp/rbx/r12/r13/r14/r15  sub rsp,40  (96 bytes)\n"
";   rbx=cx  r12=cy  r13=r  r14=angulo_actual  r15=a1_deg\n"
";   [rsp+32] = px temporal entre llamada cos y llamada sin\n"
"; ================================================================\n"
"draw_arc:\n"
"    push  rbp\n"
"    mov   rbp, rsp\n"
"    push  rbx\n"
"    push  r12\n"
"    push  r13\n"
"    push  r14\n"
"    push  r15\n"
"    sub   rsp, 40\n\n"              /* 96 bytes total, 16-alineado             */

"    mov   ebx,  ecx\n"              /* rbx = cx                                */
"    mov   r12d, edx\n"              /* r12 = cy                                */
"    mov   r13d, r8d\n"              /* r13 = r                                 */
"    mov   r14d, r9d\n"              /* r14 = angulo actual (= a0_deg)          */
"    mov   r15d, [rbp+48]\n"         /* r15 = a1_deg  (5to arg)                 */

".da_loop:\n"
"    cmp   r14d, r15d\n"
"    jg    .da_done\n\n"             /* while angulo <= a1_deg                  */

"    ; ── calcular cos(angulo) ──────────────────────────────────\n"
"    cvtsi2sd  xmm0, r14d\n"         /* xmm0 = (double)angulo                   */
"    mulsd     xmm0, [rel f_deg2rad]\n" /* xmm0 *= pi/180                     */
"    call      cos\n"                /* xmm0 = cos(angulo_rad)                  */

"    ; px = (int)(cx + r * cos)\n"
"    cvtsi2sd  xmm1, r13d\n"         /* xmm1 = (double)r                        */
"    mulsd     xmm0, xmm1\n"         /* xmm0 = r * cos                          */
"    cvtsi2sd  xmm1, ebx\n"          /* xmm1 = (double)cx                       */
"    addsd     xmm0, xmm1\n"         /* xmm0 = cx + r*cos                       */
"    cvttsd2si eax,  xmm0\n"         /* eax  = px  (truncar a entero)           */
"    mov       [rsp+32], eax\n"      /* guardar px (fuera del shadow [0..31])   */

"    ; ── calcular sin(angulo) ──────────────────────────────────\n"
"    cvtsi2sd  xmm0, r14d\n"         /* xmm0 = (double)angulo  (recomputar)     */
"    mulsd     xmm0, [rel f_deg2rad]\n"
"    call      sin\n"                /* xmm0 = sin(angulo_rad)                  */

"    ; py = (int)(cy + r * sin)\n"
"    cvtsi2sd  xmm1, r13d\n"         /* xmm1 = (double)r                        */
"    mulsd     xmm0, xmm1\n"         /* xmm0 = r * sin                          */
"    cvtsi2sd  xmm1, r12d\n"         /* xmm1 = (double)cy                       */
"    addsd     xmm0, xmm1\n"         /* xmm0 = cy + r*sin                       */
"    cvttsd2si r8d,  xmm0\n"         /* r8d  = py                               */

"    ; ── SDL_RenderDrawPoint(ren, px, py) ─────────────────────\n"
"    mov       rcx, [rel p_ren]\n"
"    mov       edx, [rsp+32]\n"      /* px guardado                             */
"    call      SDL_RenderDrawPoint\n\n"

"    inc   r14d\n"                   /* angulo++                                */
"    jmp   .da_loop\n\n"

".da_done:\n"
"    add   rsp, 40\n"
"    pop   r15\n"
"    pop   r14\n"
"    pop   r13\n"
"    pop   r12\n"
"    pop   rbx\n"
"    pop   rbp\n"
"    ret\n\n"
    , f);

    /* ── Paso 5: ik_codo — cinemática inversa SCARA en NASM ──────── */
    fputs(
"; ================================================================\n"
"; ik_codo(x, y, modo, *q1_deg, *q2_deg) -> eax=1 ok / 0 fuera\n"
";   rcx=x  rdx=y  r8=modo  r9=*q1_deg  [rsp+40]=*q2_deg\n"
"; L1=200  L2=150   CODO_ARRIBA=0  CODO_ABAJO=1\n"
"; Algoritmo:\n"
";   c2 = (x^2+y^2 - 62500) / 60000     (cos de q2)\n"
";   q2 = acos(c2)  [negado si CODO_ARRIBA]\n"
";   k1 = L1 + L2*cos(q2)  ,  k2 = L2*sin(q2)\n"
";   q1 = atan2(y,x) - atan2(k2,k1)     (en radianes)\n"
"; Frame: push rbp/rbx/r12/r13/r14/r15  sub rsp,72  (128 bytes)\n"
";   rbx=x  r12=y  r13=modo  r14=*q1_deg  r15=*q2_deg\n"
";   stack locals (fuera del shadow [0..31]):\n"
";     [rsp+32]=q2  [rsp+40]=k1  [rsp+48]=k2  [rsp+56]=atan2(y,x)\n"
"; ================================================================\n"
"ik_codo:\n"
"    push  rbp\n"
"    mov   rbp, rsp\n"
"    push  rbx\n"
"    push  r12\n"
"    push  r13\n"
"    push  r14\n"
"    push  r15\n"
"    sub   rsp, 72\n\n"              /* 128 bytes total, 16-alineado        */

"    mov   ebx,  ecx\n"              /* x                                   */
"    mov   r12d, edx\n"              /* y                                   */
"    mov   r13d, r8d\n"              /* modo                                */
"    mov   r14,  r9\n"               /* *q1_deg                             */
"    mov   r15,  [rbp+48]\n"         /* *q2_deg  (5to arg)                  */

"    ; ── r2 = x^2 + y^2 ───────────────────────────────────────\n"
"    cvtsi2sd  xmm0, ebx\n"
"    mulsd     xmm0, xmm0\n"         /* x^2                                 */
"    cvtsi2sd  xmm1, r12d\n"
"    mulsd     xmm1, xmm1\n"         /* y^2                                 */
"    addsd     xmm0, xmm1\n"         /* xmm0 = r2 = x^2 + y^2              */

"    ; ── c2 = (r2 - 62500) / 60000 ────────────────────────────\n"
"    subsd     xmm0, [rel f_L1sq_L2sq]\n"
"    divsd     xmm0, [rel f_den]\n"  /* xmm0 = c2                           */

"    ; ── rango: -1 <= c2 <= 1 ──────────────────────────────────\n"
"    ucomisd   xmm0, [rel f_neg_one]\n"
"    jb        .ik_fail\n"           /* c2 < -1.0 o NaN  →  fuera de alcance */
"    ucomisd   xmm0, [rel f_one]\n"
"    ja        .ik_fail\n"           /* c2 >  1.0        →  fuera de alcance */

"    ; ── q2 = acos(c2) ────────────────────────────────────────\n"
"    call      acos\n"               /* xmm0 = q2 (radianes, 0..pi)         */

"    ; ── si CODO_ARRIBA (modo=0): q2 = -q2 ───────────────────\n"
"    test      r13d, r13d\n"
"    jnz       .ik_modo_abajo\n"
"    xorpd     xmm1, xmm1\n"        /* xmm1 = 0.0                          */
"    subsd     xmm1, xmm0\n"        /* xmm1 = -q2                          */
"    movsd     xmm0, xmm1\n"
".ik_modo_abajo:\n"
"    movsd     [rsp+32], xmm0\n"    /* guardar q2 definitivo               */

"    ; ── k1 = L1 + L2*cos(q2) ─────────────────────────────────\n"
"    call      cos\n"               /* xmm0 = cos(q2)                      */
"    mulsd     xmm0, [rel f_L2]\n"  /* L2*cos(q2)                          */
"    addsd     xmm0, [rel f_L1]\n"  /* k1 = L1 + L2*cos(q2)               */
"    movsd     [rsp+40], xmm0\n"    /* guardar k1                          */

"    ; ── k2 = L2*sin(q2) ──────────────────────────────────────\n"
"    movsd     xmm0, [rsp+32]\n"    /* recargar q2                         */
"    call      sin\n"               /* xmm0 = sin(q2)                      */
"    mulsd     xmm0, [rel f_L2]\n"  /* k2 = L2*sin(q2)                    */
"    movsd     [rsp+48], xmm0\n"    /* guardar k2                          */

"    ; ── atan2(y, x) ───────────────────────────────────────────\n"
"    cvtsi2sd  xmm0, r12d\n"        /* xmm0 = (double)y                    */
"    cvtsi2sd  xmm1, ebx\n"         /* xmm1 = (double)x                    */
"    call      atan2\n"             /* xmm0 = atan2(y,x)                   */
"    movsd     [rsp+56], xmm0\n"    /* guardar atan2(y,x)                  */

"    ; ── atan2(k2, k1) ─────────────────────────────────────────\n"
"    movsd     xmm0, [rsp+48]\n"    /* k2                                  */
"    movsd     xmm1, [rsp+40]\n"    /* k1                                  */
"    call      atan2\n"             /* xmm0 = atan2(k2,k1)                */

"    ; ── q1_rad = atan2(y,x) - atan2(k2,k1) ──────────────────\n"
"    movsd     xmm1, xmm0\n"
"    movsd     xmm0, [rsp+56]\n"
"    subsd     xmm0, xmm1\n"        /* xmm0 = q1_rad                       */

"    ; ── q1 en grados + normalizar a [-180,180] ───────────────\n"
"    mulsd     xmm0, [rel f_rad2deg]\n"
"    cvtsd2si  eax,  xmm0\n"        /* round-to-nearest (equiv. lround)    */
".ik_n1_hi:\n"
"    cmp       eax, 180\n"
"    jle       .ik_n1_lo\n"
"    sub       eax, 360\n"
"    jmp       .ik_n1_hi\n"
".ik_n1_lo:\n"
"    cmp       eax, -180\n"
"    jg        .ik_n1_ok\n"
"    add       eax, 360\n"
"    jmp       .ik_n1_lo\n"
".ik_n1_ok:\n"
"    mov       [r14], eax\n"         /* *q1_deg = q1d                       */

"    ; ── q2 en grados ─────────────────────────────────────────\n"
"    movsd     xmm0, [rsp+32]\n"    /* q2 (radianes)                       */
"    mulsd     xmm0, [rel f_rad2deg]\n"
"    cvtsd2si  eax,  xmm0\n"
"    mov       [r15], eax\n"         /* *q2_deg = q2d                       */

"    mov       eax, 1\n"             /* return 1 (exito)                    */
"    jmp       .ik_ret\n\n"

".ik_fail:\n"
"    xor       eax, eax\n"           /* return 0 (fuera de alcance)         */
".ik_ret:\n"
"    add       rsp, 72\n"
"    pop       r15\n"
"    pop       r14\n"
"    pop       r13\n"
"    pop       r12\n"
"    pop       rbx\n"
"    pop       rbp\n"
"    ret\n\n"
    , f);

    /* ── Paso 6: draw_text (TTF → Surface → Texture → RenderCopy) ── */
    fputs(
"; ================================================================\n"
"; draw_text(font*, text*, x, y, color_rgba)\n"
";   rcx=font  rdx=text  r8d=x  r9d=y  [rsp+40]=color (en caller)\n"
";   color = RGBA empaquetado en uint32  ej. 0xFFFFFFFF = blanco\n"
"; Frame: push rbp/rbx/r12/r13/r14  sub rsp,80  (128 bytes)\n"
";   rbx=font→surface  r12=text→texture  r13d=x  r14d=y\n"
";   [rsp+32] = &h_local para QueryTexture (5to arg)\n"
";   [rsp+48..63] = SDL_Rect dst   [rsp+64]=w   [rsp+68]=h\n"
"; ================================================================\n"
"draw_text:\n"
"    push  rbp\n"
"    mov   rbp, rsp\n"
"    push  rbx\n"
"    push  r12\n"
"    push  r13\n"
"    push  r14\n"
"    sub   rsp, 80\n\n"              /* 128 bytes total, 16-alineado        */

"    mov   rbx,  rcx\n"              /* font*                               */
"    mov   r12,  rdx\n"              /* text*                               */
"    mov   r13d, r8d\n"              /* x                                   */
"    mov   r14d, r9d\n"              /* y                                   */

"    ; ── TTF_RenderText_Blended(font, text, color) ──────────────\n"
"    mov   rcx, rbx\n"
"    mov   rdx, r12\n"
"    mov   r8d, [rbp+48]\n"          /* color uint32 (SDL_Color por valor)  */
"    call  TTF_RenderText_Blended\n"
"    test  rax, rax\n"
"    jz    .dt_ret\n"                /* surface NULL → salir                */
"    mov   rbx, rax\n"               /* rbx = surface*                      */

"    ; ── SDL_CreateTextureFromSurface(ren, surface) ─────────────\n"
"    mov   rcx, [rel p_ren]\n"
"    mov   rdx, rbx\n"
"    call  SDL_CreateTextureFromSurface\n"
"    mov   r12, rax\n"               /* r12 = texture* (puede ser NULL)     */

"    ; ── SDL_FreeSurface(surface) — liberar siempre ─────────────\n"
"    mov   rcx, rbx\n"
"    call  SDL_FreeSurface\n"

"    test  r12, r12\n"
"    jz    .dt_ret\n"                /* texture NULL → salir                */

"    ; ── SDL_QueryTexture(tex, NULL, NULL, &w, &h) ───────────────\n"
"    mov   rcx, r12\n"               /* texture                             */
"    xor   edx, edx\n"               /* NULL (format*)                      */
"    xor   r8d, r8d\n"               /* NULL (access*)                      */
"    lea   r9,  [rsp+64]\n"          /* &w_local                            */
"    lea   rax, [rsp+68]\n"
"    mov   [rsp+32], rax\n"          /* &h_local  (5to arg)                 */
"    call  SDL_QueryTexture\n"

"    ; ── armar SDL_Rect dst = { x, y, w, h } ─────────────────────\n"
"    mov   eax, r13d\n"
"    mov   [rsp+48], eax\n"          /* dst.x                               */
"    mov   eax, r14d\n"
"    mov   [rsp+52], eax\n"          /* dst.y                               */
"    mov   eax, [rsp+64]\n"
"    mov   [rsp+56], eax\n"          /* dst.w  (de QueryTexture)            */
"    mov   eax, [rsp+68]\n"
"    mov   [rsp+60], eax\n"          /* dst.h  (de QueryTexture)            */

"    ; ── SDL_RenderCopy(ren, tex, NULL, &dst) ────────────────────\n"
"    mov   rcx, [rel p_ren]\n"
"    mov   rdx, r12\n"               /* texture                             */
"    xor   r8d, r8d\n"               /* NULL (srcrect)                      */
"    lea   r9,  [rsp+48]\n"          /* &dst                                */
"    call  SDL_RenderCopy\n"

"    ; ── SDL_DestroyTexture(tex) ──────────────────────────────────\n"
"    mov   rcx, r12\n"
"    call  SDL_DestroyTexture\n"

".dt_ret:\n"
"    add   rsp, 80\n"
"    pop   r14\n"
"    pop   r13\n"
"    pop   r12\n"
"    pop   rbx\n"
"    pop   rbp\n"
"    ret\n\n"
    , f);

    /* ── Paso 7: draw_hud (panel izquierdo, estilo original) ──────── */
    fputs(
"; ================================================================\n"
"; draw_hud(idx)  — panel izquierdo 200px, igual que input_vis.c\n"
"; rcx = idx\n"
"; Frame: push rbp/rbx/r12/r13/r14/r15  sub rsp,168  (224 bytes)\n"
";   rbx=idx  r12d=wx  r13d=wy  r14d=wz  r15d=vel\n"
";   [rsp+48]=q1_deg  [rsp+52]=q2_deg  [rsp+56]=pinza\n"
";   [rsp+60]=vel_bar_w  [rsp+64..127]=text_buf\n"
";   [rsp+128..143]=rect1  [rsp+144..159]=rect2\n"
"; ================================================================\n"
"draw_hud:\n"
"    push  rbp\n"
"    mov   rbp, rsp\n"
"    push  rbx\n"
"    push  r12\n"
"    push  r13\n"
"    push  r14\n"
"    push  r15\n"
"    sub   rsp, 168\n\n"

"    mov   ebx, ecx\n"

"    ; ── cargar traza[idx] ────────────────────────────────────\n"
"    lea   r10, [rel traza_x]\n"
"    mov   r12d, [r10 + rbx*4]\n"
"    lea   r10, [rel traza_y]\n"
"    mov   r13d, [r10 + rbx*4]\n"
"    lea   r10, [rel traza_z]\n"
"    mov   r14d, [r10 + rbx*4]\n"
"    lea   r10, [rel traza_pinza]\n"
"    mov   eax,  [r10 + rbx*4]\n"
"    mov   [rsp+56], eax\n"
"    lea   r10, [rel traza_vel]\n"
"    mov   r15d, [r10 + rbx*4]\n\n"

"    ; ── IK ───────────────────────────────────────────────────\n"
"    mov   ecx, r12d\n"
"    mov   edx, r13d\n"
"    xor   r8d, r8d\n"
"    lea   r9,  [rsp+48]\n"
"    lea   rax, [rsp+52]\n"
"    mov   [rsp+32], rax\n"
"    call  ik_codo\n\n"

"    ; vel_bar_w = 176 * vel / 100\n"
"    mov   eax, r15d\n"
"    imul  eax, 176\n"
"    cdq\n"
"    mov   ecx, 100\n"
"    idiv  ecx\n"
"    mov   [rsp+60], eax\n\n"

"    ; ── fondo panel con blend ────────────────────────────────\n"
"    mov   rcx, [rel p_ren]\n"
"    mov   edx, 1\n"               /* SDL_BLENDMODE_BLEND                  */
"    call  SDL_SetRenderDrawBlendMode\n"
"    mov   rcx, [rel p_ren]\n"
"    mov   edx, 10\n"
"    mov   r8d, 10\n"
"    mov   r9d, 20\n"
"    mov   dword [rsp+32], 210\n"
"    call  SDL_SetRenderDrawColor\n"
"    mov   dword [rsp+128], 0\n"
"    mov   dword [rsp+132], 0\n"
"    mov   dword [rsp+136], 200\n"
"    mov   dword [rsp+140], 600\n"
"    mov   rcx, [rel p_ren]\n"
"    lea   rdx, [rsp+128]\n"
"    call  SDL_RenderFillRect\n"
"    mov   rcx, [rel p_ren]\n"
"    xor   edx, edx\n"             /* SDL_BLENDMODE_NONE                   */
"    call  SDL_SetRenderDrawBlendMode\n\n"

"    ; ── borde derecho (60,80,120) ────────────────────────────\n"
"    mov   rcx, [rel p_ren]\n"
"    mov   edx, 60\n"
"    mov   r8d, 80\n"
"    mov   r9d, 120\n"
"    mov   dword [rsp+32], 255\n"
"    call  SDL_SetRenderDrawColor\n"
"    mov   rcx, [rel p_ren]\n"
"    mov   edx, 200\n"
"    xor   r8d, r8d\n"
"    mov   r9d, 200\n"
"    mov   dword [rsp+32], 600\n"
"    call  SDL_RenderDrawLine\n\n"

"    ; ── 'SCARA VM' bold azul ─────────────────────────────────\n"
"    mov   rcx, [rel p_font_b]\n"
"    lea   rdx, [rel s_scara_vm]\n"
"    mov   r8d, 12\n"
"    mov   r9d, 12\n"
"    mov   dword [rsp+32], 0xFFFFB464\n"  /* R=100,G=180,B=255,A=255      */
"    call  draw_text\n\n"

"    ; ── subtítulo gris ───────────────────────────────────────\n"
"    mov   rcx, [rel p_font]\n"
"    lea   rdx, [rel s_simulacion]\n"
"    mov   r8d, 12\n"
"    mov   r9d, 32\n"
"    mov   dword [rsp+32], 0xFFA08C8C\n"  /* R=140,G=140,B=160,A=255      */
"    call  draw_text\n\n"

"    mov   ecx, 56\n    call  draw_sep\n\n"  /* separator y=56             */

"    ; ── POSICION ─────────────────────────────────────────────\n"
"    mov   rcx, [rel p_font_b]\n"
"    lea   rdx, [rel s_s_pos]\n"
"    mov   r8d, 12\n"
"    mov   r9d, 64\n"
"    mov   dword [rsp+32], 0xFFC8B4B4\n"  /* R=180,G=180,B=200            */
"    call  draw_text\n"

"    lea   rcx, [rsp+64]\n    lea   rdx, [rel s_fmt_x]\n"
"    mov   r8d, r12d\n    call  sprintf\n"
"    mov   rcx, [rel p_font]\n    lea   rdx, [rsp+64]\n"
"    mov   r8d, 12\n    mov   r9d, 84\n"
"    mov   dword [rsp+32], 0xFFFFDCDC\n    call  draw_text\n\n"  /* R=220,G=220,B=255 */

"    lea   rcx, [rsp+64]\n    lea   rdx, [rel s_fmt_y]\n"
"    mov   r8d, r13d\n    call  sprintf\n"
"    mov   rcx, [rel p_font]\n    lea   rdx, [rsp+64]\n"
"    mov   r8d, 12\n    mov   r9d, 104\n"
"    mov   dword [rsp+32], 0xFFFFDCDC\n    call  draw_text\n\n"

"    lea   rcx, [rsp+64]\n    lea   rdx, [rel s_fmt_z]\n"
"    mov   r8d, r14d\n    call  sprintf\n"
"    mov   rcx, [rel p_font]\n    lea   rdx, [rsp+64]\n"
"    mov   r8d, 12\n    mov   r9d, 124\n"
"    mov   dword [rsp+32], 0xFFFFDCDC\n    call  draw_text\n\n"

"    mov   ecx, 148\n    call  draw_sep\n\n"

"    ; ── CINEMATICA ───────────────────────────────────────────\n"
"    mov   rcx, [rel p_font_b]\n"
"    lea   rdx, [rel s_s_cin]\n"
"    mov   r8d, 12\n    mov   r9d, 156\n"
"    mov   dword [rsp+32], 0xFFC8B4B4\n    call  draw_text\n\n"

"    lea   rcx, [rsp+64]\n    lea   rdx, [rel s_fmt_q1]\n"
"    mov   r8d, [rsp+48]\n    call  sprintf\n"
"    mov   rcx, [rel p_font]\n    lea   rdx, [rsp+64]\n"
"    mov   r8d, 12\n    mov   r9d, 176\n"
"    mov   dword [rsp+32], 0xFFB4DCB4\n    call  draw_text\n\n"  /* R=180,G=220,B=180 */

"    lea   rcx, [rsp+64]\n    lea   rdx, [rel s_fmt_q2]\n"
"    mov   r8d, [rsp+52]\n    call  sprintf\n"
"    mov   rcx, [rel p_font]\n    lea   rdx, [rsp+64]\n"
"    mov   r8d, 12\n    mov   r9d, 196\n"
"    mov   dword [rsp+32], 0xFFB4DCB4\n    call  draw_text\n\n"

"    mov   ecx, 220\n    call  draw_sep\n\n"

"    ; ── VELOCIDAD ────────────────────────────────────────────\n"
"    mov   rcx, [rel p_font_b]\n"
"    lea   rdx, [rel s_s_vel]\n"
"    mov   r8d, 12\n    mov   r9d, 228\n"
"    mov   dword [rsp+32], 0xFFC8B4B4\n    call  draw_text\n\n"

"    lea   rcx, [rsp+64]\n    lea   rdx, [rel s_fmt_vel]\n"
"    mov   r8d, r15d\n    call  sprintf\n"
"    mov   rcx, [rel p_font]\n    lea   rdx, [rsp+64]\n"
"    mov   r8d, 12\n    mov   r9d, 248\n"
"    mov   dword [rsp+32], 0xFF32B4FF\n    call  draw_text\n\n"  /* R=255,G=180,B=50 */

"    ; ── barra velocidad ──────────────────────────────────────\n"
"    mov   rcx, [rel p_ren]\n"
"    mov   edx, 40\n    mov   r8d, 40\n    mov   r9d, 60\n"
"    mov   dword [rsp+32], 255\n    call  SDL_SetRenderDrawColor\n"
"    mov   dword [rsp+128], 12\n    mov   dword [rsp+132], 268\n"
"    mov   dword [rsp+136], 176\n    mov   dword [rsp+140], 10\n"
"    mov   rcx, [rel p_ren]\n    lea   rdx, [rsp+128]\n    call  SDL_RenderFillRect\n"
"    mov   rcx, [rel p_ren]\n"
"    mov   edx, 255\n    mov   r8d, 160\n    mov   r9d, 40\n"
"    mov   dword [rsp+32], 255\n    call  SDL_SetRenderDrawColor\n"
"    mov   dword [rsp+128], 12\n    mov   dword [rsp+132], 268\n"
"    mov   eax, [rsp+60]\n    mov   [rsp+136], eax\n"          /* vel_bar_w */
"    mov   dword [rsp+140], 10\n"
"    mov   rcx, [rel p_ren]\n    lea   rdx, [rsp+128]\n    call  SDL_RenderFillRect\n\n"

"    mov   ecx, 288\n    call  draw_sep\n\n"

"    ; ── PINZA ────────────────────────────────────────────────\n"
"    mov   rcx, [rel p_font_b]\n"
"    lea   rdx, [rel s_s_pza]\n"
"    mov   r8d, 12\n    mov   r9d, 296\n"
"    mov   dword [rsp+32], 0xFFC8B4B4\n    call  draw_text\n\n"

"    cmp   dword [rsp+56], 0\n"     /* pinza=0 → ABIERTA (verde)            */
"    jne   .dh_cerrada\n"
"    mov   rcx, [rel p_ren]\n"
"    mov   edx, 30\n    mov   r8d, 200\n    mov   r9d, 80\n"
"    mov   dword [rsp+32], 255\n    call  SDL_SetRenderDrawColor\n"
"    mov   dword [rsp+144], 12\n    mov   dword [rsp+148], 316\n"
"    mov   dword [rsp+152], 14\n    mov   dword [rsp+156], 14\n"
"    mov   rcx, [rel p_ren]\n    lea   rdx, [rsp+144]\n    call  SDL_RenderFillRect\n"
"    mov   rcx, [rel p_font]\n    lea   rdx, [rel s_pinza_a]\n"
"    mov   r8d, 34\n    mov   r9d, 316\n"
"    mov   dword [rsp+32], 0xFF50DC1E\n    call  draw_text\n"  /* R=30,G=220,B=80 */
"    jmp   .dh_pinza_done\n"
".dh_cerrada:\n"
"    mov   rcx, [rel p_ren]\n"
"    mov   edx, 220\n    mov   r8d, 50\n    mov   r9d, 50\n"
"    mov   dword [rsp+32], 255\n    call  SDL_SetRenderDrawColor\n"
"    mov   dword [rsp+144], 12\n    mov   dword [rsp+148], 316\n"
"    mov   dword [rsp+152], 14\n    mov   dword [rsp+156], 14\n"
"    mov   rcx, [rel p_ren]\n    lea   rdx, [rsp+144]\n    call  SDL_RenderFillRect\n"
"    mov   rcx, [rel p_font]\n    lea   rdx, [rel s_pinza_c]\n"
"    mov   r8d, 34\n    mov   r9d, 316\n"
"    mov   dword [rsp+32], 0xFF3232DC\n    call  draw_text\n"  /* R=220,G=50,B=50  */
".dh_pinza_done:\n\n"

"    mov   ecx, 342\n    call  draw_sep\n\n"

"    ; ── PROGRESO ─────────────────────────────────────────────\n"
"    mov   rcx, [rel p_font_b]\n"
"    lea   rdx, [rel s_s_prg]\n"
"    mov   r8d, 12\n    mov   r9d, 350\n"
"    mov   dword [rsp+32], 0xFFC8B4B4\n    call  draw_text\n\n"

"    lea   rcx, [rsp+64]\n    lea   rdx, [rel s_fmt_paso]\n"
"    mov   r8d, ebx\n"                     /* arg3 = idx                      */
"    lea   r10, [rel traza_len]\n"
"    mov   r9d, [r10]\n    call  sprintf\n"
"    mov   rcx, [rel p_font]\n    lea   rdx, [rsp+64]\n"
"    mov   r8d, 12\n    mov   r9d, 368\n"
"    mov   dword [rsp+32], 0xFFA08C8C\n    call  draw_text\n\n"

"    ; ── barra progreso ───────────────────────────────────────\n"
"    lea   r10, [rel traza_len]\n"
"    mov   ecx, [r10]\n"            /* total                                 */
"    cmp   ecx, 1\n"
"    jle   .dh_pb_full\n"
"    dec   ecx\n"                   /* total-1                               */
"    mov   eax, 176\n"
"    imul  eax, ebx\n"              /* 176*idx                               */
"    cdq\n"
"    idiv  ecx\n"                   /* eax = bar_w                           */
"    jmp   .dh_pb_w_ok\n"
".dh_pb_full:\n"
"    mov   eax, 176\n"
".dh_pb_w_ok:\n"
"    mov   [rsp+52], eax\n"          /* save bar_w (q2_deg slot, ya usado)    */
"    mov   rcx, [rel p_ren]\n"
"    mov   edx, 40\n    mov   r8d, 40\n    mov   r9d, 60\n"
"    mov   dword [rsp+32], 255\n    call  SDL_SetRenderDrawColor\n"
"    mov   dword [rsp+128], 12\n    mov   dword [rsp+132], 384\n"
"    mov   dword [rsp+136], 176\n    mov   dword [rsp+140], 8\n"
"    mov   rcx, [rel p_ren]\n    lea   rdx, [rsp+128]\n    call  SDL_RenderFillRect\n"
"    mov   rcx, [rel p_ren]\n"
"    mov   edx, 70\n    mov   r8d, 130\n    mov   r9d, 220\n"
"    mov   dword [rsp+32], 255\n    call  SDL_SetRenderDrawColor\n"
"    mov   dword [rsp+128], 12\n    mov   dword [rsp+132], 384\n"
"    mov   eax, [rsp+52]\n"         /* restore bar_w from stack              */
"    mov   [rsp+136], eax\n"
"    mov   dword [rsp+140], 8\n"
"    mov   rcx, [rel p_ren]\n    lea   rdx, [rsp+128]\n    call  SDL_RenderFillRect\n\n"

"    ; ── KF tick marks en barra HUD ───────────────────────────\n"
"    lea   r13, [rel traza_kf_len]\n"
"    mov   r13d, [r13]\n"
"    test  r13d, r13d\n"
"    jz    .dh_kf_done\n"
"    lea   r15, [rel traza_len]\n"
"    mov   r15d, [r15]\n"
"    cmp   r15d, 1\n"
"    jle   .dh_kf_done\n"
"    dec   r15d\n"
"    mov   rcx, [rel p_ren]\n"
"    mov   edx, 255\n    mov   r8d, 220\n    mov   r9d, 60\n"
"    mov   dword [rsp+32], 255\n    call  SDL_SetRenderDrawColor\n"
"    lea   r14, [rel traza_kf]\n"
"    xor   r12d, r12d\n"
".dh_kf_tick_loop:\n"
"    cmp   r12d, r13d\n"
"    jge   .dh_kf_done\n"
"    movsxd rax, dword [r14 + r12*4]\n"
"    imul  eax, 176\n"
"    cdq\n    idiv  r15d\n"
"    add   eax, 12\n"
"    mov   rcx, [rel p_ren]\n"
"    mov   edx, eax\n    mov   r8d, 381\n    mov   r9d, eax\n"
"    mov   dword [rsp+32], 394\n    call  SDL_RenderDrawLine\n"
"    inc   r12d\n    jmp   .dh_kf_tick_loop\n"
".dh_kf_done:\n\n"
"    ; ── nombre instruccion actual (busqueda dinamica) ────────\n"
"    lea   rax, [rel traza_kf_len]\n"
"    mov   r13d, [rax]\n"
"    test  r13d, r13d\n"
"    jz    .dh_kf_text_done\n"
"    lea   r14, [rel traza_kf]\n"
"    xor   r12d, r12d\n"
"    xor   r15d, r15d\n"
".dh_find_kf_loop:\n"
"    cmp   r15d, r13d\n"
"    jge   .dh_find_kf_done\n"
"    movsxd rax, dword [r14 + r15*4]\n"
"    cmp   rax, rbx\n"
"    jg    .dh_find_kf_done\n"
"    mov   r12d, r15d\n"
"    inc   r15d\n    jmp   .dh_find_kf_loop\n"
".dh_find_kf_done:\n"
"    lea   rax, [rel traza_kf_tipo]\n"
"    movsxd r14, dword [rax + r12*4]\n"
"    cmp   r14d, 0\n    jl    .dh_kf_unk_name\n"
"    cmp   r14d, 9\n    jg    .dh_kf_unk_name\n"
"    lea   rax, [rel s_kf_names]\n"
"    mov   r15, [rax + r14*8]\n"
"    jmp   .dh_kf_name_ok\n"
".dh_kf_unk_name:\n"
"    lea   r15, [rel s_kfn_unk]\n"
".dh_kf_name_ok:\n"
"    mov   rcx, [rel p_font]\n"
"    mov   rdx, r15\n"
"    mov   r8d, 12\n    mov   r9d, 420\n"
"    mov   dword [rsp+32], 0xFF3CDCFF\n    call  draw_text\n\n"
".dh_kf_text_done:\n\n"
"    mov   ecx, 438\n    call  draw_sep\n\n"

"    ; ── VISTA ────────────────────────────────────────────────\n"
"    mov   rcx, [rel p_font_b]\n"
"    lea   rdx, [rel s_s_vis]\n"
"    mov   r8d, 12\n    mov   r9d, 446\n"
"    mov   dword [rsp+32], 0xFFC8B4B4\n    call  draw_text\n\n"

"    ; botón ISO (x=12, índice=0) ─────────────────────────────\n"
"    lea   rax, [rel modo_vista]\n"
"    mov   eax, [rax]\n"
"    test  eax, eax\n"
"    je    .dh_v0_act\n"
"    mov   rcx, [rel p_ren]\n"
"    mov   edx, 40\n    mov   r8d, 40\n    mov   r9d, 60\n"
"    mov   dword [rsp+32], 255\n    call  SDL_SetRenderDrawColor\n"
"    mov   dword [rsp+144], 12\n    mov   dword [rsp+148], 464\n"
"    mov   dword [rsp+152], 50\n    mov   dword [rsp+156], 18\n"
"    mov   rcx, [rel p_ren]\n    lea   rdx, [rsp+144]\n    call  SDL_RenderFillRect\n"
"    mov   rcx, [rel p_font]\n    lea   rdx, [rel s_btn_iso]\n"
"    mov   r8d, 18\n    mov   r9d, 466\n"
"    mov   dword [rsp+32], 0xFF826464\n    call  draw_text\n"  /* gris inactivo */
"    jmp   .dh_v0_done\n"
".dh_v0_act:\n"
"    mov   rcx, [rel p_ren]\n"
"    mov   edx, 70\n    mov   r8d, 130\n    mov   r9d, 220\n"
"    mov   dword [rsp+32], 255\n    call  SDL_SetRenderDrawColor\n"
"    mov   dword [rsp+144], 12\n    mov   dword [rsp+148], 464\n"
"    mov   dword [rsp+152], 50\n    mov   dword [rsp+156], 18\n"
"    mov   rcx, [rel p_ren]\n    lea   rdx, [rsp+144]\n    call  SDL_RenderFillRect\n"
"    mov   rcx, [rel p_font]\n    lea   rdx, [rel s_btn_iso]\n"
"    mov   r8d, 18\n    mov   r9d, 466\n"
"    mov   dword [rsp+32], 0xFFFFFFFF\n    call  draw_text\n"
".dh_v0_done:\n\n"

"    ; botón TOP (x=68, índice=1) ─────────────────────────────\n"
"    lea   rax, [rel modo_vista]\n"
"    mov   eax, [rax]\n"
"    cmp   eax, 1\n"
"    je    .dh_v1_act\n"
"    mov   rcx, [rel p_ren]\n"
"    mov   edx, 40\n    mov   r8d, 40\n    mov   r9d, 60\n"
"    mov   dword [rsp+32], 255\n    call  SDL_SetRenderDrawColor\n"
"    mov   dword [rsp+144], 68\n    mov   dword [rsp+148], 464\n"
"    mov   dword [rsp+152], 50\n    mov   dword [rsp+156], 18\n"
"    mov   rcx, [rel p_ren]\n    lea   rdx, [rsp+144]\n    call  SDL_RenderFillRect\n"
"    mov   rcx, [rel p_font]\n    lea   rdx, [rel s_btn_top]\n"
"    mov   r8d, 74\n    mov   r9d, 466\n"
"    mov   dword [rsp+32], 0xFF826464\n    call  draw_text\n"
"    jmp   .dh_v1_done\n"
".dh_v1_act:\n"
"    mov   rcx, [rel p_ren]\n"
"    mov   edx, 70\n    mov   r8d, 130\n    mov   r9d, 220\n"
"    mov   dword [rsp+32], 255\n    call  SDL_SetRenderDrawColor\n"
"    mov   dword [rsp+144], 68\n    mov   dword [rsp+148], 464\n"
"    mov   dword [rsp+152], 50\n    mov   dword [rsp+156], 18\n"
"    mov   rcx, [rel p_ren]\n    lea   rdx, [rsp+144]\n    call  SDL_RenderFillRect\n"
"    mov   rcx, [rel p_font]\n    lea   rdx, [rel s_btn_top]\n"
"    mov   r8d, 74\n    mov   r9d, 466\n"
"    mov   dword [rsp+32], 0xFFFFFFFF\n    call  draw_text\n"
".dh_v1_done:\n\n"

"    ; botón LADO (x=124, índice=2) ───────────────────────────\n"
"    lea   rax, [rel modo_vista]\n"
"    mov   eax, [rax]\n"
"    cmp   eax, 2\n"
"    je    .dh_v2_act\n"
"    mov   rcx, [rel p_ren]\n"
"    mov   edx, 40\n    mov   r8d, 40\n    mov   r9d, 60\n"
"    mov   dword [rsp+32], 255\n    call  SDL_SetRenderDrawColor\n"
"    mov   dword [rsp+144], 124\n    mov   dword [rsp+148], 464\n"
"    mov   dword [rsp+152], 50\n    mov   dword [rsp+156], 18\n"
"    mov   rcx, [rel p_ren]\n    lea   rdx, [rsp+144]\n    call  SDL_RenderFillRect\n"
"    mov   rcx, [rel p_font]\n    lea   rdx, [rel s_btn_lado]\n"
"    mov   r8d, 130\n    mov   r9d, 466\n"
"    mov   dword [rsp+32], 0xFF826464\n    call  draw_text\n"
"    jmp   .dh_v2_done\n"
".dh_v2_act:\n"
"    mov   rcx, [rel p_ren]\n"
"    mov   edx, 70\n    mov   r8d, 130\n    mov   r9d, 220\n"
"    mov   dword [rsp+32], 255\n    call  SDL_SetRenderDrawColor\n"
"    mov   dword [rsp+144], 124\n    mov   dword [rsp+148], 464\n"
"    mov   dword [rsp+152], 50\n    mov   dword [rsp+156], 18\n"
"    mov   rcx, [rel p_ren]\n    lea   rdx, [rsp+144]\n    call  SDL_RenderFillRect\n"
"    mov   rcx, [rel p_font]\n    lea   rdx, [rel s_btn_lado]\n"
"    mov   r8d, 130\n    mov   r9d, 466\n"
"    mov   dword [rsp+32], 0xFFFFFFFF\n    call  draw_text\n"
".dh_v2_done:\n\n"

"    ; ── nombre de vista actual ───────────────────────────────\n"
"    lea   rax, [rel modo_vista]\n"
"    mov   eax, [rax]\n"
"    cmp   eax, 1\n    je    .dh_vn_top\n"
"    cmp   eax, 2\n    je    .dh_vn_lado\n"
"    lea   rdx, [rel s_v_iso]\n    jmp   .dh_vn_draw\n"
".dh_vn_top:  lea   rdx, [rel s_v_top]\n    jmp   .dh_vn_draw\n"
".dh_vn_lado: lea   rdx, [rel s_v_lado]\n"
".dh_vn_draw:\n"
"    mov   rcx, [rel p_font]\n"
"    mov   r8d, 12\n    mov   r9d, 488\n"
"    mov   dword [rsp+32], 0xFFFFC88C\n    call  draw_text\n\n"  /* R=140,G=200,B=255 */

"    ; ── hints ────────────────────────────────────────────────\n"
"    mov   rcx, [rel p_font]\n"
"    lea   rdx, [rel s_hint1]\n"
"    mov   r8d, 12\n    mov   r9d, 532\n"
"    mov   dword [rsp+32], 0xFF645050\n    call  draw_text\n\n"  /* gris oscuro */

"    mov   rcx, [rel p_font]\n"
"    lea   rdx, [rel s_hint2]\n"
"    mov   r8d, 12\n    mov   r9d, 552\n"
"    mov   dword [rsp+32], 0xFF645050\n    call  draw_text\n\n"

".dh_ret:\n"
"    add   rsp, 168\n"
"    pop   r15\n"
"    pop   r14\n"
"    pop   r13\n"
"    pop   r12\n"
"    pop   rbx\n"
"    pop   rbp\n"
"    ret\n\n"
    , f);

    /* ── Paso 8a: draw_frame — proyecta brazo y llama al HUD ──────── */
    fputs(
"; ================================================================\n"
"; draw_frame(idx) — frame completo con grid, brazo, HUD, barra\n"
"; rcx = idx\n"
"; Frame: push rbp/rbx/r12/r13/r14/r15  sub rsp,120  (176 bytes)\n"
";   rbx=idx  r12d=wx  r13d=wy  r14d=wz\n"
";   [rsp+48]=q1_deg  [rsp+52]=q2_deg\n"
";   [rsp+56]=base_sx [rsp+60]=base_sy\n"
";   [rsp+64]=elbow_sx [rsp+68]=elbow_sy\n"
";   [rsp+72]=tool_sx  [rsp+76]=tool_sy\n"
";   [rsp+84]=ex  [rsp+88]=ey  [rsp+92]=pinza\n"
";   [rsp+96..111]=grid_temps  (reusados para bar_rect al final)\n"
"; ================================================================\n"
"draw_frame:\n"
"    push  rbp\n"
"    mov   rbp, rsp\n"
"    push  rbx\n"
"    push  r12\n"
"    push  r13\n"
"    push  r14\n"
"    push  r15\n"
"    sub   rsp, 120\n\n"

"    mov   ebx, ecx\n\n"

"    ; ================================================================\n"
"    ; 0. Grid de fondo (30,30,50,255)\n"
"    ; ================================================================\n"
"    mov   rcx, [rel p_ren]\n"
"    mov   edx, 30\n    mov   r8d, 30\n    mov   r9d, 50\n"
"    mov   dword [rsp+32], 255\n"
"    call  SDL_SetRenderDrawColor\n\n"

"    ; ¿vista LADO?\n"
"    lea   rax, [rel modo_vista]\n"
"    mov   eax, [rax]\n"
"    cmp   eax, 2\n"
"    je    .df_grid_lado\n\n"

"    ; ── grid ISO/TOP: g de -350 a 350 paso 50 ────────────────\n"
"    mov   r15d, -350\n"
".df_grid_loop:\n"
"    cmp   r15d, 350\n"
"    jg    .df_grid_done\n"

"    ; línea en dirección X: proj(g,-350,0) → proj(g,350,0)\n"
"    mov   ecx, r15d\n    mov   edx, -350\n    xor   r8d, r8d\n"
"    lea   r9,  [rsp+96]\n    lea   rax, [rsp+100]\n    mov   [rsp+32], rax\n"
"    call  proj\n"
"    mov   ecx, r15d\n    mov   edx, 350\n    xor   r8d, r8d\n"
"    lea   r9,  [rsp+104]\n    lea   rax, [rsp+108]\n    mov   [rsp+32], rax\n"
"    call  proj\n"
"    mov   rcx, [rel p_ren]\n"
"    mov   edx, [rsp+96]\n    mov   r8d, [rsp+100]\n"
"    mov   r9d, [rsp+104]\n    mov   eax, [rsp+108]\n    mov   [rsp+32], eax\n"
"    call  SDL_RenderDrawLine\n"

"    ; línea en dirección Y: proj(-350,g,0) → proj(350,g,0)\n"
"    mov   ecx, -350\n    mov   edx, r15d\n    xor   r8d, r8d\n"
"    lea   r9,  [rsp+96]\n    lea   rax, [rsp+100]\n    mov   [rsp+32], rax\n"
"    call  proj\n"
"    mov   ecx, 350\n    mov   edx, r15d\n    xor   r8d, r8d\n"
"    lea   r9,  [rsp+104]\n    lea   rax, [rsp+108]\n    mov   [rsp+32], rax\n"
"    call  proj\n"
"    mov   rcx, [rel p_ren]\n"
"    mov   edx, [rsp+96]\n    mov   r8d, [rsp+100]\n"
"    mov   r9d, [rsp+104]\n    mov   eax, [rsp+108]\n    mov   [rsp+32], eax\n"
"    call  SDL_RenderDrawLine\n"

"    add   r15d, 50\n"
"    jmp   .df_grid_loop\n"
"    jmp   .df_grid_done\n\n"

"    ; ── grid LADO: r y z de 0 a 350 paso 50 ─────────────────\n"
".df_grid_lado:\n"
"    mov   r15d, 0\n"
".df_grid_lado_loop:\n"
"    cmp   r15d, 350\n"
"    jg    .df_grid_done\n"

"    ; columna a radio r=g: proj(g,0,0) → proj(g,0,350)\n"
"    mov   ecx, r15d\n    xor   edx, edx\n    xor   r8d, r8d\n"
"    lea   r9,  [rsp+96]\n    lea   rax, [rsp+100]\n    mov   [rsp+32], rax\n"
"    call  proj\n"
"    mov   ecx, r15d\n    xor   edx, edx\n    mov   r8d, 350\n"
"    lea   r9,  [rsp+104]\n    lea   rax, [rsp+108]\n    mov   [rsp+32], rax\n"
"    call  proj\n"
"    mov   rcx, [rel p_ren]\n"
"    mov   edx, [rsp+96]\n    mov   r8d, [rsp+100]\n"
"    mov   r9d, [rsp+104]\n    mov   eax, [rsp+108]\n    mov   [rsp+32], eax\n"
"    call  SDL_RenderDrawLine\n"

"    ; fila a altura z=g: proj(0,0,g) → proj(350,0,g)\n"
"    xor   ecx, ecx\n    xor   edx, edx\n    mov   r8d, r15d\n"
"    lea   r9,  [rsp+96]\n    lea   rax, [rsp+100]\n    mov   [rsp+32], rax\n"
"    call  proj\n"
"    mov   ecx, 350\n    xor   edx, edx\n    mov   r8d, r15d\n"
"    lea   r9,  [rsp+104]\n    lea   rax, [rsp+108]\n    mov   [rsp+32], rax\n"
"    call  proj\n"
"    mov   rcx, [rel p_ren]\n"
"    mov   edx, [rsp+96]\n    mov   r8d, [rsp+100]\n"
"    mov   r9d, [rsp+104]\n    mov   eax, [rsp+108]\n    mov   [rsp+32], eax\n"
"    call  SDL_RenderDrawLine\n"

"    add   r15d, 50\n"
"    jmp   .df_grid_lado_loop\n\n"

".df_grid_done:\n\n"

"    ; ================================================================\n"
"    ; 1. Cargar datos de traza[idx]\n"
"    ; ================================================================\n"
"    lea   r10, [rel traza_x]\n"
"    mov   r12d, [r10 + rbx*4]\n"
"    lea   r10, [rel traza_y]\n"
"    mov   r13d, [r10 + rbx*4]\n"
"    lea   r10, [rel traza_z]\n"
"    mov   r14d, [r10 + rbx*4]\n"
"    lea   r10, [rel traza_pinza]\n"
"    mov   eax,  [r10 + rbx*4]\n"
"    mov   [rsp+92], eax\n\n"

"    ; ── IK ───────────────────────────────────────────────────\n"
"    mov   ecx, r12d\n"
"    mov   edx, r13d\n"
"    xor   r8d, r8d\n"
"    lea   r9,  [rsp+48]\n"
"    lea   rax, [rsp+52]\n"
"    mov   [rsp+32], rax\n"
"    call  ik_codo\n"
"    test  eax, eax\n"
"    jnz   .dframe_ik_ok\n"
"    mov   dword [rsp+48], 0\n"
"    mov   dword [rsp+52], 0\n"
".dframe_ik_ok:\n\n"

"    ; ── FK: ex=L1*cos(q1)  ey=L1*sin(q1) ────────────────────\n"
"    cvtsi2sd  xmm0, dword [rsp+48]\n"
"    mulsd     xmm0, [rel f_deg2rad]\n"
"    call      cos\n"
"    mulsd     xmm0, [rel f_L1]\n"
"    cvttsd2si eax,  xmm0\n"
"    mov   [rsp+84], eax\n"

"    cvtsi2sd  xmm0, dword [rsp+48]\n"
"    mulsd     xmm0, [rel f_deg2rad]\n"
"    call      sin\n"
"    mulsd     xmm0, [rel f_L1]\n"
"    cvttsd2si eax,  xmm0\n"
"    mov   [rsp+88], eax\n\n"

"    ; ── proyecciones ─────────────────────────────────────────\n"
"    xor   ecx, ecx\n    xor   edx, edx\n    xor   r8d, r8d\n"
"    lea   r9,  [rsp+56]\n    lea   rax, [rsp+60]\n    mov   [rsp+32], rax\n"
"    call  proj\n"

"    mov   ecx, [rsp+84]\n    mov   edx, [rsp+88]\n    mov   r8d, r14d\n"
"    lea   r9,  [rsp+64]\n    lea   rax, [rsp+68]\n    mov   [rsp+32], rax\n"
"    call  proj\n"

"    mov   ecx, r12d\n    mov   edx, r13d\n    mov   r8d, r14d\n"
"    lea   r9,  [rsp+72]\n    lea   rax, [rsp+76]\n    mov   [rsp+32], rax\n"
"    call  proj\n\n"

"    ; ================================================================\n"
"    ; 2. Eslabón 1: base→codo  (70,130,220) grosor 3px\n"
"    ; ================================================================\n"
"    mov   rcx, [rel p_ren]\n"
"    mov   edx, 70\n    mov   r8d, 130\n    mov   r9d, 220\n"
"    mov   dword [rsp+32], 255\n"
"    call  SDL_SetRenderDrawColor\n"

"    ; t=-1 (y offset)\n"
"    mov   rcx, [rel p_ren]\n"
"    mov   edx, [rsp+56]\n    mov   r8d, [rsp+60]\n    dec   r8d\n"
"    mov   r9d, [rsp+64]\n    mov   eax, [rsp+68]\n    dec   eax\n    mov   [rsp+32], eax\n"
"    call  SDL_RenderDrawLine\n"
"    ; t=0\n"
"    mov   rcx, [rel p_ren]\n"
"    mov   edx, [rsp+56]\n    mov   r8d, [rsp+60]\n"
"    mov   r9d, [rsp+64]\n    mov   eax, [rsp+68]\n    mov   [rsp+32], eax\n"
"    call  SDL_RenderDrawLine\n"
"    ; t=+1\n"
"    mov   rcx, [rel p_ren]\n"
"    mov   edx, [rsp+56]\n    mov   r8d, [rsp+60]\n    inc   r8d\n"
"    mov   r9d, [rsp+64]\n    mov   eax, [rsp+68]\n    inc   eax\n    mov   [rsp+32], eax\n"
"    call  SDL_RenderDrawLine\n\n"

"    ; ================================================================\n"
"    ; 3. Eslabón 2: codo→herramienta  (180,100,40) grosor 3px\n"
"    ; ================================================================\n"
"    mov   rcx, [rel p_ren]\n"
"    mov   edx, 180\n    mov   r8d, 100\n    mov   r9d, 40\n"
"    mov   dword [rsp+32], 255\n"
"    call  SDL_SetRenderDrawColor\n"

"    mov   rcx, [rel p_ren]\n"
"    mov   edx, [rsp+64]\n    mov   r8d, [rsp+68]\n    dec   r8d\n"
"    mov   r9d, [rsp+72]\n    mov   eax, [rsp+76]\n    dec   eax\n    mov   [rsp+32], eax\n"
"    call  SDL_RenderDrawLine\n"
"    mov   rcx, [rel p_ren]\n"
"    mov   edx, [rsp+64]\n    mov   r8d, [rsp+68]\n"
"    mov   r9d, [rsp+72]\n    mov   eax, [rsp+76]\n    mov   [rsp+32], eax\n"
"    call  SDL_RenderDrawLine\n"
"    mov   rcx, [rel p_ren]\n"
"    mov   edx, [rsp+64]\n    mov   r8d, [rsp+68]\n    inc   r8d\n"
"    mov   r9d, [rsp+72]\n    mov   eax, [rsp+76]\n    inc   eax\n    mov   [rsp+32], eax\n"
"    call  SDL_RenderDrawLine\n\n"

"    ; ================================================================\n"
"    ; 4. Círculos: base=blanco, codo=verde, end=amarillo/rojo\n"
"    ; ================================================================\n"
"    ; base (200,200,200) r=8\n"
"    mov   rcx, [rel p_ren]\n"
"    mov   edx, 200\n    mov   r8d, 200\n    mov   r9d, 200\n"
"    mov   dword [rsp+32], 255\n    call  SDL_SetRenderDrawColor\n"
"    mov   ecx, [rsp+56]\n    mov   edx, [rsp+60]\n    mov   r8d, 8\n"
"    call  draw_circle\n\n"

"    ; codo (100,200,100) r=6\n"
"    mov   rcx, [rel p_ren]\n"
"    mov   edx, 100\n    mov   r8d, 200\n    mov   r9d, 100\n"
"    mov   dword [rsp+32], 255\n    call  SDL_SetRenderDrawColor\n"
"    mov   ecx, [rsp+64]\n    mov   edx, [rsp+68]\n    mov   r8d, 6\n"
"    call  draw_circle\n\n"

"    ; end-effector: amarillo=abierta(255,220,50) / rojo=cerrada(255,60,60)\n"
"    cmp   dword [rsp+92], 0\n"
"    jne   .dframe_cerrada\n"
"    mov   rcx, [rel p_ren]\n"
"    mov   edx, 255\n    mov   r8d, 220\n    mov   r9d, 50\n"
"    mov   dword [rsp+32], 255\n    call  SDL_SetRenderDrawColor\n"
"    jmp   .dframe_draw_end\n"
".dframe_cerrada:\n"
"    mov   rcx, [rel p_ren]\n"
"    mov   edx, 255\n    mov   r8d, 60\n    mov   r9d, 60\n"
"    mov   dword [rsp+32], 255\n    call  SDL_SetRenderDrawColor\n"
".dframe_draw_end:\n"
"    mov   ecx, [rsp+72]\n    mov   edx, [rsp+76]\n    mov   r8d, 9\n"
"    call  draw_circle\n\n"

"    ; ================================================================\n"
"    ; 5. HUD (panel izquierdo)\n"
"    ; ================================================================\n"
"    mov   ecx, ebx\n"
"    call  draw_hud\n\n"

"    ; ================================================================\n"
"    ; 6. Etiqueta de vista (top-right, x=710, y=12)\n"
"    ; ================================================================\n"
"    lea   rax, [rel modo_vista]\n"
"    mov   eax, [rax]\n"
"    cmp   eax, 1\n    je    .df_lbl_top\n"
"    cmp   eax, 2\n    je    .df_lbl_lado\n"
"    lea   rdx, [rel s_vista_iso]\n    jmp   .df_lbl_draw\n"
".df_lbl_top:  lea   rdx, [rel s_vista_top]\n    jmp   .df_lbl_draw\n"
".df_lbl_lado: lea   rdx, [rel s_vista_lad]\n"
".df_lbl_draw:\n"
"    mov   rcx, [rel p_font_b]\n"
"    mov   r8d, 710\n    mov   r9d, 12\n"
"    mov   dword [rsp+32], 0xC8FFB464\n"  /* R=100,G=180,B=255,A=200          */
"    call  draw_text\n\n"

"    ; ================================================================\n"
"    ; 7. Barra de progreso inferior (y=594, h=6)\n"
"    ; ================================================================\n"
"    lea   r10, [rel traza_len]\n"
"    mov   r15d, [r10]\n"            /* r15d = total (callee-save, survives calls) */
"    cmp   r15d, 1\n"
"    jle   .df_pb_full\n"
"    mov   eax, r15d\n"
"    dec   eax\n"                    /* total-1 */
"    mov   ecx, eax\n"
"    mov   eax, 900\n"
"    imul  eax, ebx\n"              /* 900*idx                               */
"    cdq\n"
"    idiv  ecx\n"                    /* bar_w                                 */
"    jmp   .df_pb_w_ok\n"
".df_pb_full:\n"
"    mov   eax, 900\n"
".df_pb_w_ok:\n"
"    mov   [rsp+92], eax\n"          /* save bar_w on stack (pinza slot reused) */
"    ; fondo (30,30,50)\n"
"    mov   rcx, [rel p_ren]\n"
"    mov   edx, 30\n    mov   r8d, 30\n    mov   r9d, 50\n"
"    mov   dword [rsp+32], 255\n    call  SDL_SetRenderDrawColor\n"
"    mov   dword [rsp+96],  0\n"
"    mov   dword [rsp+100], 594\n"
"    mov   dword [rsp+104], 900\n"
"    mov   dword [rsp+108], 6\n"
"    mov   rcx, [rel p_ren]\n    lea   rdx, [rsp+96]\n    call  SDL_RenderFillRect\n"
"    ; fg (70,130,220)\n"
"    mov   rcx, [rel p_ren]\n"
"    mov   edx, 70\n    mov   r8d, 130\n    mov   r9d, 220\n"
"    mov   dword [rsp+32], 255\n    call  SDL_SetRenderDrawColor\n"
"    mov   dword [rsp+96],  0\n"
"    mov   dword [rsp+100], 594\n"
"    mov   eax, [rsp+92]\n    mov   [rsp+104], eax\n"
"    mov   dword [rsp+108], 6\n"
"    mov   rcx, [rel p_ren]\n    lea   rdx, [rsp+96]\n    call  SDL_RenderFillRect\n\n"
"    ; lineas de keyframe (amarillo 255,220,60)\n"
"    lea   r14, [rel traza_kf_len]\n"
"    mov   r14d, [r14]\n"            /* kf_len */
"    test  r14d, r14d\n"
"    jz    .df_kf_done\n"
"    mov   rcx, [rel p_ren]\n"
"    mov   edx, 255\n    mov   r8d, 220\n    mov   r9d, 60\n"
"    mov   dword [rsp+32], 255\n    call  SDL_SetRenderDrawColor\n"
"    cmp   r15d, 1\n"               /* total-1 divisor */
"    jle   .df_kf_done\n"
"    mov   r13d, r15d\n    dec   r13d\n"  /* r13d = total-1 */
"    lea   r15, [rel traza_kf]\n"   /* r15 = ptr to kf_idx[] */
"    xor   r12d, r12d\n"            /* r12d = k */
".df_kf_loop:\n"
"    cmp   r12d, r14d\n"
"    jge   .df_kf_done\n"
"    movsxd rax, dword [r15 + r12*4]\n"  /* kf_idx[k] */
"    imul  eax, 900\n"
"    cdq\n"
"    idiv  r13d\n"                   /* kx = 900*kf_idx[k]/(total-1) */
"    mov   rcx, [rel p_ren]\n"
"    mov   edx, eax\n    mov   r8d, 591\n    mov   r9d, eax\n"
"    mov   dword [rsp+32], 602\n    call  SDL_RenderDrawLine\n"
"    inc   r12d\n"
"    jmp   .df_kf_loop\n"
".df_kf_done:\n\n"

"    add   rsp, 120\n"
"    pop   r15\n"
"    pop   r14\n"
"    pop   r13\n"
"    pop   r12\n"
"    pop   rbx\n"
"    pop   rbp\n"
"    ret\n\n"
    , f);

    /* ── main — Paso 1: init SDL2 + ventana vacía + cleanup ───── */
    fputs(
"; ================================================================\n"
"; main  (Paso 1 — esqueleto: abre ventana, muestra 1 frame, cierra)\n"
"; Stack frame: sub rsp,48  => 32 shadow + 16 para args 5/6\n"
"; Convencion Windows x64: rcx rdx r8 r9  [rsp+32] [rsp+40] ...\n"
"; ================================================================\n"
"main:\n"
"    push  rbp\n"
"    mov   rbp, rsp\n"
"    sub   rsp, 48\n\n"

"    ; SDL_SetMainReady()\n"
"    call  SDL_SetMainReady\n\n"

"    ; SDL_Init(SDL_INIT_VIDEO = 0x20)\n"
"    mov   ecx, 0x20\n"
"    call  SDL_Init\n"
"    test  eax, eax\n"
"    jz    .sdl_ok\n"
"    mov   eax, 1\n"
"    leave\n"
"    ret\n"
".sdl_ok:\n\n"

"    ; TTF_Init()\n"
"    call  TTF_Init\n"
"    test  eax, eax\n"
"    jz    .ttf_ok\n"
"    call  SDL_Quit\n"
"    mov   eax, 1\n"
"    leave\n"
"    ret\n"
".ttf_ok:\n\n"

"    ; p_font = TTF_OpenFont(\"arial.ttf\", 16)\n"
"    lea   rcx, [rel s_font_path]\n"
"    mov   edx, 16\n"
"    call  TTF_OpenFont\n"
"    mov   [rel p_font], rax\n"
"    test  rax, rax\n"
"    jnz   .font_ok\n"
"    call  TTF_Quit\n"
"    call  SDL_Quit\n"
"    mov   eax, 1\n"
"    leave\n"
"    ret\n"
".font_ok:\n\n"

"    ; p_font_b = TTF_OpenFont(\"arial.ttf\", 16)\n"
"    lea   rcx, [rel s_font_path]\n"
"    mov   edx, 16\n"
"    call  TTF_OpenFont\n"
"    mov   [rel p_font_b], rax\n\n"

"    ; TTF_SetFontStyle(p_font_b, TTF_STYLE_BOLD=1)\n"
"    mov   rcx, [rel p_font_b]\n"
"    mov   edx, 1\n"
"    call  TTF_SetFontStyle\n\n"

"    ; SDL_CreateWindow(title, CENTERED, CENTERED, 900, 600, SHOWN)\n"
"    lea   rcx, [rel s_win_title]\n"
"    mov   edx, 0x2FFF0000          ; SDL_WINDOWPOS_CENTERED\n"
"    mov   r8d, 0x2FFF0000\n"
"    mov   r9d, 900                 ; WIN_W\n"
"    mov   dword [rsp+32], 600      ; WIN_H  (5to arg)\n"
"    mov   dword [rsp+40], 4        ; SDL_WINDOW_SHOWN (6to arg)\n"
"    call  SDL_CreateWindow\n"
"    mov   [rel p_win], rax\n\n"

"    ; SDL_CreateRenderer(win, -1, ACCELERATED|PRESENTVSYNC = 6)\n"
"    mov   rcx, [rel p_win]\n"
"    mov   edx, -1\n"
"    mov   r8d, 6\n"
"    call  SDL_CreateRenderer\n"
"    mov   [rel p_ren], rax\n\n"

"    ; SDL_SetRenderDrawColor(ren, 15, 15, 25, 255)  — fondo oscuro\n"
"    mov   rcx, [rel p_ren]\n"
"    mov   edx, 15\n"
"    mov   r8d, 15\n"
"    mov   r9d, 25\n"
"    mov   dword [rsp+32], 255      ; alpha (5to arg)\n"
"    call  SDL_SetRenderDrawColor\n\n"

"    ; SDL_RenderClear(ren)\n"
"    mov   rcx, [rel p_ren]\n"
"    call  SDL_RenderClear\n\n"

"    ; SDL_RenderPresent(ren)\n"
"    mov   rcx, [rel p_ren]\n"
"    call  SDL_RenderPresent\n\n"

"    ; ================================================================\n"
"    ; Paso 8b — render / event loop principal\n"
"    ; ================================================================\n"
".ml_loop:\n"
"    ; ── vaciar cola de eventos ───────────────────────────────\n"
".ml_poll:\n"
"    lea   rcx, [rel ev_buf]\n"
"    call  SDL_PollEvent\n"
"    test  eax, eax\n"
"    jz    .ml_no_event\n"

"    lea   rax, [rel ev_buf]\n"
"    mov   eax, [rax]\n"              /* SDL_Event.type  (offset 0)          */
"    cmp   eax, 0x100\n"             /* SDL_QUIT                            */
"    je    .ml_quit\n"
"    cmp   eax, 0x300\n"             /* SDL_KEYDOWN                         */
"    jne   .ml_poll\n"

"    ; keysym.sym esta en SDL_KeyboardEvent a offset 20\n"
"    lea   rax, [rel ev_buf]\n"
"    mov   eax, [rax + 20]\n"
"    cmp   eax, 27\n"               /* SDLK_ESCAPE                          */
"    je    .ml_quit\n"
"    cmp   eax, 32\n"              /* SPACE → toggle pausa                  */
"    jne   .ml_not_space\n"
"    lea   rax, [rel paused]\n"
"    mov   ecx, [rax]\n"
"    xor   ecx, 1\n"
"    mov   [rax], ecx\n"
"    jmp   .ml_poll\n"
".ml_not_space:\n"
"    cmp   eax, 0x4000004F\n"      /* SDLK_RIGHT → siguiente KF            */
"    je    .ml_kf_right\n"
"    cmp   eax, 0x40000050\n"      /* SDLK_LEFT → anterior KF              */
"    je    .ml_kf_left\n"
"    jmp   .ml_not_lr\n"
/* ── RIGHT: busca KF actual desde cur_idx, salta al siguiente ── */
".ml_kf_right:\n"
"    lea   r10, [rel traza_kf_len]\n"
"    mov   r11d, [r10]\n"
"    test  r11d, r11d\n    jz    .ml_poll\n"
"    lea   r10, [rel cur_idx]\n    mov   edx, [r10]\n"
"    lea   r10, [rel traza_kf]\n"
"    xor   ecx, ecx\n    xor   eax, eax\n"
".ml_fkf_r:\n"
"    cmp   eax, r11d\n    jge   .ml_fkf_r_done\n"
"    cmp   dword [r10 + rax*4], edx\n    jg    .ml_fkf_r_done\n"
"    mov   ecx, eax\n    inc   eax\n    jmp   .ml_fkf_r\n"
".ml_fkf_r_done:\n"
"    inc   ecx\n"
"    cmp   ecx, r11d\n    jl    .ml_right_ok\n"
"    mov   ecx, r11d\n    dec   ecx\n"
".ml_right_ok:\n"
"    lea   rax, [rel kf_cur]\n    mov   [rax], ecx\n"
"    movsxd rdx, dword [r10 + rcx*4]\n"
"    lea   rax, [rel cur_idx]\n    mov   [rax], edx\n"
"    lea   rax, [rel paused]\n    mov   dword [rax], 1\n"
"    jmp   .ml_poll\n"
/* ── LEFT: busca KF actual desde cur_idx, salta al anterior ── */
".ml_kf_left:\n"
"    lea   r10, [rel traza_kf_len]\n"
"    mov   r11d, [r10]\n"
"    test  r11d, r11d\n    jz    .ml_poll\n"
"    lea   r10, [rel cur_idx]\n    mov   edx, [r10]\n"
"    lea   r10, [rel traza_kf]\n"
"    xor   ecx, ecx\n    xor   eax, eax\n"
".ml_fkf_l:\n"
"    cmp   eax, r11d\n    jge   .ml_fkf_l_done\n"
"    cmp   dword [r10 + rax*4], edx\n    jg    .ml_fkf_l_done\n"
"    mov   ecx, eax\n    inc   eax\n    jmp   .ml_fkf_l\n"
".ml_fkf_l_done:\n"
"    test  ecx, ecx\n    jz    .ml_poll\n"
"    dec   ecx\n"
"    lea   rax, [rel kf_cur]\n    mov   [rax], ecx\n"
"    movsxd rdx, dword [r10 + rcx*4]\n"
"    lea   rax, [rel cur_idx]\n    mov   [rax], edx\n"
"    lea   rax, [rel paused]\n    mov   dword [rax], 1\n"
"    jmp   .ml_poll\n"
".ml_not_lr:\n"
"    cmp   eax, 118\n"             /* 'v' → rotar vista ISO/TOP/LATERAL    */
"    jne   .ml_poll\n"
"    lea   rax, [rel modo_vista]\n"
"    mov   ecx, [rax]\n"
"    inc   ecx\n"
"    cmp   ecx, 3\n"
"    jl    .ml_mv_ok\n"
"    xor   ecx, ecx\n"
".ml_mv_ok:\n"
"    mov   [rax], ecx\n"
"    jmp   .ml_poll\n\n"

".ml_no_event:\n"
"    ; ── limpiar pantalla (fondo oscuro) ─────────────────────\n"
"    mov   rcx, [rel p_ren]\n"
"    mov   edx, 15\n"
"    mov   r8d, 15\n"
"    mov   r9d, 25\n"
"    mov   dword [rsp+32], 255\n"
"    call  SDL_SetRenderDrawColor\n"
"    mov   rcx, [rel p_ren]\n"
"    call  SDL_RenderClear\n\n"

"    ; ── dibujar frame actual ─────────────────────────────────\n"
"    lea   rax, [rel cur_idx]\n"
"    mov   ecx, [rax]\n"
"    call  draw_frame\n\n"

"    ; ── presentar ────────────────────────────────────────────\n"
"    mov   rcx, [rel p_ren]\n"
"    call  SDL_RenderPresent\n\n"

"    ; ── pausa: si paused!=0, no avanzar índice ─────────────\n"
"    lea   rax, [rel paused]\n"
"    cmp   dword [rax], 0\n"
"    jne   .ml_paused\n\n"

"    ; ── avanzar indice (ciclico) ──────────────────────────────\n"
"    lea   rax, [rel cur_idx]\n"
"    mov   ecx, [rax]\n"
"    inc   ecx\n"
"    lea   r10, [rel traza_len]\n"
"    cmp   ecx, [r10]\n"
"    jl    .ml_idx_ok\n"
"    xor   ecx, ecx\n"
".ml_idx_ok:\n"
"    mov   [rax], ecx\n"

"    ; ── delay basado en velocidad del paso actual ─────────────\n"
"    lea   r10, [rel traza_vel]\n"
"    mov   ecx, [rax]\n"           /* cur_idx (just updated)                */
"    mov   ecx, [r10 + rcx*4]\n"  /* vel[idx]                              */
"    ; delay = 220 - vel*180/100  (min 35ms)\n"
"    mov   eax, ecx\n"
"    imul  eax, 180\n"
"    cdq\n    mov   ecx, 100\n    idiv  ecx\n"
"    mov   ecx, 220\n"
"    sub   ecx, eax\n"
"    cmp   ecx, 35\n"
"    jge   .ml_delay_ok\n"
"    mov   ecx, 35\n"
".ml_delay_ok:\n"
"    call  SDL_Delay\n"
"    jmp   .ml_loop\n\n"

".ml_paused:\n"
"    mov   ecx, 16\n"             /* 60fps while paused                    */
"    call  SDL_Delay\n"
"    jmp   .ml_loop\n\n"

".ml_quit:\n"
"; ── cleanup ──────────────────────────────────────────────────────\n"
"    mov   rcx, [rel p_ren]\n"
"    call  SDL_DestroyRenderer\n\n"

"    mov   rcx, [rel p_win]\n"
"    call  SDL_DestroyWindow\n\n"

"    mov   rcx, [rel p_font]\n"
"    call  TTF_CloseFont\n\n"

"    mov   rcx, [rel p_font_b]\n"
"    call  TTF_CloseFont\n\n"

"    call  TTF_Quit\n"
"    call  SDL_Quit\n\n"

"    xor   eax, eax\n"
"    leave\n"
"    ret\n"
    , f);
}

/* ═══════════════════════════════════════════════════════════════
 * SECCIÓN 5 — Punto de entrada público
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

    /* 3. Emitir .asm completo (datos + SDL2 en NASM) */
    emit_asm_completo(f);

    fclose(f);
    return 0;
}