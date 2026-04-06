#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <SDL2/SDL.h>
#include "lexer.h"
#include "parser.h"
#include "simbolos.h"
#include "errores.h"
#include "vm.h"
#include "cinematica.h"

#define VIS_W 960
#define VIS_H 640

typedef struct {
    double zoom;
    int pan_x;
    int pan_y;
} VisCam;

static void proyectar_iso(int x, int y, int z, const VisCam* cam, int* sx, int* sy) {
    const double escala = 1.4 * cam->zoom;
    const int ox = VIS_W / 2;
    const int oy = (VIS_H * 3) / 4;
    double dx = ((double)x - (double)y) * 0.90;
    double dy = ((double)x + (double)y) * 0.45 - (double)z * 1.20;
    *sx = ox + cam->pan_x + (int)lround(dx * escala);
    *sy = oy + cam->pan_y - (int)lround(dy * escala);
}

static void dibujar_circulo(SDL_Renderer* r, int cx, int cy, int radio) {
    for (int dy = -radio; dy <= radio; dy++) {
        for (int dx = -radio; dx <= radio; dx++) {
            if ((dx * dx + dy * dy) <= (radio * radio)) {
                SDL_RenderDrawPoint(r, cx + dx, cy + dy);
            }
        }
    }
}

static void dibujar_grid_base(SDL_Renderer* renderer, const VisCam* cam) {
    SDL_SetRenderDrawColor(renderer, 230, 230, 230, 255);
    for (int x = -350; x <= 350; x += 50) {
        int x1, y1, x2, y2;
        proyectar_iso(x, -350, 0, cam, &x1, &y1);
        proyectar_iso(x, 350, 0, cam, &x2, &y2);
        SDL_RenderDrawLine(renderer, x1, y1, x2, y2);
    }
    for (int y = -350; y <= 350; y += 50) {
        int x1, y1, x2, y2;
        proyectar_iso(-350, y, 0, cam, &x1, &y1);
        proyectar_iso(350, y, 0, cam, &x2, &y2);
        SDL_RenderDrawLine(renderer, x1, y1, x2, y2);
    }
}

static void dibujar_hud(SDL_Renderer* renderer,
                        const VmEstado* traza,
                        int total,
                        int idx_actual,
                        int en_pausa) {
    int vel = traza[idx_actual].velocidad;
    int progreso_w = (total > 1) ? (700 * idx_actual) / (total - 1) : 700;
    int vel_w = (vel * 180) / 100;

    SDL_Rect panel = { 20, 20, 260, 110 };
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 220);
    SDL_RenderFillRect(renderer, &panel);
    SDL_SetRenderDrawColor(renderer, 70, 70, 70, 255);
    SDL_RenderDrawRect(renderer, &panel);

    SDL_Rect progreso_bg = { 20, VIS_H - 36, 700, 14 };
    SDL_Rect progreso_fg = { 20, VIS_H - 36, progreso_w, 14 };
    SDL_SetRenderDrawColor(renderer, 210, 210, 210, 255);
    SDL_RenderFillRect(renderer, &progreso_bg);
    SDL_SetRenderDrawColor(renderer, 50, 120, 220, 255);
    SDL_RenderFillRect(renderer, &progreso_fg);

    SDL_Rect vel_bg = { 40, 56, 180, 12 };
    SDL_Rect vel_fg = { 40, 56, vel_w, 12 };
    SDL_SetRenderDrawColor(renderer, 220, 220, 220, 255);
    SDL_RenderFillRect(renderer, &vel_bg);
    SDL_SetRenderDrawColor(renderer, 245, 140, 35, 255);
    SDL_RenderFillRect(renderer, &vel_fg);

    SDL_Rect pinza = { 40, 82, 22, 22 };
    if (traza[idx_actual].pinza_abierta) {
        SDL_SetRenderDrawColor(renderer, 30, 170, 60, 255);
    } else {
        SDL_SetRenderDrawColor(renderer, 200, 40, 40, 255);
    }
    SDL_RenderFillRect(renderer, &pinza);
    SDL_SetRenderDrawColor(renderer, 60, 60, 60, 255);
    SDL_RenderDrawRect(renderer, &pinza);

    if (en_pausa) {
        SDL_Rect p1 = { 240, 45, 8, 30 };
        SDL_Rect p2 = { 252, 45, 8, 30 };
        SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);
        SDL_RenderFillRect(renderer, &p1);
        SDL_RenderFillRect(renderer, &p2);
    } else {
        SDL_Point tri[4] = { {240, 45}, {240, 75}, {262, 60}, {240, 45} };
        SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);
        SDL_RenderDrawLines(renderer, tri, 4);
    }
}

static Uint32 vis_delay_ms(const VmEstado* a, const VmEstado* b) {
    int v = (a->velocidad + b->velocidad) / 2;
    Uint32 base = (Uint32)(220 - (v * 180) / 100);
    if (base < 35) base = 35;
    if (base > 240) base = 240;

    if (a->x == b->x && a->y == b->y && a->z == b->z) {
        return 200;
    }
    return base;
}

static void dibujar_escena(SDL_Renderer* renderer,
                           const VmEstado* traza,
                           int total,
                           int idx_actual,
                           const VisCam* cam,
                           int en_pausa) {
    const double L1 = 200.0;
    const double L2 = 150.0;

    int sx0 = 0, sy0 = 0;
    int sx1 = 0, sy1 = 0;
    int sx2 = 0, sy2 = 0;
    int q1 = 0, q2 = 0;
    int x = traza[idx_actual].x;
    int y = traza[idx_actual].y;
    int z = traza[idx_actual].z;

    if (!cinematica_ik_xy_modo(x, y, CIN_MODO_CODO_ABAJO, &q1, &q2)) {
        q1 = 0;
        q2 = 0;
    }

    double q1r = ((double)q1 * 3.14159265358979323846) / 180.0;
    double q2r = ((double)q2 * 3.14159265358979323846) / 180.0;
    int ex = (int)lround(L1 * cos(q1r));
    int ey = (int)lround(L1 * sin(q1r));
    int wx = (int)lround(L1 * cos(q1r) + L2 * cos(q1r + q2r));
    int wy = (int)lround(L1 * sin(q1r) + L2 * sin(q1r + q2r));

    (void)wx;
    (void)wy;

    proyectar_iso(0, 0, 0, cam, &sx0, &sy0);
    proyectar_iso(ex, ey, z, cam, &sx1, &sy1);
    proyectar_iso(x, y, z, cam, &sx2, &sy2);

    SDL_SetRenderDrawColor(renderer, 248, 246, 240, 255);
    SDL_RenderClear(renderer);

    dibujar_grid_base(renderer, cam);

    {
        int ax1, ay1, ax2, ay2;
        int bx1, by1, bx2, by2;
        int cx1, cy1, cx2, cy2;
        proyectar_iso(0, 0, 0, cam, &ax1, &ay1);
        proyectar_iso(120, 0, 0, cam, &ax2, &ay2);
        proyectar_iso(0, 0, 0, cam, &bx1, &by1);
        proyectar_iso(0, 120, 0, cam, &bx2, &by2);
        proyectar_iso(0, 0, 0, cam, &cx1, &cy1);
        proyectar_iso(0, 0, 120, cam, &cx2, &cy2);
        SDL_SetRenderDrawColor(renderer, 200, 40, 40, 255);
        SDL_RenderDrawLine(renderer, ax1, ay1, ax2, ay2);
        SDL_SetRenderDrawColor(renderer, 40, 170, 70, 255);
        SDL_RenderDrawLine(renderer, bx1, by1, bx2, by2);
        SDL_SetRenderDrawColor(renderer, 40, 80, 210, 255);
        SDL_RenderDrawLine(renderer, cx1, cy1, cx2, cy2);
    }

    SDL_SetRenderDrawColor(renderer, 120, 120, 120, 255);
    for (int i = 1; i <= idx_actual && i < total; i++) {
        int px1, py1, px2, py2;
        proyectar_iso(traza[i - 1].x, traza[i - 1].y, traza[i - 1].z, cam, &px1, &py1);
        proyectar_iso(traza[i].x, traza[i].y, traza[i].z, cam, &px2, &py2);
        SDL_RenderDrawLine(renderer, px1, py1, px2, py2);
    }

    SDL_SetRenderDrawColor(renderer, 30, 80, 190, 255);
    SDL_RenderDrawLine(renderer, sx0, sy0, sx1, sy1);
    SDL_SetRenderDrawColor(renderer, 30, 150, 90, 255);
    SDL_RenderDrawLine(renderer, sx1, sy1, sx2, sy2);

    SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);
    dibujar_circulo(renderer, sx0, sy0, 6);
    dibujar_circulo(renderer, sx1, sy1, 5);

    if (traza[idx_actual].pinza_abierta) {
        SDL_SetRenderDrawColor(renderer, 20, 160, 20, 255);
    } else {
        SDL_SetRenderDrawColor(renderer, 190, 30, 30, 255);
    }
    dibujar_circulo(renderer, sx2, sy2, 7);

    dibujar_hud(renderer, traza, total, idx_actual, en_pausa);
    SDL_RenderPresent(renderer);
}

static int visualizar_trayectoria_sdl(const VmEstado* traza, int n, int mantener_abierta) {
    if (!traza || n <= 0) {
        return 0;
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL ERROR: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "SCARA VM - FASE 5 (Visualizacion 2.5D)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        VIS_W, VIS_H, SDL_WINDOW_SHOWN
    );
    if (!window) {
        fprintf(stderr, "SDL ERROR (window): %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        fprintf(stderr, "SDL ERROR (renderer): %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    int running = 1;
    int idx = 0;
    int en_pausa = 0;
    VisCam cam;
    cam.zoom = 1.0;
    cam.pan_x = 0;
    cam.pan_y = 0;
    Uint32 last_step = SDL_GetTicks();
    Uint32 fin_anim = (n <= 1) ? SDL_GetTicks() : 0;

    printf("[VIS] Controles: Flechas mover camara | +/- zoom | ESPACIO pausa | R reset camara\n");

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                running = 0;
            } else if (e.type == SDL_KEYDOWN) {
                switch (e.key.keysym.sym) {
                    case SDLK_SPACE:
                        en_pausa = !en_pausa;
                        break;
                    case SDLK_UP:
                        cam.pan_y += 20;
                        break;
                    case SDLK_DOWN:
                        cam.pan_y -= 20;
                        break;
                    case SDLK_LEFT:
                        cam.pan_x += 20;
                        break;
                    case SDLK_RIGHT:
                        cam.pan_x -= 20;
                        break;
                    case SDLK_EQUALS:
                    case SDLK_PLUS:
                    case SDLK_KP_PLUS:
                        cam.zoom += 0.10;
                        if (cam.zoom > 2.4) cam.zoom = 2.4;
                        break;
                    case SDLK_MINUS:
                    case SDLK_KP_MINUS:
                        cam.zoom -= 0.10;
                        if (cam.zoom < 0.45) cam.zoom = 0.45;
                        break;
                    case SDLK_r:
                        cam.zoom = 1.0;
                        cam.pan_x = 0;
                        cam.pan_y = 0;
                        break;
                    default:
                        break;
                }
            }
        }

        Uint32 ahora = SDL_GetTicks();
        if (idx == (n - 1) && fin_anim == 0) {
            fin_anim = ahora;
        }

        if (!en_pausa && idx < (n - 1) && (ahora - last_step) >= vis_delay_ms(&traza[idx], &traza[idx + 1])) {
            idx++;
            last_step = ahora;
            if (idx == n - 1) {
                fin_anim = ahora;
            }
        } else if (!mantener_abierta && idx == (n - 1) && fin_anim > 0 && (ahora - fin_anim) >= 1500) {
            running = 0;
        }

        dibujar_escena(renderer, traza, n, idx, &cam, en_pausa);
        SDL_Delay(8);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

// --- Leer archivo completo a string ----------------------

char* leer_archivo(const char* ruta) {
    FILE* f = fopen(ruta, "rb");
    if (!f) {
        fprintf(stderr, "Error: no se puede abrir '%s'\n", ruta);
        exit(1);
    }

    fseek(f, 0, SEEK_END);
    long tam = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* contenido = (char*)malloc(tam + 1);
    if (!contenido) {
        fprintf(stderr, "Error: memoria insuficiente\n");
        exit(1);
    }
    size_t leidos = fread(contenido, 1, tam, f);
    contenido[leidos] = '\0';
    fclose(f);

    return contenido;
}

// --- Main ------------------------------------------------

static int ejecutar_ik_selftest(void) {
    const int tol_deg = 2;
    int total = 0;
    int ok = 0;
    int fail = 0;
    const int modos[2] = { CIN_MODO_CODO_ARRIBA, CIN_MODO_CODO_ABAJO };
    const char* modo_txt[2] = { "UP", "DOWN" };

    printf("=== IK SELFTEST C vs ASM ===\n");
    for (int x = -350; x <= 350; x += 25) {
        for (int y = -350; y <= 350; y += 25) {
            if (!cinematica_xy_en_alcance(x, y)) continue;

            for (int m = 0; m < 2; m++) {
                int ok_c = 0, ok_asm = 0;
                int q1_c = 0, q2_c = 0;
                int q1_asm = 0, q2_asm = 0;

                total++;
                if (cinematica_comparar_c_vs_asm_modo(x, y, modos[m], tol_deg,
                                                      &ok_c, &ok_asm,
                                                      &q1_c, &q2_c,
                                                      &q1_asm, &q2_asm)) {
                    ok++;
                } else {
                    fail++;
                    printf("[IK MISMATCH %s] XY=(%d,%d) C=(%d,%d) ASM=(%d,%d) okC=%d okASM=%d\n",
                           modo_txt[m], x, y, q1_c, q2_c, q1_asm, q2_asm, ok_c, ok_asm);
                }
            }
        }
    }

    printf("[IK SELFTEST] total=%d ok=%d fail=%d\n", total, ok, fail);
    return (fail == 0) ? 0 : 1;
}

int main(int argc, char* argv[]) {
    int mantener_vis_abierta = 0;
    const char* ruta_programa = NULL;

    if (argc >= 2 && strcmp(argv[1], "--ik-selftest") == 0) {
        return ejecutar_ik_selftest();
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--vis-keep-open") == 0) {
            mantener_vis_abierta = 1;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Error: opcion no reconocida: %s\n", argv[i]);
            fprintf(stderr, "Uso: scara.exe [--vis-keep-open] <archivo.scara>\n");
            return 1;
        } else {
            ruta_programa = argv[i];
        }
    }

    // verificar argumento
    if (!ruta_programa) {
        fprintf(stderr, "Uso: scara.exe [--vis-keep-open] <archivo.scara>\n");
        fprintf(stderr, "Ejemplo: scara.exe --vis-keep-open programa.scara\n");
        return 1;
    }

    // leer archivo fuente
    char* programa = leer_archivo(ruta_programa);

    // inicializar sistema de errores con el codigo fuente
    error_init(programa);

    printf("=== COMPILANDO: %s ===\n", ruta_programa);

    // inicializar lexer
    Lexer lex;
    lexer_init(&lex, programa);

    // inicializar parser y compilar
    Parser parser;
    parser_init(&parser, &lex);
    parsear_programa(&parser);

    // mostrar tabla de simbolos
    printf("\n=== TABLA DE SIMBOLOS ===\n");
    tabla_imprimir();

    // mostrar bytecode generado
    printf("\n=== BYTECODE GENERADO ===\n");
        printf("%-4s %-14s %-6s %-6s %-6s %-5s %-14s %-14s %-14s\n",
            "PC", "OPCODE", "ARG1", "ARG2", "ARG3", "FLAGS", "SVAL", "SVAL2", "SVAL3");
        printf("-----------------------------------------------------------------------------------------\n");

    for (int i = 0; i < bytecode_len; i++) {
        Instruccion* ins = &bytecode[i];
         printf("%-4d %-14s %-6d %-6d %-6d %-5d %-14s %-14s %-14s\n",
             i, opcode_a_texto(ins->opcode),
               ins->arg1, ins->arg2, ins->arg3,
             ins->flags,
             ins->sval,
             ins->sval2,
             ins->sval3);
    }

        printf("\n=== EJECUCION VM (FASE 5) ===\n");
        if (vm_ejecutar(bytecode, bytecode_len, 1) != 0) {
            free(programa);
            return 1;
        }

        {
            int n_traza = 0;
            const VmEstado* traza = vm_obtener_traza_estados(&n_traza);
            printf("\n=== VISUALIZACION SDL2 (FASE 5) ===\n");
            if (visualizar_trayectoria_sdl(traza, n_traza, mantener_vis_abierta) != 0) {
                free(programa);
                return 1;
            }
        }

    free(programa);
    return 0;
}
