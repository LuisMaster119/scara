#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lexer.h"
#include "parser.h"
#include "simbolos.h"
#include "errores.h"
#include "optimizador.h"
#include "ensamblador.h"
#include "cinematica.h"

// ── leer archivo ──────────────────────────────────────────
char* leer_archivo(const char* ruta) {
    FILE* f = fopen(ruta, "rb");
    if (!f) { fprintf(stderr, "Error: no se puede abrir '%s'\n", ruta); exit(1); }
    fseek(f, 0, SEEK_END);
    long tam = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = malloc(tam + 1);
    if (!buf) { fprintf(stderr, "Error: memoria insuficiente\n"); exit(1); }
    buf[fread(buf, 1, tam, f)] = '\0';
    fclose(f);
    return buf;
}

// ── main ──────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uso: scara.exe <archivo.scara>\n");
        fprintf(stderr, "Ejemplo: scara.exe programa.scara\n");
        return 1;
    }
    const char* ruta_programa = argv[1];

    // ── 1. Leer fuente ──
    char* programa = leer_archivo(ruta_programa);
    error_init(programa);
    printf("=== COMPILANDO: %s ===\n", ruta_programa);

    // ── 2. Lexer + Parser ──
    Lexer lex;
    lexer_init(&lex, programa);
    Parser parser;
    parser_init(&parser, &lex);
    parsear_programa(&parser);

    printf("\n=== TABLA DE SIMBOLOS ===\n");
    tabla_imprimir();

    printf("\n=== BYTECODE GENERADO ===\n");
    printf("%-4s %-14s %-6s %-6s %-6s %-5s %-14s %-14s %-14s\n",
           "PC", "OPCODE", "ARG1", "ARG2", "ARG3", "FLAGS", "SVAL", "SVAL2", "SVAL3");
    printf("-----------------------------------------------------------------------------------------\n");
    for (int i = 0; i < bytecode_len; i++) {
        Instruccion* ins = &bytecode[i];
        printf("%-4d %-14s %-6d %-6d %-6d %-5d %-14s %-14s %-14s\n",
               i, opcode_a_texto(ins->opcode),
               ins->arg1, ins->arg2, ins->arg3, ins->flags,
               ins->sval, ins->sval2, ins->sval3);
    }

    // ── 3. Optimizar ──
    Instruccion bytecode_opt[MAX_INSTRUCCIONES];
    int opt_len = optimizar_bytecode(bytecode, bytecode_len,
                                     bytecode_opt, MAX_INSTRUCCIONES);
    if (opt_len < 0) {
        fprintf(stderr, "Error: fallo en optimizacion\n");
        free(programa); return 1;
    }

    printf("\n=== BYTECODE OPTIMIZADO ===\n");
    printf("%-4s %-14s %-6s %-6s %-6s %-5s %-14s %-14s %-14s\n",
           "PC", "OPCODE", "ARG1", "ARG2", "ARG3", "FLAGS", "SVAL", "SVAL2", "SVAL3");
    printf("-----------------------------------------------------------------------------------------\n");
    for (int i = 0; i < opt_len; i++) {
        Instruccion* ins = &bytecode_opt[i];
        printf("%-4d %-14s %-6d %-6d %-6d %-5d %-14s %-14s %-14s\n",
               i, opcode_a_texto(ins->opcode),
               ins->arg1, ins->arg2, ins->arg3, ins->flags,
               ins->sval, ins->sval2, ins->sval3);
    }

    // ── 4. Generar .asm ──
    char ruta_asm[512];
    const char* ext = strrchr(ruta_programa, '.');
    if (ext) {
        size_t n = ext - ruta_programa;
        if (n >= sizeof(ruta_asm)) n = sizeof(ruta_asm) - 1;
        memcpy(ruta_asm, ruta_programa, n);
        ruta_asm[n] = '\0';
    } else {
        snprintf(ruta_asm, sizeof(ruta_asm), "%s", ruta_programa);
    }
    strcat(ruta_asm, ".asm");

    if (generar_ensamblador(bytecode_opt, opt_len, ruta_asm) != 0) {
        fprintf(stderr, "Error: no se pudo generar '%s'\n", ruta_asm);
        free(programa); return 1;
    }
    printf("\n=== ASM GENERADO: %s ===\n", ruta_asm);

    // ── 5. Ensamblar con NASM ──
    char ruta_obj[512];
    snprintf(ruta_obj, sizeof(ruta_obj), "%.*s.obj",
             (int)(strlen(ruta_asm) - 4), ruta_asm);

    char cmd_nasm[1024];
    snprintf(cmd_nasm, sizeof(cmd_nasm),
             "nasm -f win64 \"%s\" -o \"%s\"", ruta_asm, ruta_obj);
    printf("\n=== ENSAMBLANDO: %s ===\n", cmd_nasm);
    if (system(cmd_nasm) != 0) {
        fprintf(stderr, "Error: nasm fallo\n");
        free(programa); return 1;
    }

    // ── 6. Generar visualizador SDL2 y enlazar ──
    char ruta_exe[512];
    snprintf(ruta_exe, sizeof(ruta_exe), "%.*s.exe",
             (int)(strlen(ruta_asm) - 4), ruta_asm);

    char ruta_vis[512];
    snprintf(ruta_vis, sizeof(ruta_vis), "%.*s_vis.c",
             (int)(strlen(ruta_asm) - 4), ruta_asm);

    FILE* fvis = fopen(ruta_vis, "w");
    if (!fvis) {
        fprintf(stderr, "Error: no se puede crear visualizador SDL2\n");
        free(programa); return 1;
    }

    fprintf(fvis,
"/* Visualizador SDL2 generado automaticamente por el compilador SCARA */\n"
"#define SDL_MAIN_HANDLED\n"
"#include <SDL2/SDL.h>\n"
"#include <stdio.h>\n"
"#include <math.h>\n\n"
"extern int traza_get_len(void);\n"
"extern int traza_get_x(int idx);\n"
"extern int traza_get_y(int idx);\n"
"extern int traza_get_z(int idx);\n"
"extern int traza_get_pinza(int idx);\n"
"extern int traza_get_vel(int idx);\n\n"
"#define WIN_W 900\n"
"#define WIN_H 600\n"
"#define L1 200.0\n"
"#define L2 150.0\n\n"
"static void iso(int wx, int wy, int wz, int *sx, int *sy) {\n"
"    *sx = WIN_W/2 + (wx - wy) * 70 / 100;\n"
"    *sy = WIN_H/2 - ((wx + wy) * 32 / 100 - wz * 85 / 100);\n"
"}\n\n"
"static int ik_codo(double ex, double ey, double *cx, double *cy) {\n"
"    double r2 = ex*ex + ey*ey;\n"
"    double c2 = (r2 - L1*L1 - L2*L2) / (2.0*L1*L2);\n"
"    if (c2 < -1.0 || c2 > 1.0) {\n"
"        double d = sqrt(r2);\n"
"        *cx = ex * L1 / (d > 1.0 ? d : 1.0);\n"
"        *cy = ey * L1 / (d > 1.0 ? d : 1.0);\n"
"        return 0;\n"
"    }\n"
"    double q2 = acos(c2);\n"
"    double k1 = L1 + L2*cos(q2);\n"
"    double k2 = L2*sin(q2);\n"
"    double q1 = atan2(ey, ex) - atan2(k2, k1);\n"
"    *cx = L1 * cos(q1);\n"
"    *cy = L1 * sin(q1);\n"
"    return 1;\n"
"}\n\n"
"static void draw_circle(SDL_Renderer *r, int cx, int cy, int rad) {\n"
"    for (int w = 0; w < rad*2; w++)\n"
"        for (int h = 0; h < rad*2; h++) {\n"
"            int dx = rad-w, dy = rad-h;\n"
"            if (dx*dx+dy*dy <= rad*rad)\n"
"                SDL_RenderDrawPoint(r, cx+dx, cy+dy);\n"
"        }\n"
"}\n\n"
"int main(int argc, char *argv[]) {\n"
"    (void)argc; (void)argv;\n"
"    SDL_SetMainReady();\n"
"    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;\n"
"    SDL_Window *win = SDL_CreateWindow(\"SCARA — Compilador\",\n"
"        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,\n"
"        WIN_W, WIN_H, SDL_WINDOW_SHOWN);\n"
"    SDL_Renderer *ren = SDL_CreateRenderer(win, -1,\n"
"        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);\n\n"
"    int total = traza_get_len();\n"
"    int idx = 0, running = 1;\n"
"    SDL_Event ev;\n\n"
"    while (running && idx < total) {\n"
"        while (SDL_PollEvent(&ev))\n"
"            if (ev.type == SDL_QUIT) running = 0;\n\n"
"        int wx = traza_get_x(idx), wy = traza_get_y(idx);\n"
"        int wz = traza_get_z(idx);\n"
"        int pinza = traza_get_pinza(idx);\n"
"        int vel   = traza_get_vel(idx);\n\n"
"        SDL_SetRenderDrawColor(ren, 15, 15, 25, 255);\n"
"        SDL_RenderClear(ren);\n\n"
"        /* cuadricula */\n"
"        SDL_SetRenderDrawColor(ren, 30, 30, 50, 255);\n"
"        for (int g = -350; g <= 350; g += 50) {\n"
"            int ax,ay,bx,by;\n"
"            iso(g,-350,0,&ax,&ay); iso(g,350,0,&bx,&by);\n"
"            SDL_RenderDrawLine(ren,ax,ay,bx,by);\n"
"            iso(-350,g,0,&ax,&ay); iso(350,g,0,&bx,&by);\n"
"            SDL_RenderDrawLine(ren,ax,ay,bx,by);\n"
"        }\n\n"
"        double ck_x=0, ck_y=0;\n"
"        ik_codo((double)wx,(double)wy,&ck_x,&ck_y);\n"
"        int cx=(int)round(ck_x), cy=(int)round(ck_y), cz=wz;\n\n"
"        int bsx,bsy,esx,esy,csx,csy;\n"
"        iso(0,0,0,&bsx,&bsy);\n"
"        iso(cx,cy,cz,&csx,&csy);\n"
"        iso(wx,wy,wz,&esx,&esy);\n\n"
"        SDL_SetRenderDrawColor(ren, 70, 130, 220, 255);\n"
"        for (int t=-2;t<=2;t++) {\n"
"            SDL_RenderDrawLine(ren,bsx+t,bsy,csx+t,csy);\n"
"            SDL_RenderDrawLine(ren,bsx,bsy+t,csx,csy+t);\n"
"        }\n"
"        SDL_SetRenderDrawColor(ren, 180, 100, 40, 255);\n"
"        for (int t=-2;t<=2;t++) {\n"
"            SDL_RenderDrawLine(ren,csx+t,csy,esx+t,esy);\n"
"            SDL_RenderDrawLine(ren,csx,csy+t,esx,esy+t);\n"
"        }\n\n"
"        SDL_SetRenderDrawColor(ren, 200, 200, 200, 255);\n"
"        draw_circle(ren,bsx,bsy,8);\n"
"        SDL_SetRenderDrawColor(ren, 100, 200, 100, 255);\n"
"        draw_circle(ren,csx,csy,6);\n"
"        if (pinza) SDL_SetRenderDrawColor(ren, 255, 220, 50, 255);\n"
"        else       SDL_SetRenderDrawColor(ren, 255,  60, 60, 255);\n"
"        draw_circle(ren,esx,esy,9);\n"
"        {\n"
"            double ddx=esx-csx, ddy=esy-csy;\n"
"            double d=sqrt(ddx*ddx+ddy*ddy);\n"
"            if (d>0.5) {\n"
"                int px=(int)(-ddy/d*(pinza?8:4));\n"
"                int py=(int)(ddx/d*(pinza?8:4));\n"
"                SDL_RenderDrawLine(ren,esx+px,esy+py,\n"
"                    esx+(int)(ddx/d*12)+px,esy+(int)(ddy/d*12)+py);\n"
"                SDL_RenderDrawLine(ren,esx-px,esy-py,\n"
"                    esx+(int)(ddx/d*12)-px,esy+(int)(ddy/d*12)-py);\n"
"            }\n"
"        }\n\n"
"        SDL_SetRenderDrawColor(ren, 50, 50, 80, 255);\n"
"        SDL_Rect bar_bg = {10, WIN_H-30, WIN_W-20, 16};\n"
"        SDL_RenderFillRect(ren, &bar_bg);\n"
"        SDL_SetRenderDrawColor(ren, 70, 130, 220, 255);\n"
"        SDL_Rect bar_fg = {10, WIN_H-30, (WIN_W-20)*idx/total, 16};\n"
"        SDL_RenderFillRect(ren, &bar_fg);\n\n"
"        SDL_RenderPresent(ren);\n"
"        int delay = 220 - vel*180/100;\n"
"        if (delay < 35) delay = 35;\n"
"        SDL_Delay(delay);\n"
"        idx++;\n"
"    }\n\n"
"    SDL_Delay(2000);\n"
"    SDL_DestroyRenderer(ren);\n"
"    SDL_DestroyWindow(win);\n"
"    SDL_Quit();\n"
"    return 0;\n"
"}\n"
    );
    fclose(fvis);
    printf("[GEN] Visualizador SDL2: %s\n", ruta_vis);

    // ── 7. Enlazar: gcc visualizador.c + .obj ASM + SDL2 ──
    char cmd_ld[1024];
    snprintf(cmd_ld, sizeof(cmd_ld),
             "gcc -O2 \"%s\" \"%s\" -o \"%s\" "
             "-I\"C:/msys64/mingw64/include\" "
             "-I\"C:/msys64/mingw64/include/SDL2\" "
             "-L\"C:/msys64/mingw64/lib\" "
             "-lSDL2 -lSDL2main -lmingw32 -lm "
             "-mwindows",
             ruta_vis, ruta_obj, ruta_exe);
    printf("\n=== ENLAZANDO: %s ===\n", cmd_ld);
    if (system(cmd_ld) != 0) {
        fprintf(stderr, "Error: enlazado fallo\n");
        free(programa); return 1;
    }

    // ── 8. Ejecutar el binario generado ──
    printf("\n=== EJECUTANDO: %s ===\n", ruta_exe);
    char cmd_run[512];
    snprintf(cmd_run, sizeof(cmd_run), "\"%s\"", ruta_exe);
    system(cmd_run);

    free(programa);
    return 0;
}