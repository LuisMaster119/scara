#include <string.h>
#include "optimizador.h"

static int es_operando_constante(const Instruccion* ins, int es_arg1) {
    if (es_arg1) return !(ins->flags & INS_F_ARG1_VAR);
    return !(ins->flags & INS_F_ARG3_VAR);
}

static int obtener_operando(const Instruccion* ins, int es_arg1) {
    return es_arg1 ? ins->arg1 : ins->arg3;
}

int optimizar_bytecode(const Instruccion* entrada, int len_entrada,
                       Instruccion* salida, int max_salida) {
    if (len_entrada > max_salida) return -1;

    int salida_len = 0;
    for (int i = 0; i < len_entrada; i++) {
        const Instruccion* ins = &entrada[i];
        Instruccion copia = *ins;

        // Eliminacion de SPEED redundante
        if (copia.opcode == OP_SPEED && salida_len > 0) {
            const Instruccion* prev = &salida[salida_len - 1];
            if (prev->opcode == OP_SPEED) {
                int prev_var  = (prev->flags  & INS_F_ARG1_VAR) != 0;
                int copia_var = (copia.flags  & INS_F_ARG1_VAR) != 0;
                int mismo = 0;
                if (!prev_var && !copia_var) {
                    /* Ambos literales: comparar valor */
                    mismo = (prev->arg1 == copia.arg1);
                } else if (prev_var && copia_var) {
                    /* Ambas variables: comparar nombre */
                    mismo = (strcmp(prev->sval, copia.sval) == 0);
                }
                if (mismo) continue;
            }
        }

        // Eliminacion de OPEN/CLOSE duplicado inmediato
        if ((copia.opcode == OP_OPEN || copia.opcode == OP_CLOSE) && salida_len > 0) {
            const Instruccion* prev = &salida[salida_len - 1];
            if (prev->opcode == copia.opcode) {
                continue;
            }
        }

        // Plegado de constantes en asignacion
        if (copia.opcode == OP_ASSIGN && copia.arg2 != 0) {
            int left_const = es_operando_constante(&copia, 1);
            int right_const = es_operando_constante(&copia, 0);
            if (left_const && right_const) {
                int lhs = obtener_operando(&copia, 1);
                int rhs = obtener_operando(&copia, 0);
                int result = (copia.arg2 == 1) ? (lhs + rhs) : (lhs - rhs);
                copia.arg1 = result;
                copia.arg2 = 0;
                copia.arg3 = 0;
                copia.flags = 0;
                copia.sval2[0] = '\0';
                copia.sval3[0] = '\0';
            }
        }

        // Simplificacion de asignacion de cero-op
        if (copia.opcode == OP_ASSIGN && copia.arg2 == 0 &&
            (copia.flags & INS_F_ARG1_VAR) && copia.sval2[0] != '\0' &&
            strcmp(copia.sval, copia.sval2) == 0) {
            // x = x ; no lo emitimos
            continue;
        }

        salida[salida_len++] = copia;
    }

    return salida_len;
}
