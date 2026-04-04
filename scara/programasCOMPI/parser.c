#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lexer.h"
#include "parser.h"
#include "simbolos.h"
#include "errores.h"

// --- Bytecode ---------------------------------------------

Instruccion bytecode[MAX_INSTRUCCIONES];
int         bytecode_len = 0;

void emitir(Opcode op, int a1, int a2, int a3, const char* sval) {
    if (bytecode_len >= MAX_INSTRUCCIONES) {
        fprintf(stderr, "Error: programa demasiado largo\n");
        exit(1);
    }
    bytecode[bytecode_len].opcode = op;
    bytecode[bytecode_len].arg1   = a1;
    bytecode[bytecode_len].arg2   = a2;
    bytecode[bytecode_len].arg3   = a3;
    bytecode[bytecode_len].flags  = 0;
    if (sval) strcpy(bytecode[bytecode_len].sval, sval);
    else      bytecode[bytecode_len].sval[0] = '\0';
    bytecode[bytecode_len].sval2[0] = '\0';
    bytecode[bytecode_len].sval3[0] = '\0';
    bytecode_len++;
}

static void emitir_assign_ext(int a1, int op, int a3,
                              int flags,
                              const char* destino,
                              const char* nombre_izq,
                              const char* nombre_der) {
    emitir(OP_ASSIGN, a1, op, a3, destino);
    bytecode[bytecode_len - 1].flags = flags;
    if (nombre_izq) strcpy(bytecode[bytecode_len - 1].sval2, nombre_izq);
    if (nombre_der) strcpy(bytecode[bytecode_len - 1].sval3, nombre_der);
}

const char* opcode_a_texto(Opcode op) {
    switch (op) {
        case OP_MOVE:       return "OP_MOVE";
        case OP_MOVEJ:      return "OP_MOVEJ";
        case OP_APPROACH:   return "OP_APPROACH";
        case OP_DEPART:     return "OP_DEPART";
        case OP_HOME:       return "OP_HOME";
        case OP_OPEN:       return "OP_OPEN";
        case OP_CLOSE:      return "OP_CLOSE";
        case OP_SPEED:      return "OP_SPEED";
        case OP_WAIT:       return "OP_WAIT";
        case OP_PRINT:      return "OP_PRINT";
        case OP_WHILE:      return "OP_WHILE";
        case OP_END_WHILE:  return "OP_END_WHILE";
        case OP_IF:         return "OP_IF";
        case OP_ELSE:       return "OP_ELSE";
        case OP_END_IF:     return "OP_END_IF";
        case OP_REPEAT:     return "OP_REPEAT";
        case OP_END_REPEAT: return "OP_END_REPEAT";
        case OP_VAR:        return "OP_VAR";
        case OP_POINT:      return "OP_POINT";
        case OP_ASSIGN:     return "OP_ASSIGN";
        case OP_HALT:       return "OP_HALT";
        default:            return "OP_UNKNOWN";
    }
}

// --- Estado del Parser ------------------------------------

void parser_init(Parser* p, Lexer* lex) {
    p->lex       = lex;
    p->actual    = lexer_siguiente(lex);
    p->siguiente = lexer_siguiente(lex);
}

void parser_avanzar(Parser* p) {
    p->actual    = p->siguiente;
    p->siguiente = lexer_siguiente(p->lex);
}

// --- Convierte numero de token a nombre legible -----------

const char* nombre_token_esperado(TipoToken tipo) {
    switch (tipo) {
        case TOK_MOVE:    return "MOVE";
        case TOK_MOVEJ:   return "MOVEJ";
        case TOK_APPROACH:return "APPROACH";
        case TOK_DEPART:  return "DEPART";
        case TOK_HOME:    return "HOME";
        case TOK_OPEN:    return "OPEN";
        case TOK_CLOSE:   return "CLOSE";
        case TOK_REPEAT:  return "REPEAT";
        case TOK_WHILE:   return "WHILE";
        case TOK_IF:      return "IF";
        case TOK_ELSE:    return "ELSE";
        case TOK_END:     return "END";
        case TOK_WAIT:    return "WAIT";
        case TOK_PROGRAM: return "PROGRAM";
        case TOK_VAR:     return "VAR";
        case TOK_POINT:   return "POINT";
        case TOK_SPEED:   return "SPEED";
        case TOK_PRINT:   return "PRINT";
        case TOK_ASSIGN:  return "=";
        case TOK_PLUS:    return "+";
        case TOK_MINUS:   return "-";
        case TOK_LESS:    return "<";
        case TOK_GREATER: return ">";
        case TOK_EQUAL:   return "==";
        case TOK_NUMBER:  return "NUMBER";
        case TOK_STRING:  return "STRING";
        case TOK_IDENT:   return "IDENTIFICADOR";
        case TOK_EOF:     return "EOF";
        default:          return "token desconocido";
    }
}

void parser_consumir(Parser* p, TipoToken esperado) {
    if (p->actual.tipo != esperado) {
        char mensaje[128];
        sprintf(mensaje,
            "Se esperaba '%s' pero se encontro '%s'",
            nombre_token_esperado(esperado), p->actual.valor);
        error_reportar(ERR_SINTACTICO, p->actual.linea,
                       p->actual.valor, mensaje);
    }
    parser_avanzar(p);
}

// --- Funciones del Parser ---------------------------------

void parsear_bloque(Parser* p);

int parsear_expresion(Parser* p, char* nombre_out) {
    if (p->actual.tipo == TOK_NUMBER) {
        int val = atoi(p->actual.valor);
        parser_avanzar(p);
        return val;
    }
    if (p->actual.tipo == TOK_IDENT) {
        Simbolo* s = tabla_buscar(p->actual.valor);
        if (s == NULL) {
            char mensaje[128];
            sprintf(mensaje, "'%s' no fue declarado", p->actual.valor);
            error_reportar(ERR_SEMANTICO, p->actual.linea,
                           p->actual.valor, mensaje);
        }
        if (s->tipo != SIM_VAR) {
            char mensaje[128];
            sprintf(mensaje,
                "'%s' es POINT, se esperaba VAR", p->actual.valor);
            error_reportar(ERR_SEMANTICO, p->actual.linea,
                           p->actual.valor, mensaje);
        }
        if (nombre_out) strcpy(nombre_out, p->actual.valor);
        parser_avanzar(p);
        return -1;
    }
    char mensaje[128];
    sprintf(mensaje,
        "Se esperaba numero o variable, se encontro '%s'",
        p->actual.valor);
    error_reportar(ERR_SINTACTICO, p->actual.linea,
                   p->actual.valor, mensaje);
    return 0;
}

void parsear_condicion(Parser* p, int* izq, int* der,
                       TipoToken* op, char* nombre_izq, char* nombre_der) {
    *izq = parsear_expresion(p, nombre_izq);

    if (p->actual.tipo != TOK_LESS    &&
        p->actual.tipo != TOK_GREATER &&
        p->actual.tipo != TOK_EQUAL) {
        char mensaje[128];
        sprintf(mensaje,
            "Se esperaba operador (<, >, ==) pero se encontro '%s'",
            p->actual.valor);
        error_reportar(ERR_SINTACTICO, p->actual.linea,
                       p->actual.valor, mensaje);
    }
    *op = p->actual.tipo;
    parser_avanzar(p);
    *der = parsear_expresion(p, nombre_der);
}

void parsear_movimiento(Parser* p) {
    TipoToken tipo = p->actual.tipo;
    parser_avanzar(p);

    if (tipo == TOK_HOME) {
        emitir(OP_HOME, 0, 0, 0, NULL);
        return;
    }
    if (tipo == TOK_DEPART) {
        int dist = atoi(p->actual.valor);
        if (dist <= 0) {
            char mensaje[128];
            sprintf(mensaje,
                "DEPART debe ser mayor a 0, se encontro %d", dist);
            error_reportar(ERR_SEMANTICO, p->actual.linea,
                           p->actual.valor, mensaje);
        }
        parser_consumir(p, TOK_NUMBER);
        emitir(OP_DEPART, dist, 0, 0, NULL);
        return;
    }

    if (p->actual.tipo == TOK_IDENT) {
        char nombre[64];
        strcpy(nombre, p->actual.valor);
        int linea_token = p->actual.linea;  // guardar linea antes de avanzar
        parser_avanzar(p);

        Simbolo* s = tabla_buscar(nombre);
        if (s == NULL) {
            char mensaje[128];
            sprintf(mensaje, "'%s' no fue declarado", nombre);
            error_reportar(ERR_SEMANTICO, linea_token, nombre, mensaje);
        }
        if (s->tipo != SIM_POINT) {
            char mensaje[128];
            sprintf(mensaje, "'%s' es VAR, se esperaba POINT", nombre);
            error_reportar(ERR_SEMANTICO, linea_token, nombre, mensaje);
        }

        Opcode op = (tipo == TOK_MOVE)     ? OP_MOVE :
                    (tipo == TOK_MOVEJ)    ? OP_MOVEJ : OP_APPROACH;
        emitir(op, s->val1, s->val2, s->val3, nombre);

    } else {
        int x = atoi(p->actual.valor); parser_consumir(p, TOK_NUMBER);
        int y = atoi(p->actual.valor); parser_consumir(p, TOK_NUMBER);
        int z = atoi(p->actual.valor); parser_consumir(p, TOK_NUMBER);
        Opcode op = (tipo == TOK_MOVE)     ? OP_MOVE :
                    (tipo == TOK_MOVEJ)    ? OP_MOVEJ : OP_APPROACH;
        emitir(op, x, y, z, NULL);
    }
}

void parsear_declaracion(Parser* p) {
    TipoToken tipo = p->actual.tipo;
    parser_avanzar(p);

    char nombre[64];
    strcpy(nombre, p->actual.valor);
    int linea_nombre = p->actual.linea;  // guardar linea antes de avanzar
    parser_consumir(p, TOK_IDENT);
    parser_consumir(p, TOK_ASSIGN);

    if (tipo == TOK_VAR) {
        int val = atoi(p->actual.valor);
        parser_consumir(p, TOK_NUMBER);

        if (tabla_buscar(nombre) != NULL) {
            char mensaje[128];
            sprintf(mensaje, "'%s' ya fue declarado anteriormente", nombre);
            error_reportar(ERR_SEMANTICO, linea_nombre, nombre, mensaje);
        }

        tabla_agregar(nombre, SIM_VAR, val, 0, 0);
        emitir(OP_VAR, val, 0, 0, nombre);

    } else {
        int x = atoi(p->actual.valor); parser_consumir(p, TOK_NUMBER);
        int y = atoi(p->actual.valor); parser_consumir(p, TOK_NUMBER);
        int z = atoi(p->actual.valor); parser_consumir(p, TOK_NUMBER);

        if (tabla_buscar(nombre) != NULL) {
            char mensaje[128];
            sprintf(mensaje, "'%s' ya fue declarado anteriormente", nombre);
            error_reportar(ERR_SEMANTICO, linea_nombre, nombre, mensaje);
        }

        // verificar alcanzabilidad
        int L1 = 200, L2 = 150;
        int d2   = x*x + y*y;
        int dmax = (L1+L2)*(L1+L2);
        int dmin = (L1-L2)*(L1-L2);
        if (d2 > dmax || d2 < dmin) {
            char mensaje[128];
            sprintf(mensaje,
                "POINT '%s' fuera de alcance (distancia debe ser %d-%d mm)",
                nombre, L1-L2, L1+L2);
            error_reportar(ERR_SEMANTICO, linea_nombre, nombre, mensaje);
        }

        tabla_agregar(nombre, SIM_POINT, x, y, z);
        emitir(OP_POINT, x, y, z, nombre);
    }
}

void parsear_asignacion(Parser* p) {
    char nombre[64];
    strcpy(nombre, p->actual.valor);
    int linea_nombre = p->actual.linea;  // guardar linea antes de avanzar

    Simbolo* s = tabla_buscar(nombre);
    if (s == NULL) {
        char mensaje[128];
        sprintf(mensaje, "'%s' no fue declarado", nombre);
        error_reportar(ERR_SEMANTICO, linea_nombre, nombre, mensaje);
    }
    if (s->tipo != SIM_VAR) {
        char mensaje[128];
        sprintf(mensaje, "No se puede asignar a POINT '%s'", nombre);
        error_reportar(ERR_SEMANTICO, linea_nombre, nombre, mensaje);
    }

    parser_consumir(p, TOK_IDENT);
    parser_consumir(p, TOK_ASSIGN);

    char nombre_izq[64] = "";
    int val_izq = parsear_expresion(p, nombre_izq);

    if (p->actual.tipo == TOK_PLUS || p->actual.tipo == TOK_MINUS) {
        TipoToken op = p->actual.tipo;
        parser_avanzar(p);
        char nombre_der[64] = "";
        int val_der = parsear_expresion(p, nombre_der);
        int flags = 0;
        if (val_izq == -1) flags |= INS_F_ARG1_VAR;
        if (val_der == -1) flags |= INS_F_ARG3_VAR;
        emitir_assign_ext(
            (val_izq == -1 ? 0 : val_izq),
            (op == TOK_PLUS ? 1 : -1),
            (val_der == -1 ? 0 : val_der),
            flags,
            nombre,
            (val_izq == -1 ? nombre_izq : NULL),
            (val_der == -1 ? nombre_der : NULL)
        );
    } else {
        int flags = 0;
        if (val_izq == -1) flags |= INS_F_ARG1_VAR;
        emitir_assign_ext(
            (val_izq == -1 ? 0 : val_izq),
            0,
            0,
            flags,
            nombre,
            (val_izq == -1 ? nombre_izq : NULL),
            NULL
        );
    }
}

void parsear_flujo(Parser* p) {
    TipoToken tipo = p->actual.tipo;
    parser_avanzar(p);

    if (tipo == TOK_WHILE) {
        int izq, der; TipoToken op;
        char nom_izq[64] = "", nom_der[64] = "";
        parsear_condicion(p, &izq, &der, &op, nom_izq, nom_der);
        emitir(OP_WHILE,
              (izq == -1 ? 0 : izq),
              (int)op,
              (der == -1 ? 0 : der),
              (izq == -1 ? nom_izq : NULL));
        if (izq == -1) bytecode[bytecode_len - 1].flags |= INS_F_ARG1_VAR;
        if (der == -1) bytecode[bytecode_len - 1].flags |= INS_F_ARG3_VAR;
        if (der == -1) strcpy(bytecode[bytecode_len - 1].sval2, nom_der);
        parsear_bloque(p);
        parser_consumir(p, TOK_END);
        emitir(OP_END_WHILE, 0, 0, 0, NULL);

    } else if (tipo == TOK_REPEAT) {
        int n = atoi(p->actual.valor);
        if (n <= 0) {
            char mensaje[128];
            sprintf(mensaje,
                "REPEAT debe ser mayor a 0, se encontro %d", n);
            error_reportar(ERR_SEMANTICO, p->actual.linea,
                           p->actual.valor, mensaje);
        }
        parser_consumir(p, TOK_NUMBER);
        emitir(OP_REPEAT, n, 0, 0, NULL);
        parsear_bloque(p);
        parser_consumir(p, TOK_END);
        emitir(OP_END_REPEAT, 0, 0, 0, NULL);

    } else if (tipo == TOK_IF) {
        int izq, der; TipoToken op;
        char nom_izq[64] = "", nom_der[64] = "";
        parsear_condicion(p, &izq, &der, &op, nom_izq, nom_der);
        emitir(OP_IF,
              (izq == -1 ? 0 : izq),
              (int)op,
              (der == -1 ? 0 : der),
              (izq == -1 ? nom_izq : NULL));
        if (izq == -1) bytecode[bytecode_len - 1].flags |= INS_F_ARG1_VAR;
        if (der == -1) bytecode[bytecode_len - 1].flags |= INS_F_ARG3_VAR;
        if (der == -1) strcpy(bytecode[bytecode_len - 1].sval2, nom_der);
        parsear_bloque(p);
        if (p->actual.tipo == TOK_ELSE) {
            parser_avanzar(p);
            emitir(OP_ELSE, 0, 0, 0, NULL);
            parsear_bloque(p);
        }
        parser_consumir(p, TOK_END);
        emitir(OP_END_IF, 0, 0, 0, NULL);
    }
}

void parsear_instruccion(Parser* p) {
    switch (p->actual.tipo) {
        case TOK_MOVE:
        case TOK_MOVEJ:
        case TOK_APPROACH:
        case TOK_DEPART:
        case TOK_HOME:
            parsear_movimiento(p); break;

        case TOK_OPEN:
            parser_avanzar(p);
            emitir(OP_OPEN, 0, 0, 0, NULL); break;

        case TOK_CLOSE:
            parser_avanzar(p);
            emitir(OP_CLOSE, 0, 0, 0, NULL); break;

        case TOK_SPEED:
            parser_avanzar(p);
            {
                int vel = atoi(p->actual.valor);
                if (vel < 0 || vel > 100) {
                    char mensaje[128];
                    sprintf(mensaje,
                        "SPEED debe ser 0-100, se encontro %d", vel);
                    error_reportar(ERR_SEMANTICO, p->actual.linea,
                                   p->actual.valor, mensaje);
                }
                emitir(OP_SPEED, vel, 0, 0, NULL);
                parser_consumir(p, TOK_NUMBER);
            }
            break;

        case TOK_WAIT:
            parser_avanzar(p);
            {
                int tiempo = atoi(p->actual.valor);
                if (tiempo <= 0) {
                    char mensaje[128];
                    sprintf(mensaje,
                        "WAIT debe ser mayor a 0, se encontro %d", tiempo);
                    error_reportar(ERR_SEMANTICO, p->actual.linea,
                                   p->actual.valor, mensaje);
                }
                emitir(OP_WAIT, tiempo, 0, 0, NULL);
                parser_consumir(p, TOK_NUMBER);
            }
            break;

        case TOK_PRINT:
            parser_avanzar(p);
            emitir(OP_PRINT, 0, 0, 0, p->actual.valor);
            parser_avanzar(p); break;

        case TOK_VAR:
        case TOK_POINT:
            parsear_declaracion(p); break;

        case TOK_WHILE:
        case TOK_REPEAT:
        case TOK_IF:
            parsear_flujo(p); break;

        case TOK_IDENT:
            if (p->siguiente.tipo == TOK_ASSIGN)
                parsear_asignacion(p);
            break;

        default: break;
    }
}

void parsear_bloque(Parser* p) {
    while (p->actual.tipo != TOK_END  &&
           p->actual.tipo != TOK_ELSE &&
           p->actual.tipo != TOK_EOF) {
        parsear_instruccion(p);
    }
}

void parsear_programa(Parser* p) {
    parser_consumir(p, TOK_PROGRAM);
    parser_consumir(p, TOK_IDENT);
    parsear_bloque(p);
    parser_consumir(p, TOK_END);
    emitir(OP_HALT, 0, 0, 0, NULL);
}