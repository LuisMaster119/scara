#ifndef CINEMATICA_H
#define CINEMATICA_H

#define CIN_MODO_CODO_ARRIBA 0
#define CIN_MODO_CODO_ABAJO 1

int cinematica_ik_xy_modo(int x, int y, int modo, int* q1_deg, int* q2_deg);
int cinematica_ik_xy(int x, int y, int* q1_deg, int* q2_deg);
int cinematica_ik_xy_modo_asm(int x, int y, int modo, int* q1_deg, int* q2_deg);
int cinematica_ik_xy_asm(int x, int y, int* q1_deg, int* q2_deg);
int cinematica_xy_en_alcance(int x, int y);
int cinematica_ik_xy_core_c(int x, int y, int modo, int* q1_deg, int* q2_deg);
int cinematica_ik_xy_core_lookup_c(int x, int y, int modo, int* q1_deg, int* q2_deg);
void cinematica_init_tabla_cos(void);
int cinematica_q1_desde_componentes_c(int x, int y, int k1_scaled, int k2_scaled, int* q1_deg);
int cinematica_q1_desde_q2_deg_c(int x, int y, int q2_deg, int* q1_deg);
extern int cin_cos_tab_deg[181];
extern int cin_sin_tab_deg[181];
extern int cin_atan_lut_deg[1001];
int cinematica_comparar_c_vs_asm_modo(int x, int y, int modo, int tolerancia_deg,
									  int* ok_c, int* ok_asm,
									  int* q1_c, int* q2_c,
									  int* q1_asm, int* q2_asm);
int cinematica_comparar_c_vs_asm(int x, int y, int tolerancia_deg,
								 int* ok_c, int* ok_asm,
								 int* q1_c, int* q2_c,
								 int* q1_asm, int* q2_asm);

#endif
