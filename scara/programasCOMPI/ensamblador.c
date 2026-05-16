/*
 * ensamblador.c
 * Fase 6 — Generación de código final en NASM x64 real.
 *
 * Dos backends:
 *   OUTPUT_ASCII  →  animación HUD en consola via WriteConsoleA (kernel32)
 *   OUTPUT_SDL2   →  animación 2.5D via SDL2
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
 * SECCIÓN 2 — Helpers para emitir arrays de datos en NASM
 * ═══════════════════════════════════════════════════════════════ */

static void emit_array_dd(FILE* f, const char* nombre, int* arr, int n) {
    fprintf(f, "    %-14s dd  ", nombre);
    for (int i = 0; i < n; i++) {
        fprintf(f, "%d", arr[i]);
        if (i < n - 1) {
            fprintf(f, ",");
            /* Salto de línea cada 16 valores para legibilidad */
            if ((i + 1) % 16 == 0)
                fprintf(f, "\\\n                    ");
        }
    }
    fprintf(f, "\n");
}

static void emit_traza_data(FILE* f) {
    int tmp[MAX_TRAZA];

    fprintf(f, "    traza_len  dd  %d\n", traza_len);

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
}

/* ═══════════════════════════════════════════════════════════════
 * SECCIÓN 3 — Backend ASCII
 *
 * Emite NASM x64 completo que al ejecutarse muestra la animación
 * HUD 80×30 en consola usando syscalls de Windows (kernel32).
 * Sin dependencias externas — solo kernel32.dll del sistema.
 *
 * Enlazar con:
 *   ld programa.obj -o programa.exe -lkernel32
 *      -L"C:/msys64/mingw64/lib" --subsystem console
 * ═══════════════════════════════════════════════════════════════ */

static void emit_ascii(FILE* f) {

    /* ── Cabecera y directivas ── */
    fprintf(f,
        "; ============================================================\n"
        "; Generado por compilador SCARA — backend ASCII\n"
        "; Ensamblar: nasm -f win64 programa.asm -o programa.obj\n"
        "; Enlazar:   ld programa.obj -o programa.exe -lkernel32\n"
        ";            -L\"C:/msys64/mingw64/lib\" --subsystem console\n"
        "; ============================================================\n\n"
        "default rel\n\n"
        "extern GetStdHandle\n"
        "extern WriteConsoleA\n"
        "extern SetConsoleCursorPosition\n"
        "extern SetConsoleCursorInfo\n"
        "extern Sleep\n"
        "extern ExitProcess\n\n"
    );

    /* ── Sección de datos ── */
    fprintf(f, "section .data\n\n");
    fprintf(f, "    ; --- Traza de movimientos generada por el compilador ---\n");
    emit_traza_data(f);

    fprintf(f,
        "\n"
        "    ; --- Constantes del brazo ---\n"
        "    L1          dd  200\n"
        "    L2          dd  150\n\n"

        "    ; --- Strings UI ---\n"
        "    ; Box-drawing CP437\n"
        "    ch_hline    db  0xC4, 0\n"  /* ─ */
        "    ch_vline    db  0xB3, 0\n"  /* │ */
        "    ch_tl       db  0xDA, 0\n"  /* ┌ */
        "    ch_tr       db  0xBF, 0\n"  /* ┐ */
        "    ch_bl       db  0xC0, 0\n"  /* └ */
        "    ch_br       db  0xD9, 0\n"  /* ┘ */
        "    ch_ttee     db  0xC2, 0\n"  /* ┬ */
        "    ch_btee     db  0xC1, 0\n"  /* ┴ */
        "    ch_dot      db  '.', 0\n"
        "    ch_seg1     db  0xDB, 0\n"  /* █ */
        "    ch_seg2     db  0xB2, 0\n"  /* ▓ */
        "    ch_base     db  '()', 0\n"
        "    ch_elbow    db  'O', 0\n"
        "    ch_efopen   db  '*', 0\n"
        "    ch_efclose  db  'X', 0\n"
        "    ch_space    db  ' ', 0\n"
        "    ch_fill     db  0xDB, 0\n"  /* █ barra progreso */
        "    ch_empty    db  0xB0, 0\n"  /* ░ barra progreso */
        "    ch_newline  db  0x0D, 0x0A, 0\n\n"

        "    str_title   db  ' SCARA VM', 0\n"
        "    str_sep     db  ' --------', 0\n"
        "    str_lx      db  ' X:', 0\n"
        "    str_ly      db  ' Y:', 0\n"
        "    str_lz      db  ' Z:', 0\n"
        "    str_lvel    db  ' vel:', 0\n"
        "    str_lpct    db  '%', 0\n"
        "    str_lopen   db  ' [OPEN ]', 0\n"
        "    str_lclose  db  ' [CLOS ]', 0\n"
        "    str_barl    db  ' [', 0\n"
        "    str_barr    db  ']', 0\n\n"

        "    ; Buffer para conversión int→string\n"
        "    num_buf     db  20 dup(0)\n\n"

        "    ; Handle de stdout (rellenado en runtime)\n"
        "    hConsole    dq  0\n\n"
        "    ; CONSOLE_CURSOR_INFO { dwSize=1, bVisible=0 }\n"
        "    cur_size    dd  1\n"
        "    cur_visible dd  0\n\n"
        "    ; 80 espacios para limpiar cada fila (usar db con strings cortas)\n"
        "    cls_spaces  db  "
        "32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,"
        "32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,"
        "32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,"
        "32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,"
        "32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32, 0\n\n"
    );

    /* ── Sección BSS ── */
    fprintf(f,
        "section .bss\n"
        "    written     resq  1\n\n"
    );

    /* ══════════════════════════════════════
     * Sección .text — funciones auxiliares
     * ══════════════════════════════════════ */
    fprintf(f, "section .text\n\n");

    /* ── hide_cursor ── */
    fprintf(f,
        "; ----------------------------------------------------------\n"
        "; hide_cursor: oculta el cursor del terminal\n"
        "; ----------------------------------------------------------\n"
        "hide_cursor:\n"
        "    ; RSP entrada = 16n-8; sub 32 → 16n-40; ante call: 16n-48 ✓\n"
        "    sub  rsp, 32\n"
        "    mov  rcx, [rel hConsole]\n"
        "    lea  rdx, [rel cur_size]\n"
        "    call SetConsoleCursorInfo\n"
        "    add  rsp, 32\n"
        "    ret\n\n"
    );

    /* ── cls ── */
    /* Estrategia: mover cursor fila por fila (0..29) y escribir 80 espacios.
     * Evita el ABI complejo de FillConsoleOutputCharacterA y funciona
     * de forma fiable en cualquier consola Windows. */
    fprintf(f,
        "; ----------------------------------------------------------\n"
        "; cls: limpia la pantalla escribiendo 80 espacios en 30 filas\n"
        "; ----------------------------------------------------------\n"
        "cls:\n"
        "    push r12\n"
        "    push r13\n"
        "    ; Al entrar RSP = 16n-8 (si llamado directo) o múltiplo de 16 (desde render_frame)\n"
        "    ; La función se llama desde dos contextos distintos; para máxima portabilidad\n"
        "    ; usamos un sub que garantice RSP%16==0 ante las calls internas:\n"
        "    ; Desde mainCRTStartup (RSP=16n-56): -8(ret)-16(2push)-40(sub) → 16n-120 → OK\n"
        "    ; Desde render_frame  (RSP=16n-120): -8(ret)-16(2push)-40(sub) → 16n-184 → OK\n"
        "    sub  rsp, 40\n"
        "    xor  r12d, r12d        ; fila actual = 0\n"
        ".cls_row:\n"
        "    cmp  r12d, 30\n"
        "    jge  .cls_done\n"
        "    ; set_cursor(col=0, row=r12d)\n"
        "    xor  ecx, ecx\n"
        "    mov  edx, r12d\n"
        "    call set_cursor\n"
        "    ; escribir 80 espacios (shadow space en los 32 bytes de sub rsp,48)\n"
        "    mov  rcx, [rel hConsole]\n"
        "    lea  rdx, [rel cls_spaces]\n"
        "    mov  r8d, 80\n"
        "    lea  r9,  [rel written]\n"
        "    mov  qword [rsp+32], 0\n"
        "    call WriteConsoleA\n"
        "    inc  r12d\n"
        "    jmp  .cls_row\n"
        ".cls_done:\n"
        "    ; mover cursor a (0,0)\n"
        "    xor  ecx, ecx\n"
        "    xor  edx, edx\n"
        "    call set_cursor\n"
        "    add  rsp, 40\n"
        "    pop  r13\n"
        "    pop  r12\n"
        "    ret\n\n"
    );

    /* ── itoa_dec ── */
    fprintf(f,
        "; ----------------------------------------------------------\n"
        "; itoa_dec: entero con signo en ecx → string en [num_buf]\n"
        ";   Usa 32 bytes de pila local como buffer de inversión.\n"
        ";   modifica: rax, rbx, rcx, rdx\n"
        "; ----------------------------------------------------------\n"
        "itoa_dec:\n"
        "    push rsi\n"
        "    push rdi\n"
        "    push rbx\n"
        "    ; 3 pushes(24) + sub(32) = 56 → al entrar RSP era 16n-8\n"
        "    ; tras pushes RSP=16n-8-24=16n-32, tras sub RSP=16n-64  (ok mod16)\n"
        "    sub  rsp, 32\n"
        "    ; rsp+0..rsp+31 = buffer de trabajo de 32 bytes\n"
        "    lea  rdi, [rel num_buf]   ; destino final\n"
        "    mov  eax, ecx\n"
        "    test eax, eax\n"
        "    jge  .ita_pos\n"
        "    neg  eax\n"
        "    mov  byte [rdi], '-'\n"
        "    inc  rdi\n"
        ".ita_pos:\n"
        "    ; rbx apunta al final del buffer local (posición 30)\n"
        "    lea  rbx, [rsp + 30]\n"
        "    mov  byte [rbx], 0\n"
        "    dec  rbx\n"
        "    mov  ecx, 10\n"
        ".ita_loop:\n"
        "    xor  edx, edx\n"
        "    div  ecx\n"
        "    add  dl, '0'\n"
        "    mov  [rbx], dl\n"
        "    dec  rbx\n"
        "    test eax, eax\n"
        "    jnz  .ita_loop\n"
        "    inc  rbx\n"
        ".ita_copy:\n"
        "    mov  al, [rbx]\n"
        "    test al, al\n"
        "    jz   .ita_done\n"
        "    mov  [rdi], al\n"
        "    inc  rbx\n"
        "    inc  rdi\n"
        "    jmp  .ita_copy\n"
        ".ita_done:\n"
        "    mov  byte [rdi], 0\n"
        "    add  rsp, 32\n"
        "    pop  rbx\n"
        "    pop  rdi\n"
        "    pop  rsi\n"
        "    ret\n\n"
    );

    /* ── write_str ── */
    fprintf(f,
        "; ----------------------------------------------------------\n"
        "; write_str: escribe string null-terminated apuntado por rsi\n"
        "; ----------------------------------------------------------\n"
        "write_str:\n"
        "    push rbx\n"
        "    ; calcular longitud\n"
        "    xor  ebx, ebx\n"
        ".ws_len:\n"
        "    cmp  byte [rsi + rbx], 0\n"
        "    je   .ws_go\n"
        "    inc  ebx\n"
        "    jmp  .ws_len\n"
        ".ws_go:\n"
        "    test ebx, ebx\n"
        "    jz   .ws_done\n"
        "    ; 1 push(8) + sub(40) = 48 → RSP ante call: 16n-8-48-8 = 16n-64 ✓\n"
        "    sub  rsp, 40\n"
        "    mov  rcx, [rel hConsole]\n"
        "    mov  rdx, rsi\n"
        "    mov  r8d, ebx\n"
        "    lea  r9,  [rel written]\n"
        "    mov  qword [rsp+32], 0\n"
        "    call WriteConsoleA\n"
        "    add  rsp, 40\n"
        ".ws_done:\n"
        "    pop  rbx\n"
        "    ret\n\n"
    );

    /* ── set_cursor ── */
    fprintf(f,
        "; ----------------------------------------------------------\n"
        "; set_cursor: mueve cursor a (col=ecx, row=edx)\n"
        "; ----------------------------------------------------------\n"
        "set_cursor:\n"
        "    push rax\n"
        "    ; 1 push(8) + sub(40) = 48 → RSP ante call: 16n-8-48-8 = 16n-64 ✓\n"
        "    sub  rsp, 40\n"
        "    movzx eax, cx\n"
        "    movzx r10d, dx\n"
        "    shl  r10d, 16\n"
        "    or   eax, r10d\n"
        "    mov  rcx, [rel hConsole]\n"
        "    mov  edx, eax\n"
        "    call SetConsoleCursorPosition\n"
        "    add  rsp, 40\n"
        "    pop  rax\n"
        "    ret\n\n"
    );

    /* ── draw_hline ── */
    fprintf(f,
        "; ----------------------------------------------------------\n"
        "; draw_hline: dibuja 'count' copias de char en fila\n"
        ";   ecx=col_inicio, edx=row, r8d=count, rsi=ptr_char\n"
        "; ----------------------------------------------------------\n"
        "draw_hline:\n"
        "    push rbx\n"
        "    push r12\n"
        "    push r13\n"
        "    push r14\n"
        "    push rsi\n"
        "    ; 5 pushes(40) + 8 padding = 48 → RSP ante call: 16n-8-48-8 = 16n-64 ✓\n"
        "    sub  rsp, 8\n"
        "    mov  r12d, ecx\n"
        "    mov  r13d, edx\n"
        "    mov  r14d, r8d\n"
        ".dhl_loop:\n"
        "    test r14d, r14d\n"
        "    jz   .dhl_done\n"
        "    mov  ecx, r12d\n"
        "    mov  edx, r13d\n"
        "    call set_cursor\n"
        "    mov  rsi, [rsp+8]      ; ptr_char está 8 bytes arriba del padding\n"
        "    call write_str\n"
        "    inc  r12d\n"
        "    dec  r14d\n"
        "    jmp  .dhl_loop\n"
        ".dhl_done:\n"
        "    add  rsp, 8\n"
        "    pop  rsi\n"
        "    pop  r14\n"
        "    pop  r13\n"
        "    pop  r12\n"
        "    pop  rbx\n"
        "    ret\n\n"
    );

    /* ── draw_vline ── */
    fprintf(f,
        "; ----------------------------------------------------------\n"
        "; draw_vline: dibuja 'count' copias de char en columna\n"
        ";   ecx=col, edx=row_inicio, r8d=count, rsi=ptr_char\n"
        "; ----------------------------------------------------------\n"
        "draw_vline:\n"
        "    push rbx\n"
        "    push r12\n"
        "    push r13\n"
        "    push r14\n"
        "    push rsi\n"
        "    ; 5 pushes(40) + 8 padding = 48 → RSP ante call: 16n-8-48-8 = 16n-64 ✓\n"
        "    sub  rsp, 8\n"
        "    mov  r12d, ecx\n"
        "    mov  r13d, edx\n"
        "    mov  r14d, r8d\n"
        ".dvl_loop:\n"
        "    test r14d, r14d\n"
        "    jz   .dvl_done\n"
        "    mov  ecx, r12d\n"
        "    mov  edx, r13d\n"
        "    call set_cursor\n"
        "    mov  rsi, [rsp+8]      ; ptr_char está 8 bytes arriba del padding\n"
        "    call write_str\n"
        "    inc  r13d\n"
        "    dec  r14d\n"
        "    jmp  .dvl_loop\n"
        ".dvl_done:\n"
        "    add  rsp, 8\n"
        "    pop  rsi\n"
        "    pop  r14\n"
        "    pop  r13\n"
        "    pop  r12\n"
        "    pop  rbx\n"
        "    ret\n\n"
    );

    /* ── iso_project ── */
    fprintf(f,
        "; ----------------------------------------------------------\n"
        "; iso_project: proyección isométrica mundo→pantalla\n"
        ";   entrada: ecx=wx, edx=wy, r8d=wz\n"
        ";   salida : eax=sx (col), edx=sy (row)\n"
        ";   Fórmula:\n"
        ";     sx = 50 + (wx-wy)*70/100\n"
        ";     sy = 24 - ((wx+wy)*32/100 - wz*85/100)\n"
        ";   Origen (0,0,0) → pantalla (50,24)\n"
        "; ----------------------------------------------------------\n"
        "iso_project:\n"
        "    push rbx\n"
        "    push r9\n"
        "    ; ── sx ──\n"
        "    mov  eax, ecx\n"
        "    sub  eax, edx          ; wx - wy\n"
        "    imul eax, 70\n"
        "    cdq\n"
        "    mov  ebx, 100\n"
        "    idiv ebx\n"
        "    add  eax, 50\n"
        "    mov  r9d, eax          ; r9d = sx (temporal)\n"
        "    ; ── sy parte 1: (wx+wy)*32/100 ──\n"
        "    mov  eax, ecx\n"
        "    add  eax, edx\n"
        "    imul eax, 32\n"
        "    cdq\n"
        "    idiv ebx\n"
        "    push rax               ; guardar sum_part\n"
        "    ; ── sy parte 2: wz*85/100 ──\n"
        "    mov  eax, r8d\n"
        "    imul eax, 85\n"
        "    cdq\n"
        "    idiv ebx\n"
        "    pop  rbx               ; sum_part\n"
        "    sub  rbx, rax          ; sum_part - wz_part\n"
        "    mov  eax, 24\n"
        "    sub  eax, ebx          ; sy = 24 - ...\n"
        "    mov  edx, eax          ; retorno: edx = sy\n"
        "    mov  eax, r9d          ; retorno: eax = sx\n"
        "    pop  r9\n"
        "    pop  rbx\n"
        "    ret\n\n"
    );

    /* ── draw_segment (Bresenham simplificado) ── */
    fprintf(f,
        "; ----------------------------------------------------------\n"
        "; draw_segment: línea entre dos puntos de pantalla\n"
        ";   entrada: r12d=x0,r13d=y0,r14d=x1,r15d=y1, rsi=ptr_char\n"
        ";   preserva: rsi\n"
        "; ----------------------------------------------------------\n"
        "draw_segment:\n"
        "    push rbx\n"
        "    push r9\n"
        "    push r10\n"
        "    push r11\n"
        "    push rsi\n"
        "    ; Copias de trabajo en pila para no pisar r12-r15\n"
        "    mov  r9d,  r12d        ; cur_x\n"
        "    mov  r10d, r13d        ; cur_y\n"
        "    ; dx = |x1-x0|\n"
        "    mov  eax, r14d\n"
        "    sub  eax, r12d\n"
        "    cdq\n"
        "    xor  eax, edx\n"
        "    sub  eax, edx\n"
        "    mov  ebx, eax          ; ebx = dx\n"
        "    ; dy = |y1-y0|\n"
        "    mov  eax, r15d\n"
        "    sub  eax, r13d\n"
        "    cdq\n"
        "    xor  eax, edx\n"
        "    sub  eax, edx\n"
        "    push rax               ; [rsp+?] = dy\n"
        "    ; sx\n"
        "    mov  eax, 1\n"
        "    cmp  r12d, r14d\n"
        "    jle  .ds_sx_ok\n"
        "    neg  eax\n"
        ".ds_sx_ok:\n"
        "    push rax               ; sx\n"
        "    ; sy\n"
        "    mov  eax, 1\n"
        "    cmp  r13d, r15d\n"
        "    jle  .ds_sy_ok\n"
        "    neg  eax\n"
        ".ds_sy_ok:\n"
        "    push rax               ; sy\n"
        "    ; err = dx - dy\n"
        "    mov  r11d, ebx\n"
        "    sub  r11d, dword [rsp+16]  ; err = dx - dy\n"
        ".ds_loop:\n"
        "    ; Dibujar punto actual\n"
        "    mov  ecx, r9d\n"
        "    mov  edx, r10d\n"
        "    call set_cursor\n"
        "    mov  rsi, [rsp+24]     ; rsi = ptr_char\n"
        "    call write_str\n"
        "    ; ¿Llegamos al destino?\n"
        "    cmp  r9d,  r14d\n"
        "    jne  .ds_cont\n"
        "    cmp  r10d, r15d\n"
        "    je   .ds_done\n"
        ".ds_cont:\n"
        "    ; e2 = 2 * err\n"
        "    mov  eax, r11d\n"
        "    add  eax, eax\n"
        "    ; if e2 > -dy → err -= dy; x += sx\n"
        "    mov  ecx, dword [rsp+16]  ; dy\n"
        "    neg  ecx\n"
        "    cmp  eax, ecx\n"
        "    jle  .ds_skip_x\n"
        "    sub  r11d, dword [rsp+16]\n"
        "    add  r9d,  dword [rsp+8]  ; sx\n"
        ".ds_skip_x:\n"
        "    ; if e2 < dx → err += dx; y += sy\n"
        "    cmp  eax, ebx\n"
        "    jge  .ds_skip_y\n"
        "    add  r11d, ebx\n"
        "    add  r10d, dword [rsp+0]  ; sy\n"
        ".ds_skip_y:\n"
        "    jmp  .ds_loop\n"
        ".ds_done:\n"
        "    add  rsp, 24           ; limpiar sy, sx, dy\n"
        "    pop  rsi\n"
        "    pop  r11\n"
        "    pop  r10\n"
        "    pop  r9\n"
        "    pop  rbx\n"
        "    ret\n\n"
    );

    /* ── render_frame ── */
    fprintf(f,
        "; ----------------------------------------------------------\n"
        "; render_frame: dibuja un frame completo\n"
        ";   entrada: r13d = índice en traza  (r12-r15 son no-volátiles)\n"
        ";   Layout de locales [rbp - N]:\n"
        ";     -4  = idx (copia segura — NO se toca después)\n"
        ";     -8  = base_sx\n"
        ";     -12 = base_sy\n"
        ";     -16 = codo_sx\n"
        ";     -20 = codo_sy\n"
        ";     -24 = ef_sx\n"
        ";     -28 = ef_sy\n"
        ";     -32 = bloques_llenos\n"
        "; NOTA: r12-r15 se preservan via push/pop. r13 NO se usa dentro\n"
        ";       del cuerpo para nada más — idx se lee siempre de [rbp-4].\n"
        "; ----------------------------------------------------------\n"
        "render_frame:\n"
        "    push rbp\n"
        "    mov  rbp, rsp\n"
        "    ; Al entrar: RSP = 16n-8\n"
        "    ; push rbp → 16n-16; sub 72 → 16n-88; 4xpush → 16n-120\n"
        "    ; ante cualquier call interno: 16n-128 ✓\n"
        "    sub  rsp, 72\n"
        "    push r12\n"
        "    push r13\n"
        "    push r14\n"
        "    push r15\n"
        "\n"
        "    ; Guardar idx — a partir de aquí leer SIEMPRE de [rbp-4]\n"
        "    mov  dword [rbp-4], r13d\n"
        "\n"
        "    ; ── Limpiar pantalla antes de cada frame ──\n"
        "    call cls\n\n"

        "    ; ── Borde superior ──\n"
        "    xor  ecx, ecx\n"
        "    xor  edx, edx\n"
        "    mov  r8d, 80\n"
        "    lea  rsi, [rel ch_hline]\n"
        "    call draw_hline\n\n"

        "    ; ── Borde inferior ──\n"
        "    xor  ecx, ecx\n"
        "    mov  edx, 29\n"
        "    mov  r8d, 80\n"
        "    lea  rsi, [rel ch_hline]\n"
        "    call draw_hline\n\n"

        "    ; ── Borde izquierdo ──\n"
        "    xor  ecx, ecx\n"
        "    mov  edx, 1\n"
        "    mov  r8d, 28\n"
        "    lea  rsi, [rel ch_vline]\n"
        "    call draw_vline\n\n"

        "    ; ── Borde derecho ──\n"
        "    mov  ecx, 79\n"
        "    mov  edx, 1\n"
        "    mov  r8d, 28\n"
        "    lea  rsi, [rel ch_vline]\n"
        "    call draw_vline\n\n"

        "    ; ── Divisor panel col=20 ──\n"
        "    mov  ecx, 20\n"
        "    mov  edx, 1\n"
        "    mov  r8d, 28\n"
        "    lea  rsi, [rel ch_vline]\n"
        "    call draw_vline\n\n"

        "    ; ════ PANEL IZQUIERDO ════\n"
        "    mov  ecx, 1\n"
        "    mov  edx, 1\n"
        "    call set_cursor\n"
        "    lea  rsi, [rel str_title]\n"
        "    call write_str\n\n"

        "    ; --- leer idx para indexar arrays ---\n"
        "    movsxd rax, dword [rbp-4]   ; rax = idx (64-bit)\n\n"

        "    ; X\n"
        "    mov  ecx, 1\n"
        "    mov  edx, 3\n"
        "    call set_cursor\n"
        "    lea  rsi, [rel str_lx]\n"
        "    call write_str\n"
        "    lea  r10, [rel traza_x]\n"
        "    movsxd rax, dword [rbp-4]\n"
        "    mov  ecx, dword [r10 + rax*4]\n"
        "    call itoa_dec\n"
        "    lea  rsi, [rel num_buf]\n"
        "    call write_str\n\n"

        "    ; Y\n"
        "    mov  ecx, 1\n"
        "    mov  edx, 4\n"
        "    call set_cursor\n"
        "    lea  rsi, [rel str_ly]\n"
        "    call write_str\n"
        "    lea  r10, [rel traza_y]\n"
        "    movsxd rax, dword [rbp-4]\n"
        "    mov  ecx, dword [r10 + rax*4]\n"
        "    call itoa_dec\n"
        "    lea  rsi, [rel num_buf]\n"
        "    call write_str\n\n"

        "    ; Z\n"
        "    mov  ecx, 1\n"
        "    mov  edx, 5\n"
        "    call set_cursor\n"
        "    lea  rsi, [rel str_lz]\n"
        "    call write_str\n"
        "    lea  r10, [rel traza_z]\n"
        "    movsxd rax, dword [rbp-4]\n"
        "    mov  ecx, dword [r10 + rax*4]\n"
        "    call itoa_dec\n"
        "    lea  rsi, [rel num_buf]\n"
        "    call write_str\n\n"

        "    ; Velocidad\n"
        "    mov  ecx, 1\n"
        "    mov  edx, 7\n"
        "    call set_cursor\n"
        "    lea  rsi, [rel str_lvel]\n"
        "    call write_str\n"
        "    lea  r10, [rel traza_vel]\n"
        "    movsxd rax, dword [rbp-4]\n"
        "    mov  ecx, dword [r10 + rax*4]\n"
        "    call itoa_dec\n"
        "    lea  rsi, [rel num_buf]\n"
        "    call write_str\n"
        "    lea  rsi, [rel str_lpct]\n"
        "    call write_str\n\n"

        "    ; Pinza\n"
        "    mov  ecx, 1\n"
        "    mov  edx, 9\n"
        "    call set_cursor\n"
        "    lea  r10, [rel traza_pinza]\n"
        "    movsxd rax, dword [rbp-4]\n"
        "    mov  eax, dword [r10 + rax*4]\n"
        "    test eax, eax\n"
        "    jz   .rf_pinza_close\n"
        "    lea  rsi, [rel str_lopen]\n"
        "    jmp  .rf_pinza_show\n"
        ".rf_pinza_close:\n"
        "    lea  rsi, [rel str_lclose]\n"
        ".rf_pinza_show:\n"
        "    call write_str\n\n"

        "    ; ════ BRAZO (proyección isométrica) ════\n"
        "    ; Proyectar base (0,0,0)\n"
        "    xor  ecx, ecx\n"
        "    xor  edx, edx\n"
        "    xor  r8d, r8d\n"
        "    call iso_project\n"
        "    mov  dword [rbp-8],  eax    ; base_sx\n"
        "    mov  dword [rbp-12], edx    ; base_sy\n\n"

        "    ; Calcular posición del codo (aprox L1/(L1+L2) del efector)\n"
        "    movsxd rax, dword [rbp-4]\n"
        "    lea  r10, [rel traza_x]\n"
        "    mov  ecx, dword [r10 + rax*4]  ; wx_ef\n"
        "    lea  r10, [rel traza_y]\n"
        "    mov  edx, dword [r10 + rax*4]  ; wy_ef\n"
        "    lea  r10, [rel traza_z]\n"
        "    mov  r8d, dword [r10 + rax*4]  ; wz_ef\n"
        "    ; codo ≈ (X*200/350, Y*200/350, Z)\n"
        "    push r8                        ; salvar wz\n"
        "    push rdx                       ; salvar wy_ef\n"
        "    imul ecx, 200\n"
        "    cdq\n"
        "    mov  eax, ecx\n"
        "    mov  ecx, 350\n"
        "    idiv ecx\n"
        "    mov  ecx, eax                  ; codo_x\n"
        "    pop  rdx                       ; wy_ef\n"
        "    push rcx                       ; salvar codo_x\n"
        "    mov  eax, edx\n"
        "    imul eax, 200\n"
        "    cdq\n"
        "    mov  ecx, 350\n"
        "    idiv ecx\n"
        "    mov  edx, eax                  ; codo_y\n"
        "    pop  rcx                       ; codo_x\n"
        "    pop  r8                        ; wz\n"
        "    call iso_project\n"
        "    mov  dword [rbp-16], eax    ; codo_sx\n"
        "    mov  dword [rbp-20], edx    ; codo_sy\n\n"

        "    ; Proyectar efector (X, Y, Z)\n"
        "    movsxd rax, dword [rbp-4]\n"
        "    lea  r10, [rel traza_x]\n"
        "    mov  ecx, dword [r10 + rax*4]\n"
        "    lea  r10, [rel traza_y]\n"
        "    mov  edx, dword [r10 + rax*4]\n"
        "    lea  r10, [rel traza_z]\n"
        "    mov  r8d, dword [r10 + rax*4]\n"
        "    call iso_project\n"
        "    mov  dword [rbp-24], eax    ; ef_sx\n"
        "    mov  dword [rbp-28], edx    ; ef_sy\n\n"

        "    ; Segmento 1: base → codo  (█)\n"
        "    mov  r12d, dword [rbp-8]\n"
        "    mov  r13d, dword [rbp-12]\n"
        "    mov  r14d, dword [rbp-16]\n"
        "    mov  r15d, dword [rbp-20]\n"
        "    lea  rsi, [rel ch_seg1]\n"
        "    call draw_segment\n\n"

        "    ; Segmento 2: codo → efector  (▓)\n"
        "    mov  r12d, dword [rbp-16]\n"
        "    mov  r13d, dword [rbp-20]\n"
        "    mov  r14d, dword [rbp-24]\n"
        "    mov  r15d, dword [rbp-28]\n"
        "    lea  rsi, [rel ch_seg2]\n"
        "    call draw_segment\n\n"

        "    ; Marcar base '()'\n"
        "    mov  ecx, dword [rbp-8]\n"
        "    mov  edx, dword [rbp-12]\n"
        "    call set_cursor\n"
        "    lea  rsi, [rel ch_base]\n"
        "    call write_str\n\n"

        "    ; Marcar codo 'O'\n"
        "    mov  ecx, dword [rbp-16]\n"
        "    mov  edx, dword [rbp-20]\n"
        "    call set_cursor\n"
        "    lea  rsi, [rel ch_elbow]\n"
        "    call write_str\n\n"

        "    ; Marcar efector (* ó X según pinza)\n"
        "    mov  ecx, dword [rbp-24]\n"
        "    mov  edx, dword [rbp-28]\n"
        "    call set_cursor\n"
        "    lea  r10, [rel traza_pinza]\n"
        "    movsxd rax, dword [rbp-4]\n"
        "    mov  eax, dword [r10 + rax*4]\n"
        "    test eax, eax\n"
        "    jz   .rf_ef_close\n"
        "    lea  rsi, [rel ch_efopen]\n"
        "    jmp  .rf_ef_show\n"
        ".rf_ef_close:\n"
        "    lea  rsi, [rel ch_efclose]\n"
        ".rf_ef_show:\n"
        "    call write_str\n\n"

        "    ; ════ BARRA DE PROGRESO ════\n"
        "    mov  ecx, 21\n"
        "    mov  edx, 28\n"
        "    call set_cursor\n"
        "    lea  rsi, [rel str_barl]\n"
        "    call write_str\n\n"
        "    ; Calcular bloques llenos: (idx * 55) / traza_len\n"
        "    mov  eax, dword [rbp-4]     ; idx\n"
        "    imul eax, 55\n"
        "    cdq\n"
        "    lea  r10, [rel traza_len]\n"
        "    idiv dword [r10]\n"
        "    mov  dword [rbp-32], eax   ; guardar bloques_llenos\n"
        "    mov  r8d, eax              ; bloques llenos\n"
        "    mov  ecx, 23\n"
        "    mov  edx, 28\n"
        "    lea  rsi, [rel ch_fill]\n"
        "    call draw_hline\n\n"
        "    ; Bloques vacíos\n"
        "    mov  eax, 55\n"
        "    sub  eax, dword [rbp-32]\n"
        "    mov  r8d, eax\n"
        "    mov  ecx, 23\n"
        "    add  ecx, dword [rbp-32]   ; 23 + bloques_llenos\n"
        "    mov  edx, 28\n"
        "    lea  rsi, [rel ch_empty]\n"
        "    call draw_hline\n\n"
        "    mov  ecx, 78\n"
        "    mov  edx, 28\n"
        "    call set_cursor\n"
        "    lea  rsi, [rel str_barr]\n"
        "    call write_str\n\n"

        "    pop  r15\n"
        "    pop  r14\n"
        "    pop  r13\n"
        "    pop  r12\n"
        "    mov  rsp, rbp\n"
        "    pop  rbp\n"
        "    ret\n\n"
    );

    /* ── Punto de entrada ── */
    fprintf(f,
        "; ----------------------------------------------------------\n"
        "; Punto de entrada del ejecutable\n"
        "; r13d = índice de traza (registro no-volátil, preservado por render_frame)\n"
        "; ----------------------------------------------------------\n"
        "global mainCRTStartup\n"
        "mainCRTStartup:\n"
        "    push r13\n"
        "    push r14\n"
        "    sub  rsp, 40\n\n"
        "    ; Obtener handle stdout\n"
        "    mov  ecx, -11\n"
        "    call GetStdHandle\n"
        "    mov  [rel hConsole], rax\n\n"
        "    ; Ocultar cursor y limpiar pantalla\n"
        "    call hide_cursor\n"
        "    call cls\n\n"
        "    ; Loop principal sobre la traza\n"
        "    xor  r13d, r13d\n"
        ".main_loop:\n"
        "    lea  r14, [rel traza_len]\n"
        "    cmp  r13d, dword [r14]\n"
        "    jge  .main_done\n\n"
        "    ; render_frame recibe el índice en r13d\n"
        "    call render_frame\n\n"
        "    ; Delay basado en velocidad del estado actual\n"
        "    sub  rsp, 32\n"
        "    lea  r14, [rel traza_vel]\n"
        "    movsxd rax, r13d\n"
        "    mov  eax, dword [r14 + rax*4]\n"
        "    ; delay_ms = 220 - vel*180/100\n"
        "    imul eax, 180\n"
        "    cdq\n"
        "    mov  ecx, 100\n"
        "    idiv ecx\n"
        "    mov  ecx, 220\n"
        "    sub  ecx, eax\n"
        "    cmp  ecx, 35\n"
        "    jge  .delay_ok\n"
        "    mov  ecx, 35\n"
        ".delay_ok:\n"
        "    call Sleep\n"
        "    add  rsp, 32\n\n"
        "    inc  r13d\n"
        "    jmp  .main_loop\n\n"
        ".main_done:\n"
        "    ; Pausa final de 2 segundos\n"
        "    sub  rsp, 32\n"
        "    mov  ecx, 2000\n"
        "    call Sleep\n"
        "    add  rsp, 32\n\n"
        "    add  rsp, 40\n"
        "    pop  r14\n"
        "    pop  r13\n"
        "    xor  ecx, ecx\n"
        "    call ExitProcess\n"
    );
}

/* ═══════════════════════════════════════════════════════════════
 * SECCIÓN 4 — Backend SDL2
 *
 * Emite la traza como datos NASM con funciones accesibles desde C.
 * El visualizador SDL2 existente en main.c llama estas funciones
 * para obtener los estados en lugar de ejecutar la VM.
 * ═══════════════════════════════════════════════════════════════ */

static void emit_sdl2(FILE* f) {
    fprintf(f,
        "; ============================================================\n"
        "; Generado por compilador SCARA — backend SDL2\n"
        "; Datos de traza exportados como símbolos enlazables.\n"
        "; El visualizador SDL2 llama traza_get_* para obtener estados.\n"
        "; Ensamblar: nasm -f win64 programa.asm -o programa.obj\n"
        "; Enlazar:   gcc visualizador.c programa.obj -lSDL2 -o programa.exe\n"
        "; ============================================================\n\n"
        "default rel\n\n"
        "section .data\n\n"
    );

    emit_traza_data(f);

    fprintf(f,
        "\nsection .text\n\n"
        "global traza_get_len\n"
        "global traza_get_x\n"
        "global traza_get_y\n"
        "global traza_get_z\n"
        "global traza_get_pinza\n"
        "global traza_get_vel\n\n"

        "; int traza_get_len(void)\n"
        "traza_get_len:\n"
        "    lea  rax, [rel traza_len]\n"
        "    mov  eax, dword [rax]\n"
        "    ret\n\n"

        "; int traza_get_x(int idx)     [idx en ecx — Windows x64 ABI]\n"
        "traza_get_x:\n"
        "    movsxd rcx, ecx\n"
        "    lea  rax, [rel traza_x]\n"
        "    mov  eax, dword [rax + rcx*4]\n"
        "    ret\n\n"

        "traza_get_y:\n"
        "    movsxd rcx, ecx\n"
        "    lea  rax, [rel traza_y]\n"
        "    mov  eax, dword [rax + rcx*4]\n"
        "    ret\n\n"

        "traza_get_z:\n"
        "    movsxd rcx, ecx\n"
        "    lea  rax, [rel traza_z]\n"
        "    mov  eax, dword [rax + rcx*4]\n"
        "    ret\n\n"

        "traza_get_pinza:\n"
        "    movsxd rcx, ecx\n"
        "    lea  rax, [rel traza_pinza]\n"
        "    mov  eax, dword [rax + rcx*4]\n"
        "    ret\n\n"

        "traza_get_vel:\n"
        "    movsxd rcx, ecx\n"
        "    lea  rax, [rel traza_vel]\n"
        "    mov  eax, dword [rax + rcx*4]\n"
        "    ret\n"
    );
}

/* ═══════════════════════════════════════════════════════════════
 * SECCIÓN 5 — Punto de entrada público
 * ═══════════════════════════════════════════════════════════════ */

int generar_ensamblador(const Instruccion* programa, int longitud,
                        const char* ruta_salida, OutputMode modo) {
    /* 1. Ejecutar el bytecode para construir la traza */
    int n = construir_traza(programa, longitud);
    if (n <= 0) {
        fprintf(stderr, "Error: traza vacia — revisa el programa fuente\n");
        return 1;
    }
    printf("[GEN] Traza construida: %d estados (modo: %s)\n",
           n, modo == OUTPUT_ASCII ? "ascii" : "sdl2");

    /* 2. Abrir archivo de salida */
    FILE* f = fopen(ruta_salida, "w");
    if (!f) return 1;

    /* 3. Delegar al backend correspondiente */
    if (modo == OUTPUT_ASCII) {
        emit_ascii(f);
    } else {
        emit_sdl2(f);
    }

    fclose(f);
    return 0;
}