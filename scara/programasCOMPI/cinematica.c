#include <math.h>
#include <stdlib.h>
#include "cinematica.h"

#define CIN_L1 200.0
#define CIN_L2 150.0
#define CIN_PI 3.14159265358979323846
#define CIN_COS_SCALE 10000

int cin_cos_tab_deg[181];
int cin_sin_tab_deg[181];
int cin_atan_lut_deg[1001];
static int cin_init_tabla = 0;

void cinematica_init_tabla_cos(void) {
    if (cin_init_tabla) return;
    for (int d = 0; d <= 180; d++) {
        double r = ((double)d * CIN_PI) / 180.0;
        cin_cos_tab_deg[d] = (int)lround(cos(r) * CIN_COS_SCALE);
        cin_sin_tab_deg[d] = (int)lround(sin(r) * CIN_COS_SCALE);
    }
    for (int i = 0; i <= 1000; i++) {
        double x = (double)i / 1000.0;
        cin_atan_lut_deg[i] = (int)lround(atan(x) * 180.0 / CIN_PI);
    }
    cin_init_tabla = 1;
}

int cinematica_q1_desde_componentes_c(int x, int y, int k1_scaled, int k2_scaled, int* q1_deg) {
    double q1 = atan2((double)y, (double)x) - atan2((double)k2_scaled, (double)k1_scaled);
    int q1d = (int)lround(q1 * 180.0 / CIN_PI);

    while (q1d > 180) q1d -= 360;
    while (q1d <= -180) q1d += 360;

    *q1_deg = q1d;
    return 1;
}

int cinematica_q1_desde_q2_deg_c(int x, int y, int q2_deg, int* q1_deg) {
    double q2 = ((double)q2_deg * CIN_PI) / 180.0;
    double k1 = CIN_L1 + CIN_L2 * cos(q2);
    double k2 = CIN_L2 * sin(q2);
    double q1 = atan2((double)y, (double)x) - atan2(k2, k1);
    int q1d = (int)lround(q1 * 180.0 / CIN_PI);

    while (q1d > 180) q1d -= 360;
    while (q1d <= -180) q1d += 360;

    *q1_deg = q1d;
    return 1;
}

static int cin_buscar_q2_por_cos(double c2, int* q2_deg) {
    int c2s = (int)lround(c2 * CIN_COS_SCALE);
    int mejor_d = 0;
    int mejor_err = abs(c2s - cin_cos_tab_deg[0]);

    for (int d = 1; d <= 180; d++) {
        int err = abs(c2s - cin_cos_tab_deg[d]);
        if (err < mejor_err) {
            mejor_err = err;
            mejor_d = d;
        }
    }

    *q2_deg = mejor_d;
    return 1;
}

int cinematica_ik_xy_core_c(int x, int y, int modo, int* q1_deg, int* q2_deg) {
    double r2 = (double)x * (double)x + (double)y * (double)y;
    double den = 2.0 * CIN_L1 * CIN_L2;
    double c2 = (r2 - CIN_L1 * CIN_L1 - CIN_L2 * CIN_L2) / den;

    if (c2 < -1.0 || c2 > 1.0) {
        return 0;
    }

    if (c2 < -1.0) c2 = -1.0;
    if (c2 > 1.0) c2 = 1.0;

    double q2 = acos(c2);
    if (modo == CIN_MODO_CODO_ARRIBA) {
        q2 = -q2;
    }

    double k1 = CIN_L1 + CIN_L2 * cos(q2);
    double k2 = CIN_L2 * sin(q2);
    double q1 = atan2((double)y, (double)x) - atan2(k2, k1);

    int q1d = (int)lround(q1 * 180.0 / CIN_PI);
    int q2d = (int)lround(q2 * 180.0 / CIN_PI);

    while (q1d > 180) q1d -= 360;
    while (q1d <= -180) q1d += 360;

    *q1_deg = q1d;
    *q2_deg = q2d;
    return 1;
}

int cinematica_ik_xy_core_lookup_c(int x, int y, int modo, int* q1_deg, int* q2_deg) {
    double r2 = (double)x * (double)x + (double)y * (double)y;
    double den = 2.0 * CIN_L1 * CIN_L2;
    double c2 = (r2 - CIN_L1 * CIN_L1 - CIN_L2 * CIN_L2) / den;
    int q2_deg_abs = 0;

    if (c2 < -1.0 || c2 > 1.0) {
        return 0;
    }

    if (c2 < -1.0) c2 = -1.0;
    if (c2 > 1.0) c2 = 1.0;

    cinematica_init_tabla_cos();
    if (!cin_buscar_q2_por_cos(c2, &q2_deg_abs)) {
        return 0;
    }

    int q2_deg_signed = (modo == CIN_MODO_CODO_ARRIBA) ? -q2_deg_abs : q2_deg_abs;
    int q1d = 0;

    if (!cinematica_q1_desde_q2_deg_c(x, y, q2_deg_signed, &q1d)) {
        return 0;
    }

    *q1_deg = q1d;
    *q2_deg = q2_deg_signed;
    return 1;
}

int cinematica_xy_en_alcance(int x, int y) {
    double r2 = (double)x * (double)x + (double)y * (double)y;
    double dmax = (CIN_L1 + CIN_L2) * (CIN_L1 + CIN_L2);
    double dmin = (CIN_L1 - CIN_L2) * (CIN_L1 - CIN_L2);
    return (r2 >= dmin && r2 <= dmax);
}

int cinematica_ik_xy_modo(int x, int y, int modo, int* q1_deg, int* q2_deg) {
    return cinematica_ik_xy_core_c(x, y, modo, q1_deg, q2_deg);
}

int cinematica_ik_xy(int x, int y, int* q1_deg, int* q2_deg) {
    return cinematica_ik_xy_modo(x, y, CIN_MODO_CODO_ABAJO, q1_deg, q2_deg);
}

int cinematica_comparar_c_vs_asm_modo(int x, int y, int modo, int tolerancia_deg,
                                      int* ok_c, int* ok_asm,
                                      int* q1_c, int* q2_c,
                                      int* q1_asm, int* q2_asm) {
    int c_ok = cinematica_ik_xy_modo(x, y, modo, q1_c, q2_c);
    int a_ok = cinematica_ik_xy_modo_asm(x, y, modo, q1_asm, q2_asm);

    if (ok_c) *ok_c = c_ok;
    if (ok_asm) *ok_asm = a_ok;

    if (!c_ok || !a_ok) {
        return (c_ok == a_ok);
    }

    return (abs(*q1_c - *q1_asm) <= tolerancia_deg) &&
           (abs(*q2_c - *q2_asm) <= tolerancia_deg);
}

int cinematica_comparar_c_vs_asm(int x, int y, int tolerancia_deg,
                                 int* ok_c, int* ok_asm,
                                 int* q1_c, int* q2_c,
                                 int* q1_asm, int* q2_asm) {
    return cinematica_comparar_c_vs_asm_modo(
        x, y, CIN_MODO_CODO_ABAJO, tolerancia_deg,
        ok_c, ok_asm, q1_c, q2_c, q1_asm, q2_asm
    );
}
