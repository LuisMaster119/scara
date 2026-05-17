/* Visualizador SDL2 + SDL2_ttf generado por el compilador SCARA */
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <math.h>

/* Funciones importadas del codigo ensamblador (compilado con NASM) */
extern int traza_get_len(void);
extern int traza_get_x(int idx);
extern int traza_get_y(int idx);
extern int traza_get_z(int idx);
extern int traza_get_pinza(int idx);
extern int traza_get_vel(int idx);

/* --- Traza generada por el compilador SCARA --- */
static const int TRAZA_LEN = 86;

static const int traza_x[86] = {
    0, 0, 37, 75, 112, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150,
    187, 225, 262, 300, 300, 300, 300, 300, 300, 300, 300, 300, 300, 300, 263, 225,
    188, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 187, 225, 262, 300,
    300, 300, 300, 300, 300, 300, 300, 300, 300, 300, 263, 225, 188, 150, 150, 150,
    150, 150, 150, 150, 150, 150, 150, 150, 187, 225, 262, 300, 300, 300, 300, 300,
    300, 300, 300, 300, 300, 300
};
static const int traza_y[86] = {
    0, 0, 50, 100, 150, 200, 200, 200, 200, 200, 200, 200, 200, 200, 200, 200,
    188, 175, 163, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 162, 175,
    187, 200, 200, 200, 200, 200, 200, 200, 200, 200, 200, 200, 188, 175, 163, 150,
    150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 162, 175, 187, 200, 200, 200,
    200, 200, 200, 200, 200, 200, 200, 200, 188, 175, 163, 150, 150, 150, 150, 150,
    150, 150, 150, 150, 150, 150
};
static const int traza_z[86] = {
    0, 0, 12, 25, 37, 50, 50, 50, 50, 50, 38, 25, 13, 0, 0, 50,
    50, 50, 50, 50, 50, 50, 50, 50, 38, 25, 13, 0, 0, 50, 50, 50,
    50, 50, 50, 50, 50, 50, 38, 25, 13, 0, 0, 50, 50, 50, 50, 50,
    50, 50, 50, 50, 38, 25, 13, 0, 0, 50, 50, 50, 50, 50, 50, 50,
    50, 50, 38, 25, 13, 0, 0, 50, 50, 50, 50, 50, 50, 50, 50, 50,
    38, 25, 13, 0, 0, 50
};
static const int traza_pinza[86] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 1, 1
};
static const int traza_vel[86] = {
    100, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70,
    70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70,
    70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70,
    70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70,
    70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70,
    70, 70, 70, 70, 70, 70
};
static const int TRAZA_KF_LEN = 26;

static const int traza_kf[26] = {
    0, 1, 2, 6, 14, 15, 16, 20, 28, 29, 30, 34, 42, 43, 44, 48,
    56, 57, 58, 62, 70, 71, 72, 76, 84, 85
};
static const int traza_kf_tipo[26] = {
    1, 8, 2, 4, 7, 5, 2, 4, 6, 5, 2, 4, 7, 5, 2, 4,
    6, 5, 2, 4, 7, 5, 2, 4, 6, 5
};

#define WIN_W    900
#define WIN_H    600
#define L1       200.0
#define L2       150.0
#define PANEL_W  200
#define FONT_SIZE 16

/* ================================================================
 *  SISTEMA DE VISTAS
 *  0 = Isometrica  (diagonal, vista por defecto)
 *  1 = Superior    (planta XY, Z ignorado)
 *  2 = Lateral     (perfil: distancia radial vs Z)
 * ================================================================ */
static int modo_vista = 0;

/* Proyeccion isometrica */
static void proj_iso(int wx, int wy, int wz, int *sx, int *sy) {
    *sx = (WIN_W-PANEL_W)/2 + (wx - wy) * 70 / 100;
    *sy = WIN_H/2 - ((wx + wy) * 32 / 100 - wz * 85 / 100);
}

/* Proyeccion superior: XY plano, Z ignorado */
static void proj_top(int wx, int wy, int wz, int *sx, int *sy) {
    (void)wz;
    *sx = (WIN_W-PANEL_W)/2 + wx * 75 / 100;
    *sy = WIN_H/2           - wy * 75 / 100;
}

/* Proyeccion lateral: r=sqrt(x^2+y^2) vs Z */
/* Origen (r=0,z=0) anclado abajo-izquierda del area util */
static void proj_lado(int wx, int wy, int wz, int *sx, int *sy) {
    int r = (int)sqrt((double)wx*(double)wx + (double)wy*(double)wy);
    int margen = 40;
    *sx = margen                  + r  * ((WIN_W-PANEL_W) - 2*margen) / 370;
    *sy = (WIN_H - margen)        - wz * (WIN_H - 2*margen)           / 370;
}

/* Despachador: selecciona la proyeccion activa y suma el offset del panel */
static void proj(int wx, int wy, int wz, int *sx, int *sy) {
    if      (modo_vista == 1) proj_top (wx, wy, wz, sx, sy);
    else if (modo_vista == 2) proj_lado(wx, wy, wz, sx, sy);
    else                      proj_iso (wx, wy, wz, sx, sy);
    *sx += PANEL_W;
}

/* Cinematica inversa: codo abajo */
static int ik_codo(double ex, double ey, double *cx, double *cy) {
    double r2 = ex*ex + ey*ey;
    double c2 = (r2 - L1*L1 - L2*L2) / (2.0*L1*L2);
    if (c2 < -1.0 || c2 > 1.0) {
        double d = sqrt(r2);
        *cx = ex * L1 / (d > 1.0 ? d : 1.0);
        *cy = ey * L1 / (d > 1.0 ? d : 1.0);
        return 0;
    }
    double q2 = acos(c2);
    double k1 = L1 + L2*cos(q2), k2 = L2*sin(q2);
    double q1 = atan2(ey,ex) - atan2(k2,k1);
    *cx = L1*cos(q1); *cy = L1*sin(q1);
    return 1;
}

static void draw_circle(SDL_Renderer *r, int cx, int cy, int rad) {
    for (int w=0; w<rad*2; w++)
        for (int h=0; h<rad*2; h++) {
            int dx=rad-w, dy=rad-h;
            if (dx*dx+dy*dy <= rad*rad)
                SDL_RenderDrawPoint(r, cx+dx, cy+dy);
        }
}

/* Arco de circulo aproximado con segmentos (para el workspace en vista TOP) */
static void draw_arc(SDL_Renderer *r, int ox, int oy, int radio_mm,
                     int escala_num, int escala_den) {
    int px0 = ox + radio_mm * escala_num / escala_den;
    int py0 = oy;
    for (int deg = 5; deg <= 360; deg += 5) {
        double rad = deg * 3.14159265 / 180.0;
        int px1 = ox + (int)(radio_mm * cos(rad)) * escala_num / escala_den;
        int py1 = oy - (int)(radio_mm * sin(rad)) * escala_num / escala_den;
        SDL_RenderDrawLine(r, px0, py0, px1, py1);
        px0 = px1; py0 = py1;
    }
}

static void draw_text(SDL_Renderer *r, TTF_Font *font,
                      const char *texto, int x, int y,
                      Uint8 R, Uint8 G, Uint8 B, Uint8 A) {
    SDL_Color color = {R, G, B, A};
    SDL_Surface *surf = TTF_RenderText_Blended(font, texto, color);
    if (!surf) return;
    SDL_Texture *tex = SDL_CreateTextureFromSurface(r, surf);
    SDL_FreeSurface(surf);
    if (!tex) return;
    int tw, th;
    SDL_QueryTexture(tex, NULL, NULL, &tw, &th);
    SDL_Rect dst = {x, y, tw, th};
    SDL_RenderCopy(r, tex, NULL, &dst);
    SDL_DestroyTexture(tex);
}

static void draw_hud(SDL_Renderer *r, TTF_Font *font, TTF_Font *font_bold,
                     int wx, int wy, int wz, int pinza, int vel,
                     int idx, int total,
                     int kf_cur, int kf_total, int kf_tipo_act) {
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 10, 10, 20, 210);
    SDL_Rect panel = {0, 0, PANEL_W, WIN_H};
    SDL_RenderFillRect(r, &panel);
    SDL_SetRenderDrawColor(r, 60, 80, 120, 255);
    SDL_RenderDrawLine(r, PANEL_W, 0, PANEL_W, WIN_H);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    draw_text(r, font_bold, "SCARA VM", 12, 12, 100, 180, 255, 255);
    draw_text(r, font, "Simulacion de trayectoria", 12, 32, 140, 140, 160, 255);
    SDL_SetRenderDrawColor(r, 50, 70, 110, 255);
    SDL_RenderDrawLine(r, 8, 56, PANEL_W-8, 56);

    char buf[64];
    draw_text(r, font_bold, "POSICION", 12, 64, 180, 180, 200, 255);
    snprintf(buf, sizeof(buf), "X : %d mm", wx);
    draw_text(r, font, buf, 12, 84, 220, 220, 255, 255);
    snprintf(buf, sizeof(buf), "Y : %d mm", wy);
    draw_text(r, font, buf, 12, 104, 220, 220, 255, 255);
    snprintf(buf, sizeof(buf), "Z : %d mm", wz);
    draw_text(r, font, buf, 12, 124, 220, 220, 255, 255);
    SDL_SetRenderDrawColor(r, 50, 70, 110, 255);
    SDL_RenderDrawLine(r, 8, 148, PANEL_W-8, 148);

    double ck_hud=0, cy_hud=0;
    ik_codo((double)wx, (double)wy, &ck_hud, &cy_hud);
    double r2v = (double)wx*(double)wx + (double)wy*(double)wy;
    double c2v = (r2v - L1*L1 - L2*L2) / (2.0*L1*L2);
    if (c2v < -1.0) c2v = -1.0;
    if (c2v >  1.0) c2v =  1.0;
    double q2r = acos(c2v);
    double k1h = L1 + L2*cos(q2r), k2h = L2*sin(q2r);
    int q1_deg = (int)round((atan2((double)wy,(double)wx) - atan2(k2h,k1h)) * 180.0/M_PI);
    int q2_deg = (int)round(q2r * 180.0/M_PI);
    draw_text(r, font_bold, "CINEMATICA", 12, 156, 180, 180, 200, 255);
    snprintf(buf, sizeof(buf), "q1: %d deg", q1_deg);
    draw_text(r, font, buf, 12, 176, 180, 220, 180, 255);
    snprintf(buf, sizeof(buf), "q2: %d deg", q2_deg);
    draw_text(r, font, buf, 12, 196, 180, 220, 180, 255);
    SDL_SetRenderDrawColor(r, 50, 70, 110, 255);
    SDL_RenderDrawLine(r, 8, 220, PANEL_W-8, 220);

    draw_text(r, font_bold, "VELOCIDAD", 12, 228, 180, 180, 200, 255);
    snprintf(buf, sizeof(buf), "%d %%", vel);
    draw_text(r, font, buf, 12, 248, 255, 180, 50, 255);
    SDL_SetRenderDrawColor(r, 40, 40, 60, 255);
    SDL_Rect vel_bg = {12, 268, PANEL_W-24, 10};
    SDL_RenderFillRect(r, &vel_bg);
    SDL_SetRenderDrawColor(r, 255, 160, 40, 255);
    SDL_Rect vel_fg = {12, 268, (PANEL_W-24)*vel/100, 10};
    SDL_RenderFillRect(r, &vel_fg);
    SDL_SetRenderDrawColor(r, 50, 70, 110, 255);
    SDL_RenderDrawLine(r, 8, 288, PANEL_W-8, 288);

    draw_text(r, font_bold, "PINZA", 12, 296, 180, 180, 200, 255);
    if (pinza) {
        SDL_SetRenderDrawColor(r, 30, 200, 80, 255);
        SDL_Rect ind = {12, 316, 14, 14}; SDL_RenderFillRect(r, &ind);
        draw_text(r, font, "ABIERTA", 34, 316, 30, 220, 80, 255);
    } else {
        SDL_SetRenderDrawColor(r, 220, 50, 50, 255);
        SDL_Rect ind = {12, 316, 14, 14}; SDL_RenderFillRect(r, &ind);
        draw_text(r, font, "CERRADA", 34, 316, 220, 50, 50, 255);
    }
    SDL_SetRenderDrawColor(r, 50, 70, 110, 255);
    SDL_RenderDrawLine(r, 8, 342, PANEL_W-8, 342);

    draw_text(r, font_bold, "PROGRESO", 12, 350, 180, 180, 200, 255);
    snprintf(buf, sizeof(buf), "Paso %d / %d", idx+1, total);
    draw_text(r, font, buf, 12, 368, 160, 160, 190, 255);
    SDL_SetRenderDrawColor(r, 40, 40, 60, 255);
    SDL_Rect prg_bg = {12, 384, PANEL_W-24, 8};
    SDL_RenderFillRect(r, &prg_bg);
    SDL_SetRenderDrawColor(r, 70, 130, 220, 255);
    int pw = (total > 1) ? (PANEL_W-24)*(idx)/(total-1) : PANEL_W-24;
    SDL_Rect prg_fg = {12, 384, pw, 8};
    SDL_RenderFillRect(r, &prg_fg);
    for (int k = 0; k < TRAZA_KF_LEN; k++) {
        int kx = 12 + (total > 1 ? (PANEL_W-24)*traza_kf[k]/(total-1) : 0);
        SDL_SetRenderDrawColor(r, 255, 220, 60, 255);
        SDL_RenderDrawLine(r, kx, 381, kx, 394);
    }
    {
        int bw = (PANEL_W-24)/2 - 2;
        SDL_SetRenderDrawColor(r, 50, 80, 140, 255);
        SDL_Rect btn_p = {12,      398, bw, 18}; SDL_RenderFillRect(r, &btn_p);
        SDL_Rect btn_n = {12+bw+4, 398, bw, 18}; SDL_RenderFillRect(r, &btn_n);
        draw_text(r, font, "< PREV", 16,      400, 180, 210, 255, 255);
        draw_text(r, font, "NEXT >", 16+bw+4, 400, 180, 210, 255, 255);
    }
    {
        const char *kf_nombres[] = {"","HOME","MOVE","MOVEJ","APPROACH","DEPART","OPEN","CLOSE","SPEED","WAIT"};
        snprintf(buf, sizeof(buf), "KF %d/%d: %s",
            kf_cur+1, kf_total,
            (kf_tipo_act >= 0 && kf_tipo_act <= 9) ? kf_nombres[kf_tipo_act] : "-");
        draw_text(r, font, buf, 12, 420, 255, 220, 60, 255);
    }
    SDL_SetRenderDrawColor(r, 50, 70, 110, 255);
    SDL_RenderDrawLine(r, 8, 438, PANEL_W-8, 438);

    draw_text(r, font_bold, "VISTA", 12, 446, 180, 180, 200, 255);
    {
        const char *etiq[3] = {"ISO","TOP","LADO"};
        const char *nombres[3] = {"Isometrica","Superior","Lateral"};
        for (int v=0; v<3; v++) {
            int bx = 12 + v*56;
            if (v == modo_vista) {
                SDL_SetRenderDrawColor(r, 70, 130, 220, 255);
                SDL_Rect btn = {bx, 464, 50, 18}; SDL_RenderFillRect(r, &btn);
                draw_text(r, font, etiq[v], bx+6, 466, 255, 255, 255, 255);
            } else {
                SDL_SetRenderDrawColor(r, 40, 40, 60, 255);
                SDL_Rect btn = {bx, 464, 50, 18}; SDL_RenderFillRect(r, &btn);
                draw_text(r, font, etiq[v], bx+6, 466, 100, 100, 130, 255);
            }
        }
        draw_text(r, font, nombres[modo_vista], 12, 488, 140, 200, 255, 255);
    }

    draw_text(r, font, "TAB: ocultar panel",  12, WIN_H-68, 80, 80, 100, 255);
    draw_text(r, font, "V: vista  SPACE: pausa", 12, WIN_H-48, 80, 80, 100, 255);
    draw_text(r, font, "<- -> : keyframes",    12, WIN_H-28, 80, 80, 100, 255);
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %%%%s\n", SDL_GetError());
        return 1;
    }
    if (TTF_Init() != 0) {
        fprintf(stderr, "TTF_Init: %%%%s\n", TTF_GetError());
        SDL_Quit(); return 1;
    }
    TTF_Font *font      = TTF_OpenFont("arial.ttf", FONT_SIZE);
    TTF_Font *font_bold = TTF_OpenFont("arial.ttf", FONT_SIZE);
    if (!font || !font_bold) {
        fprintf(stderr, "No se encontro arial.ttf: %%%%s\n", TTF_GetError());
        TTF_Quit(); SDL_Quit(); return 1;
    }
    TTF_SetFontStyle(font_bold, TTF_STYLE_BOLD);
    SDL_Window *win = SDL_CreateWindow(
        "SCARA — Compilador de Lenguaje Robotico",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WIN_W, WIN_H, SDL_WINDOW_SHOWN);
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    int total       = traza_get_len();
    int idx         = 0;
    int running     = 1;
    int hud_visible = 1;
    int paused      = 0;
    int kf_cur      = 0;
    SDL_Event ev;

    while (running) {
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = 0;
            if (ev.type == SDL_KEYDOWN) {
                switch (ev.key.keysym.sym) {
                    case SDLK_TAB:    hud_visible = !hud_visible; break;
                    case SDLK_SPACE:  paused      = !paused;      break;
                    case SDLK_ESCAPE: running     = 0;            break;
                    case SDLK_v:
                        modo_vista = (modo_vista + 1) % 3;
                        idx    = 0;
                        kf_cur = 0;
                        paused = 0;
                        break;
                    case SDLK_LEFT:
                        if (kf_cur > 0) kf_cur--;
                        idx = traza_kf[kf_cur];
                        paused = 1;
                        break;
                    case SDLK_RIGHT:
                        if (kf_cur < TRAZA_KF_LEN - 1) kf_cur++;
                        idx = traza_kf[kf_cur];
                        paused = 1;
                        break;
                    default: break;
                }
            }
        }

        if (idx >= total) { SDL_Delay(2000); break; }

        int wx    = traza_get_x(idx);
        int wy    = traza_get_y(idx);
        int wz    = traza_get_z(idx);
        int pinza = traza_get_pinza(idx);
        int vel   = traza_get_vel(idx);

        SDL_SetRenderDrawColor(ren, 15, 15, 25, 255);
        SDL_RenderClear(ren);

        SDL_SetRenderDrawColor(ren, 30, 30, 50, 255);
        if (modo_vista == 0) {
            for (int g = -350; g <= 350; g += 50) {
                int ax,ay,bx,by;
                proj_iso(g,-350,0,&ax,&ay); ax+=PANEL_W;
                proj_iso(g, 350,0,&bx,&by); bx+=PANEL_W;
                SDL_RenderDrawLine(ren,ax,ay,bx,by);
                proj_iso(-350,g,0,&ax,&ay); ax+=PANEL_W;
                proj_iso( 350,g,0,&bx,&by); bx+=PANEL_W;
                SDL_RenderDrawLine(ren,ax,ay,bx,by);
            }
        } else if (modo_vista == 1) {
            for (int g = -350; g <= 350; g += 50) {
                int ax,ay,bx,by;
                proj_top(g,-350,0,&ax,&ay); ax+=PANEL_W;
                proj_top(g, 350,0,&bx,&by); bx+=PANEL_W;
                SDL_RenderDrawLine(ren,ax,ay,bx,by);
                proj_top(-350,g,0,&ax,&ay); ax+=PANEL_W;
                proj_top( 350,g,0,&bx,&by); bx+=PANEL_W;
                SDL_RenderDrawLine(ren,ax,ay,bx,by);
            }
            int ox,oy;
            proj_top(0,0,0,&ox,&oy); ox+=PANEL_W;
            SDL_SetRenderDrawColor(ren, 60, 60, 30, 255);
            draw_arc(ren, ox, oy,  50, 75, 100);
            draw_arc(ren, ox, oy, 350, 75, 100);
            int ax2,ay2,bx2,by2;
            proj_top(350,0,0,&ax2,&ay2); ax2+=PANEL_W; proj_top(-350,0,0,&bx2,&by2); bx2+=PANEL_W;
            SDL_SetRenderDrawColor(ren, 80, 40, 40, 255);
            SDL_RenderDrawLine(ren,ax2,ay2,bx2,by2);
            proj_top(0,350,0,&ax2,&ay2); ax2+=PANEL_W; proj_top(0,-350,0,&bx2,&by2); bx2+=PANEL_W;
            SDL_SetRenderDrawColor(ren, 40, 80, 40, 255);
            SDL_RenderDrawLine(ren,ax2,ay2,bx2,by2);
        } else {
            for (int g = 0; g <= 350; g += 50) {
                int ax,ay,bx,by;
                proj_lado(g,0,  0,&ax,&ay); ax+=PANEL_W;
                proj_lado(g,0,350,&bx,&by); bx+=PANEL_W;
                SDL_RenderDrawLine(ren,ax,ay,bx,by);
                proj_lado(  0,0,g,&ax,&ay); ax+=PANEL_W;
                proj_lado(350,0,g,&bx,&by); bx+=PANEL_W;
                SDL_RenderDrawLine(ren,ax,ay,bx,by);
            }
            int ax3,ay3,bx3,by3;
            proj_lado(0,0,0,&ax3,&ay3); ax3+=PANEL_W; proj_lado(350,0,0,&bx3,&by3); bx3+=PANEL_W;
            SDL_SetRenderDrawColor(ren, 60, 80, 60, 255);
            SDL_RenderDrawLine(ren,ax3,ay3,bx3,by3);
        }

        double ck_x=0, ck_y=0;
        ik_codo((double)wx,(double)wy,&ck_x,&ck_y);
        int cx_w=(int)round(ck_x), cy_w=(int)round(ck_y);

        int bsx,bsy,csx,csy,esx,esy;
        proj(0,    0,    0,   &bsx,&bsy);
        proj(cx_w, cy_w, wz,  &csx,&csy);
        proj(wx,   wy,   wz,  &esx,&esy);

        SDL_SetRenderDrawColor(ren, 70, 130, 220, 255);
        for (int t=-2;t<=2;t++) {
            SDL_RenderDrawLine(ren,bsx+t,bsy,csx+t,csy);
            SDL_RenderDrawLine(ren,bsx,bsy+t,csx,csy+t);
        }
        SDL_SetRenderDrawColor(ren, 180, 100, 40, 255);
        for (int t=-2;t<=2;t++) {
            SDL_RenderDrawLine(ren,csx+t,csy,esx+t,esy);
            SDL_RenderDrawLine(ren,csx,csy+t,esx,esy+t);
        }
        SDL_SetRenderDrawColor(ren, 200, 200, 200, 255);
        draw_circle(ren,bsx,bsy,8);
        SDL_SetRenderDrawColor(ren, 100, 200, 100, 255);
        draw_circle(ren,csx,csy,6);
        if (pinza) SDL_SetRenderDrawColor(ren, 255, 220, 50, 255);
        else       SDL_SetRenderDrawColor(ren, 255,  60, 60, 255);
        draw_circle(ren,esx,esy,9);

        {
            double ddx=esx-csx, ddy=esy-csy;
            double d=sqrt(ddx*ddx+ddy*ddy);
            if (d>0.5) {
                int px=(int)(-ddy/d*(pinza?8:4));
                int py=(int)(ddx/d*(pinza?8:4));
                SDL_RenderDrawLine(ren,esx+px,esy+py,
                    esx+(int)(ddx/d*12)+px,esy+(int)(ddy/d*12)+py);
                SDL_RenderDrawLine(ren,esx-px,esy-py,
                    esx+(int)(ddx/d*12)-px,esy+(int)(ddy/d*12)-py);
            }
        }

        {
            const char *label[3] = {"VISTA: ISOMETRICA","VISTA: SUPERIOR","VISTA: LATERAL"};
            draw_text(ren, font_bold, label[modo_vista],
                      WIN_W - 190, 12, 100, 180, 255, 200);
        }

        SDL_SetRenderDrawColor(ren, 30, 30, 50, 255);
        SDL_Rect bar_bg = {0, WIN_H-6, WIN_W, 6};
        SDL_RenderFillRect(ren, &bar_bg);
        SDL_SetRenderDrawColor(ren, 70, 130, 220, 255);
        int bw = (total>1) ? WIN_W*idx/(total-1) : WIN_W;
        SDL_Rect bar_fg = {0, WIN_H-6, bw, 6};
        SDL_RenderFillRect(ren, &bar_fg);

        if (hud_visible) {
            int kf_tipo_act = (kf_cur >= 0 && kf_cur < TRAZA_KF_LEN)
                              ? traza_kf_tipo[kf_cur] : 0;
            draw_hud(ren, font, font_bold, wx, wy, wz, pinza, vel,
                     idx, total, kf_cur, TRAZA_KF_LEN, kf_tipo_act);
        } else {
            draw_text(ren, font, "TAB: mostrar panel",
                      8, WIN_H-48, 80, 80, 100, 255);
            draw_text(ren, font, "V: cambiar vista",
                      8, WIN_H-28, 80, 80, 100, 255);
        }

        SDL_RenderPresent(ren);

        if (!paused) {
            int delay = 220 - vel*180/100;
            if (delay < 35) delay = 35;
            SDL_Delay(delay);
            idx++;
            /* avanzar kf_cur al keyframe que corresponde al idx actual */
            while (kf_cur < TRAZA_KF_LEN - 1 && traza_kf[kf_cur+1] <= idx)
                kf_cur++;
        } else {
            SDL_Delay(16);
        }
    }

    TTF_CloseFont(font);
    TTF_CloseFont(font_bold);
    TTF_Quit();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
