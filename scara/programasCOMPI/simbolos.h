#ifndef SIMBOLOS_H
#define SIMBOLOS_H

typedef enum {
    SIM_VAR,
    SIM_POINT
} TipoSimbolo;

typedef struct {
    char        nombre[64];
    TipoSimbolo tipo;
    int         val1;
    int         val2;
    int         val3;
} Simbolo;

#define MAX_SIMBOLOS 64
extern Simbolo tabla_simbolos[MAX_SIMBOLOS];
extern int     tabla_len;

void     tabla_agregar(const char* nombre, TipoSimbolo tipo,
                       int v1, int v2, int v3);
Simbolo* tabla_buscar(const char* nombre);
void     tabla_imprimir(void);

#endif
