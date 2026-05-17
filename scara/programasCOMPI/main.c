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

    // ── 4. Construir traza, generar .asm Y _vis.c ──
    char base[512];   /* ruta sin extensión */
    {
        const char* ext = strrchr(ruta_programa, '.');
        if (ext) {
            size_t n = (size_t)(ext - ruta_programa);
            if (n >= sizeof(base)) n = sizeof(base) - 1;
            memcpy(base, ruta_programa, n);
            base[n] = '\0';
        } else {
            snprintf(base, sizeof(base), "%s", ruta_programa);
        }
    }

    char ruta_asm[512];
    snprintf(ruta_asm, sizeof(ruta_asm), "%s.asm", base);
    char ruta_exe[512];
    snprintf(ruta_exe, sizeof(ruta_exe), "%s.exe", base);
    char ruta_vis[512];
    snprintf(ruta_vis, sizeof(ruta_vis), "%s_vis.c", base);

    // ── Generar .asm (datos de traza en NASM, para el requisito de la materia) ──
    if (generar_ensamblador(bytecode_opt, opt_len, ruta_asm) != 0) {
        fprintf(stderr, "Error: no se pudo generar '%s'\n", ruta_asm);
        free(programa); return 1;
    }
    printf("\n=== ASM GENERADO: %s ===\n", ruta_asm);

    // ── Generar _vis.c con los datos de traza en C puro (sin puente ASM) ──
    FILE* fvis = fopen(ruta_vis, "w");
    if (!fvis) {
        fprintf(stderr, "Error: no se puede crear visualizador SDL2\n");
        free(programa); return 1;
    }


    fprintf(fvis,
"/* Visualizador SDL2 + SDL2_ttf generado por el compilador SCARA */\n"
"#define SDL_MAIN_HANDLED\n"
"#include <SDL2/SDL.h>\n"
"#include <SDL2/SDL_ttf.h>\n"
"#include <stdio.h>\n"
"#include <math.h>\n\n");

    fprintf(fvis,
"/* Funciones importadas del codigo ensamblador (compilado con NASM) */\n"
"extern int traza_get_len(void);\n"
"extern int traza_get_x(int idx);\n"
"extern int traza_get_y(int idx);\n"
"extern int traza_get_z(int idx);\n"
"extern int traza_get_pinza(int idx);\n"
"extern int traza_get_vel(int idx);\n\n");

    fprintf(fvis,
"#define WIN_W    900\n"
"#define WIN_H    600\n"
"#define L1       200.0\n"
"#define L2       150.0\n"
"#define PANEL_W  200\n"
"#define FONT_SIZE 16\n\n"

/* ── Sistema de vistas ── */
"/* ================================================================\n"
" *  SISTEMA DE VISTAS\n"
" *  0 = Isometrica  (diagonal, vista por defecto)\n"
" *  1 = Superior    (planta XY, Z ignorado)\n"
" *  2 = Lateral     (perfil: distancia radial vs Z)\n"
" * ================================================================ */\n"
"static int modo_vista = 0;\n\n"

/* Funciones de proyeccion */
"/* Proyeccion isometrica */\n"
"static void proj_iso(int wx, int wy, int wz, int *sx, int *sy) {\n"
"    *sx = (WIN_W-PANEL_W)/2 + (wx - wy) * 70 / 100;\n"
"    *sy = WIN_H/2 - ((wx + wy) * 32 / 100 - wz * 85 / 100);\n"
"}\n\n"

"/* Proyeccion superior: XY plano, Z ignorado */\n"
"static void proj_top(int wx, int wy, int wz, int *sx, int *sy) {\n"
"    (void)wz;\n"
"    *sx = (WIN_W-PANEL_W)/2 + wx * 75 / 100;\n"
"    *sy = WIN_H/2           - wy * 75 / 100;\n"
"}\n\n"

"/* Proyeccion lateral: r=sqrt(x^2+y^2) vs Z */\n"
"static void proj_lado(int wx, int wy, int wz, int *sx, int *sy) {\n"
"    int r = (int)sqrt((double)wx*(double)wx + (double)wy*(double)wy);\n"
"    *sx = (WIN_W-PANEL_W)/2 + r  * 75 / 100;\n"
"    *sy = WIN_H/2           - wz * 150 / 100;\n"
"}\n\n"

"/* Despachador: selecciona la proyeccion activa y suma el offset del panel */\n"
"static void proj(int wx, int wy, int wz, int *sx, int *sy) {\n"
"    if      (modo_vista == 1) proj_top (wx, wy, wz, sx, sy);\n"
"    else if (modo_vista == 2) proj_lado(wx, wy, wz, sx, sy);\n"
"    else                      proj_iso (wx, wy, wz, sx, sy);\n"
"    *sx += PANEL_W;\n"
"}\n\n"

/* Cinematica inversa */
"/* Cinematica inversa: codo abajo */\n"
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
"    double k1 = L1 + L2*cos(q2), k2 = L2*sin(q2);\n"
"    double q1 = atan2(ey,ex) - atan2(k2,k1);\n"
"    *cx = L1*cos(q1); *cy = L1*sin(q1);\n"
"    return 1;\n"
"}\n\n"

/* Circulo relleno */
"static void draw_circle(SDL_Renderer *r, int cx, int cy, int rad) {\n"
"    for (int w=0; w<rad*2; w++)\n"
"        for (int h=0; h<rad*2; h++) {\n"
"            int dx=rad-w, dy=rad-h;\n"
"            if (dx*dx+dy*dy <= rad*rad)\n"
"                SDL_RenderDrawPoint(r, cx+dx, cy+dy);\n"
"        }\n"
"}\n\n"

/* Arco de circulo para workspace en vista superior */
"/* Arco de circulo aproximado con segmentos (para el workspace en vista TOP) */\n"
"static void draw_arc(SDL_Renderer *r, int ox, int oy, int radio_mm,\n"
"                     int escala_num, int escala_den) {\n"
"    int px0 = ox + radio_mm * escala_num / escala_den;\n"
"    int py0 = oy;\n"
"    for (int deg = 5; deg <= 360; deg += 5) {\n"
"        double rad = deg * 3.14159265 / 180.0;\n"
"        int px1 = ox + (int)(radio_mm * cos(rad)) * escala_num / escala_den;\n"
"        int py1 = oy - (int)(radio_mm * sin(rad)) * escala_num / escala_den;\n"
"        SDL_RenderDrawLine(r, px0, py0, px1, py1);\n"
"        px0 = px1; py0 = py1;\n"
"    }\n"
"}\n\n"

/* draw_text */
"static void draw_text(SDL_Renderer *r, TTF_Font *font,\n"
"                      const char *texto, int x, int y,\n"
"                      Uint8 R, Uint8 G, Uint8 B, Uint8 A) {\n"
"    SDL_Color color = {R, G, B, A};\n"
"    SDL_Surface *surf = TTF_RenderText_Blended(font, texto, color);\n"
"    if (!surf) return;\n"
"    SDL_Texture *tex = SDL_CreateTextureFromSurface(r, surf);\n"
"    SDL_FreeSurface(surf);\n"
"    if (!tex) return;\n"
"    int tw, th;\n"
"    SDL_QueryTexture(tex, NULL, NULL, &tw, &th);\n"
"    SDL_Rect dst = {x, y, tw, th};\n"
"    SDL_RenderCopy(r, tex, NULL, &dst);\n"
"    SDL_DestroyTexture(tex);\n"
"}\n\n"

/* draw_hud */
"static void draw_hud(SDL_Renderer *r, TTF_Font *font, TTF_Font *font_bold,\n"
"                     int wx, int wy, int wz, int pinza, int vel,\n"
"                     int idx, int total) {\n"
"    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);\n"
"    SDL_SetRenderDrawColor(r, 10, 10, 20, 210);\n"
"    SDL_Rect panel = {0, 0, PANEL_W, WIN_H};\n"
"    SDL_RenderFillRect(r, &panel);\n"
"    SDL_SetRenderDrawColor(r, 60, 80, 120, 255);\n"
"    SDL_RenderDrawLine(r, PANEL_W, 0, PANEL_W, WIN_H);\n"
"    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);\n\n"

"    draw_text(r, font_bold, \"SCARA VM\", 12, 12, 100, 180, 255, 255);\n"
"    draw_text(r, font, \"Simulacion de trayectoria\", 12, 32, 140, 140, 160, 255);\n"
"    SDL_SetRenderDrawColor(r, 50, 70, 110, 255);\n"
"    SDL_RenderDrawLine(r, 8, 56, PANEL_W-8, 56);\n\n"

"    char buf[64];\n"
"    draw_text(r, font_bold, \"POSICION\", 12, 64, 180, 180, 200, 255);\n"
"    snprintf(buf, sizeof(buf), \"X : %d mm\", wx);\n"
"    draw_text(r, font, buf, 12, 84, 220, 220, 255, 255);\n"
"    snprintf(buf, sizeof(buf), \"Y : %d mm\", wy);\n"
"    draw_text(r, font, buf, 12, 104, 220, 220, 255, 255);\n"
"    snprintf(buf, sizeof(buf), \"Z : %d mm\", wz);\n"
"    draw_text(r, font, buf, 12, 124, 220, 220, 255, 255);\n"
"    SDL_SetRenderDrawColor(r, 50, 70, 110, 255);\n"
"    SDL_RenderDrawLine(r, 8, 148, PANEL_W-8, 148);\n\n"

/* Cinematica inversa — calculo limpio sin ternarios anidados */
"    double ck_hud=0, cy_hud=0;\n"
"    ik_codo((double)wx, (double)wy, &ck_hud, &cy_hud);\n"
"    double r2v = (double)wx*(double)wx + (double)wy*(double)wy;\n"
"    double c2v = (r2v - L1*L1 - L2*L2) / (2.0*L1*L2);\n"
"    if (c2v < -1.0) c2v = -1.0;\n"
"    if (c2v >  1.0) c2v =  1.0;\n"
"    double q2r = acos(c2v);\n"
"    double k1h = L1 + L2*cos(q2r), k2h = L2*sin(q2r);\n"
"    int q1_deg = (int)round((atan2((double)wy,(double)wx) - atan2(k2h,k1h)) * 180.0/M_PI);\n"
"    int q2_deg = (int)round(q2r * 180.0/M_PI);\n"
"    draw_text(r, font_bold, \"CINEMATICA\", 12, 156, 180, 180, 200, 255);\n"
"    snprintf(buf, sizeof(buf), \"q1: %d deg\", q1_deg);\n"
"    draw_text(r, font, buf, 12, 176, 180, 220, 180, 255);\n"
"    snprintf(buf, sizeof(buf), \"q2: %d deg\", q2_deg);\n"
"    draw_text(r, font, buf, 12, 196, 180, 220, 180, 255);\n"
"    SDL_SetRenderDrawColor(r, 50, 70, 110, 255);\n"
"    SDL_RenderDrawLine(r, 8, 220, PANEL_W-8, 220);\n\n"

"    draw_text(r, font_bold, \"VELOCIDAD\", 12, 228, 180, 180, 200, 255);\n"
"    snprintf(buf, sizeof(buf), \"%d %%\", vel);\n"
"    draw_text(r, font, buf, 12, 248, 255, 180, 50, 255);\n"
"    SDL_SetRenderDrawColor(r, 40, 40, 60, 255);\n"
"    SDL_Rect vel_bg = {12, 268, PANEL_W-24, 10};\n"
"    SDL_RenderFillRect(r, &vel_bg);\n"
"    SDL_SetRenderDrawColor(r, 255, 160, 40, 255);\n"
"    SDL_Rect vel_fg = {12, 268, (PANEL_W-24)*vel/100, 10};\n"
"    SDL_RenderFillRect(r, &vel_fg);\n"
"    SDL_SetRenderDrawColor(r, 50, 70, 110, 255);\n"
"    SDL_RenderDrawLine(r, 8, 288, PANEL_W-8, 288);\n\n"

"    draw_text(r, font_bold, \"PINZA\", 12, 296, 180, 180, 200, 255);\n"
"    if (pinza) {\n"
"        SDL_SetRenderDrawColor(r, 30, 200, 80, 255);\n"
"        SDL_Rect ind = {12, 316, 14, 14}; SDL_RenderFillRect(r, &ind);\n"
"        draw_text(r, font, \"ABIERTA\", 34, 316, 30, 220, 80, 255);\n"
"    } else {\n"
"        SDL_SetRenderDrawColor(r, 220, 50, 50, 255);\n"
"        SDL_Rect ind = {12, 316, 14, 14}; SDL_RenderFillRect(r, &ind);\n"
"        draw_text(r, font, \"CERRADA\", 34, 316, 220, 50, 50, 255);\n"
"    }\n"
"    SDL_SetRenderDrawColor(r, 50, 70, 110, 255);\n"
"    SDL_RenderDrawLine(r, 8, 342, PANEL_W-8, 342);\n\n"

"    draw_text(r, font_bold, \"PROGRESO\", 12, 350, 180, 180, 200, 255);\n"
"    snprintf(buf, sizeof(buf), \"Paso %d / %d\", idx+1, total);\n"
"    draw_text(r, font, buf, 12, 370, 160, 160, 190, 255);\n"
"    SDL_SetRenderDrawColor(r, 40, 40, 60, 255);\n"
"    SDL_Rect prg_bg = {12, 392, PANEL_W-24, 10};\n"
"    SDL_RenderFillRect(r, &prg_bg);\n"
"    SDL_SetRenderDrawColor(r, 70, 130, 220, 255);\n"
"    int pw = (total > 1) ? (PANEL_W-24)*(idx)/(total-1) : PANEL_W-24;\n"
"    SDL_Rect prg_fg = {12, 392, pw, 10};\n"
"    SDL_RenderFillRect(r, &prg_fg);\n"
"    SDL_SetRenderDrawColor(r, 50, 70, 110, 255);\n"
"    SDL_RenderDrawLine(r, 8, 416, PANEL_W-8, 416);\n\n"

/* Selector de vista */
"    draw_text(r, font_bold, \"VISTA\", 12, 424, 180, 180, 200, 255);\n"
"    {\n"
"        const char *etiq[3] = {\"ISO\",\"TOP\",\"LADO\"};\n"
"        const char *nombres[3] = {\"Isometrica\",\"Superior\",\"Lateral\"};\n"
"        for (int v=0; v<3; v++) {\n"
"            int bx = 12 + v*56;\n"
"            if (v == modo_vista) {\n"
"                SDL_SetRenderDrawColor(r, 70, 130, 220, 255);\n"
"                SDL_Rect btn = {bx, 444, 50, 18}; SDL_RenderFillRect(r, &btn);\n"
"                draw_text(r, font, etiq[v], bx+6, 446, 255, 255, 255, 255);\n"
"            } else {\n"
"                SDL_SetRenderDrawColor(r, 40, 40, 60, 255);\n"
"                SDL_Rect btn = {bx, 444, 50, 18}; SDL_RenderFillRect(r, &btn);\n"
"                draw_text(r, font, etiq[v], bx+6, 446, 100, 100, 130, 255);\n"
"            }\n"
"        }\n"
"        draw_text(r, font, nombres[modo_vista], 12, 468, 140, 200, 255, 255);\n"
"    }\n\n"

"    draw_text(r, font, \"TAB: ocultar panel\", 12, WIN_H-48, 80, 80, 100, 255);\n"
"    draw_text(r, font, \"V: cambiar vista\",   12, WIN_H-28, 80, 80, 100, 255);\n"
"}\n\n"

/* main del _vis.c */
"int main(int argc, char *argv[]) {\n"
"    (void)argc; (void)argv;\n"
"    SDL_SetMainReady();\n"
"    if (SDL_Init(SDL_INIT_VIDEO) != 0) {\n"
"        fprintf(stderr, \"SDL_Init: %%s\\n\", SDL_GetError());\n"
"        return 1;\n"
"    }\n"
"    if (TTF_Init() != 0) {\n"
"        fprintf(stderr, \"TTF_Init: %%s\\n\", TTF_GetError());\n"
"        SDL_Quit(); return 1;\n"
"    }\n"
"    TTF_Font *font      = TTF_OpenFont(\"arial.ttf\", FONT_SIZE);\n"
"    TTF_Font *font_bold = TTF_OpenFont(\"arial.ttf\", FONT_SIZE);\n"
"    if (!font || !font_bold) {\n"
"        fprintf(stderr, \"No se encontro arial.ttf: %%s\\n\", TTF_GetError());\n"
"        TTF_Quit(); SDL_Quit(); return 1;\n"
"    }\n"
"    TTF_SetFontStyle(font_bold, TTF_STYLE_BOLD);\n"
"    SDL_Window *win = SDL_CreateWindow(\n"
"        \"SCARA — Compilador de Lenguaje Robotico\",\n"
"        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,\n"
"        WIN_W, WIN_H, SDL_WINDOW_SHOWN);\n"
"    SDL_Renderer *ren = SDL_CreateRenderer(win, -1,\n"
"        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);\n\n"

"    int total       = traza_get_len();\n"
"    if (total <= 0 || total > 100000) { fprintf(stderr, \"Error: traza invalida (len=%d)\\n\", total); return 1; }\n"
"    int idx         = 0;\n"
"    int running     = 1;\n"
"    int hud_visible = 1;\n"
"    int paused      = 0;\n"
"    SDL_Event ev;\n\n"

"    while (running) {\n"
"        while (SDL_PollEvent(&ev)) {\n"
"            if (ev.type == SDL_QUIT) running = 0;\n"
"            if (ev.type == SDL_KEYDOWN) {\n"
"                switch (ev.key.keysym.sym) {\n"
"                    case SDLK_TAB:    hud_visible = !hud_visible; break;\n"
"                    case SDLK_SPACE:  paused      = !paused;      break;\n"
"                    case SDLK_ESCAPE: running     = 0;            break;\n"
"                    case SDLK_v:\n"
"                        modo_vista = (modo_vista + 1) %% 3;\n"
"                        idx    = 0;\n"
"                        paused = 0;\n"
"                        total  = traza_get_len();\n"
"                        break;\n"
"                    default: break;\n"
"                }\n"
"            }\n"
"        }\n\n"

"        if (idx >= total) { SDL_Delay(2000); break; }\n\n"

"        int wx    = traza_get_x(idx);\n"
"        int wy    = traza_get_y(idx);\n"
"        int wz    = traza_get_z(idx);\n"
"        int pinza = traza_get_pinza(idx);\n"
"        int vel   = traza_get_vel(idx);\n"
"        if (vel < 0) vel = 0; if (vel > 100) vel = 100;\n\n"

"        SDL_SetRenderDrawColor(ren, 15, 15, 25, 255);\n"
"        SDL_RenderClear(ren);\n\n"

/* Cuadricula segun vista */
"        SDL_SetRenderDrawColor(ren, 30, 30, 50, 255);\n"
"        if (modo_vista == 0) {\n"
"            for (int g = -350; g <= 350; g += 50) {\n"
"                int ax,ay,bx,by;\n"
"                proj_iso(g,-350,0,&ax,&ay); ax+=PANEL_W;\n"
"                proj_iso(g, 350,0,&bx,&by); bx+=PANEL_W;\n"
"                SDL_RenderDrawLine(ren,ax,ay,bx,by);\n"
"                proj_iso(-350,g,0,&ax,&ay); ax+=PANEL_W;\n"
"                proj_iso( 350,g,0,&bx,&by); bx+=PANEL_W;\n"
"                SDL_RenderDrawLine(ren,ax,ay,bx,by);\n"
"            }\n"
"        } else if (modo_vista == 1) {\n"
"            for (int g = -350; g <= 350; g += 50) {\n"
"                int ax,ay,bx,by;\n"
"                proj_top(g,-350,0,&ax,&ay); ax+=PANEL_W;\n"
"                proj_top(g, 350,0,&bx,&by); bx+=PANEL_W;\n"
"                SDL_RenderDrawLine(ren,ax,ay,bx,by);\n"
"                proj_top(-350,g,0,&ax,&ay); ax+=PANEL_W;\n"
"                proj_top( 350,g,0,&bx,&by); bx+=PANEL_W;\n"
"                SDL_RenderDrawLine(ren,ax,ay,bx,by);\n"
"            }\n"
"            int ox,oy;\n"
"            proj_top(0,0,0,&ox,&oy); ox+=PANEL_W;\n"
"            SDL_SetRenderDrawColor(ren, 60, 60, 30, 255);\n"
"            draw_arc(ren, ox, oy,  50, 75, 100);\n"
"            draw_arc(ren, ox, oy, 350, 75, 100);\n"
"            int ax2,ay2,bx2,by2;\n"
"            proj_top(350,0,0,&ax2,&ay2);  ax2+=PANEL_W;\n"
"            proj_top(-350,0,0,&bx2,&by2); bx2+=PANEL_W;\n"
"            SDL_SetRenderDrawColor(ren, 80, 40, 40, 255);\n"
"            SDL_RenderDrawLine(ren,ax2,ay2,bx2,by2);\n"
"            proj_top(0,350,0,&ax2,&ay2);  ax2+=PANEL_W;\n"
"            proj_top(0,-350,0,&bx2,&by2); bx2+=PANEL_W;\n"
"            SDL_SetRenderDrawColor(ren, 40, 80, 40, 255);\n"
"            SDL_RenderDrawLine(ren,ax2,ay2,bx2,by2);\n"
"        } else {\n"
"            for (int g = 0; g <= 350; g += 50) {\n"
"                int ax,ay,bx,by;\n"
"                proj_lado(g,0,  0,&ax,&ay); ax+=PANEL_W;\n"
"                proj_lado(g,0,350,&bx,&by); bx+=PANEL_W;\n"
"                SDL_RenderDrawLine(ren,ax,ay,bx,by);\n"
"                proj_lado(  0,0,g,&ax,&ay); ax+=PANEL_W;\n"
"                proj_lado(350,0,g,&bx,&by); bx+=PANEL_W;\n"
"                SDL_RenderDrawLine(ren,ax,ay,bx,by);\n"
"            }\n"
"            int ax3,ay3,bx3,by3;\n"
"            proj_lado(0,0,0,&ax3,&ay3);   ax3+=PANEL_W;\n"
"            proj_lado(350,0,0,&bx3,&by3); bx3+=PANEL_W;\n"
"            SDL_SetRenderDrawColor(ren, 60, 80, 60, 255);\n"
"            SDL_RenderDrawLine(ren,ax3,ay3,bx3,by3);\n"
"        }\n\n"

/* Brazo */
"        double ck_x=0, ck_y=0;\n"
"        ik_codo((double)wx,(double)wy,&ck_x,&ck_y);\n"
"        int cx_w=(int)round(ck_x), cy_w=(int)round(ck_y);\n\n"

"        int bsx,bsy,csx,csy,esx,esy;\n"
"        proj(0,    0,    0,   &bsx,&bsy);\n"
"        proj(cx_w, cy_w, wz,  &csx,&csy);\n"
"        proj(wx,   wy,   wz,  &esx,&esy);\n\n"

"        SDL_SetRenderDrawColor(ren, 70, 130, 220, 255);\n"
"        for (int t=-2;t<=2;t++) {\n"
"            SDL_RenderDrawLine(ren,bsx+t,bsy,csx+t,csy);\n"
"            SDL_RenderDrawLine(ren,bsx,bsy+t,csx,csy+t);\n"
"        }\n"
"        SDL_SetRenderDrawColor(ren, 180, 100, 40, 255);\n"
"        for (int t=-2;t<=2;t++) {\n"
"            SDL_RenderDrawLine(ren,csx+t,csy,esx+t,esy);\n"
"            SDL_RenderDrawLine(ren,csx,csy+t,esx,esy+t);\n"
"        }\n"
"        SDL_SetRenderDrawColor(ren, 200, 200, 200, 255);\n"
"        draw_circle(ren,bsx,bsy,8);\n"
"        SDL_SetRenderDrawColor(ren, 100, 200, 100, 255);\n"
"        draw_circle(ren,csx,csy,6);\n"
"        if (pinza) SDL_SetRenderDrawColor(ren, 255, 220, 50, 255);\n"
"        else       SDL_SetRenderDrawColor(ren, 255,  60, 60, 255);\n"
"        draw_circle(ren,esx,esy,9);\n\n"

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

/* Etiqueta de vista activa */
"        {\n"
"            const char *label[3] = {\"VISTA: ISOMETRICA\",\"VISTA: SUPERIOR\",\"VISTA: LATERAL\"};\n"
"            draw_text(ren, font_bold, label[modo_vista],\n"
"                      WIN_W - 190, 12, 100, 180, 255, 200);\n"
"        }\n\n"

/* Barra de progreso inferior */
"        SDL_SetRenderDrawColor(ren, 30, 30, 50, 255);\n"
"        SDL_Rect bar_bg = {0, WIN_H-6, WIN_W, 6};\n"
"        SDL_RenderFillRect(ren, &bar_bg);\n"
"        SDL_SetRenderDrawColor(ren, 70, 130, 220, 255);\n"
"        int bw = (total>1) ? WIN_W*idx/(total-1) : WIN_W;\n"
"        SDL_Rect bar_fg = {0, WIN_H-6, bw, 6};\n"
"        SDL_RenderFillRect(ren, &bar_fg);\n\n"

/* HUD */
"        if (hud_visible) {\n"
"            draw_hud(ren, font, font_bold, wx, wy, wz, pinza, vel, idx, total);\n"
"        } else {\n"
"            draw_text(ren, font, \"TAB: mostrar panel\",\n"
"                      8, WIN_H-48, 80, 80, 100, 255);\n"
"            draw_text(ren, font, \"V: cambiar vista\",\n"
"                      8, WIN_H-28, 80, 80, 100, 255);\n"
"        }\n\n"

"        SDL_RenderPresent(ren);\n\n"

"        if (!paused) {\n"
"            int delay = 220 - vel*180/100;\n"
"            if (delay < 35) delay = 35;\n"
"            SDL_Delay(delay);\n"
"            idx++;\n"
"        } else {\n"
"            SDL_Delay(16);\n"
"        }\n"
"    }\n\n"

"    TTF_CloseFont(font);\n"
"    TTF_CloseFont(font_bold);\n"
"    TTF_Quit();\n"
"    SDL_DestroyRenderer(ren);\n"
"    SDL_DestroyWindow(win);\n"
"    SDL_Quit();\n"
"    return 0;\n"
"}\n"
    );
    fclose(fvis);
    printf("[GEN] Visualizador SDL2+TTF: %s\n", ruta_vis);

    // ── 7. Compilar .asm a .obj con NASM ──
    char ruta_obj[512];
    snprintf(ruta_obj, sizeof(ruta_obj), "%.*s.obj", (int)(strlen(ruta_asm)-4), ruta_asm);
    
    char cmd_nasm[1024];
    snprintf(cmd_nasm, sizeof(cmd_nasm), "nasm -f win64 \"%s\" -o \"%s\"", ruta_asm, ruta_obj);
    printf("\n=== COMPILANDO CON NASM: %s ===\n", cmd_nasm);
    if (system(cmd_nasm) != 0) {
        fprintf(stderr, "Error: NASM fallo\n");
        free(programa); return 1;
    }

    // ── 8. Enlazar _vis.c + .obj con gcc ──
    char cmd_ld[1024];
    snprintf(cmd_ld, sizeof(cmd_ld),
             "gcc -O2 \"%s\" \"%s\" -o \"%s\" "
             "-I\"C:/msys64/mingw64/include\" "
             "-I\"C:/msys64/mingw64/include/SDL2\" "
             "-L\"C:/msys64/mingw64/lib\" "
             "-lSDL2 -lSDL2main -lSDL2_ttf -lmingw32 -lm "
             "-mwindows",
             ruta_vis, ruta_obj, ruta_exe);
    printf("\n=== ENLAZANDO: %s ===\n", cmd_ld);
    if (system(cmd_ld) != 0) {
        fprintf(stderr, "Error: enlazado fallo\n");
        free(programa); return 1;
    }

    // ── 9. Ejecutar el binario generado ──
    printf("\n=== EJECUTANDO: %s ===\n", ruta_exe);
    char cmd_run[512];
    snprintf(cmd_run, sizeof(cmd_run), "\"%s\"", ruta_exe);
    system(cmd_run);

    free(programa);
    return 0;
}