#ifndef ERRORES_H
#define ERRORES_H

typedef enum {
    ERR_LEXICO,
    ERR_SINTACTICO,
    ERR_SEMANTICO
} TipoError;

void error_init(const char* fuente);
void error_reportar(TipoError tipo, int linea,
                    const char* token, const char* mensaje);
int  error_hubo(void);   /* retorna 1 si hubo al menos un error */

#endif
