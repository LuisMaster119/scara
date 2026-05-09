#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "ensamblador.h"

static const char* operacion_a_texto(Opcode op) {
    switch (op) {
        case OP_MOVE: return "MOVE";
        case OP_MOVEJ: return "MOVEJ";
        case OP_APPROACH: return "APPROACH";
        case OP_DEPART: return "DEPART";
        case OP_HOME: return "HOME";
        case OP_OPEN: return "OPEN";
        case OP_CLOSE: return "CLOSE";
        case OP_SPEED: return "SPEED";
        case OP_WAIT: return "WAIT";
        case OP_PRINT: return "PRINT";
        case OP_WHILE: return "WHILE";
        case OP_END_WHILE: return "END_WHILE";
        case OP_IF: return "IF";
        case OP_ELSE: return "ELSE";
        case OP_END_IF: return "END_IF";
        case OP_REPEAT: return "REPEAT";
        case OP_END_REPEAT: return "END_REPEAT";
        case OP_VAR: return "VAR";
        case OP_POINT: return "POINT";
        case OP_ASSIGN: return "ASSIGN";
        case OP_HALT: return "HALT";
        default: return "UNKNOWN";
    }
}

static int es_numero(const char* token) {
    if (!token || *token == '\0') return 0;
    if (*token == '-' || *token == '+') token++;
    if (!*token) return 0;
    while (*token) {
        if (!isdigit((unsigned char)*token)) return 0;
        token++;
    }
    return 1;
}

static void escribir_operando(FILE* f, const char* nombre, int valor, int es_var) {
    if (es_var) {
        fprintf(f, "%s", nombre);
    } else {
        fprintf(f, "%d", valor);
    }
}

static int parsear_operando(const char* token, int* out_valor, int* out_es_var, char* out_nombre) {
    if (es_numero(token)) {
        *out_valor = atoi(token);
        *out_es_var = 0;
        out_nombre[0] = '\0';
        return 1;
    }
    *out_valor = 0;
    *out_es_var = 1;
    strcpy(out_nombre, token);
    return 1;
}

static const char* operador_texto(TipoToken tipo) {
    switch (tipo) {
        case TOK_LESS: return "<";
        case TOK_GREATER: return ">";
        case TOK_EQUAL: return "==";
        default: return "?";
    }
}

static int operador_tipo(const char* token) {
    if (strcmp(token, "<") == 0) return TOK_LESS;
    if (strcmp(token, ">") == 0) return TOK_GREATER;
    if (strcmp(token, "==") == 0) return TOK_EQUAL;
    return -1;
}

int generar_ensamblador(const Instruccion* programa, int longitud,
                        const char* ruta_salida) {
    FILE* f = fopen(ruta_salida, "w");
    if (!f) return 1;

    for (int i = 0; i < longitud; i++) {
        const Instruccion* ins = &programa[i];
        const char* op_text = operacion_a_texto(ins->opcode);
        switch (ins->opcode) {
            case OP_MOVE:
            case OP_MOVEJ:
            case OP_APPROACH:
                fprintf(f, "%s %d %d %d\n", op_text, ins->arg1, ins->arg2, ins->arg3);
                break;
            case OP_DEPART:
                fprintf(f, "DEPART %d\n", ins->arg1);
                break;
            case OP_HOME:
            case OP_OPEN:
            case OP_CLOSE:
            case OP_END_WHILE:
            case OP_ELSE:
            case OP_END_IF:
            case OP_END_REPEAT:
            case OP_HALT:
                fprintf(f, "%s\n", op_text);
                break;
            case OP_SPEED:
            case OP_WAIT:
                fprintf(f, "%s %d\n", op_text, ins->arg1);
                break;
            case OP_PRINT:
                fprintf(f, "PRINT \"%s\"\n", ins->sval);
                break;
            case OP_VAR:
                fprintf(f, "VAR %s %d\n", ins->sval, ins->arg1);
                break;
            case OP_POINT:
                fprintf(f, "POINT %s %d %d %d\n", ins->sval, ins->arg1, ins->arg2, ins->arg3);
                break;
            case OP_ASSIGN: {
                fprintf(f, "ASSIGN %s ", ins->sval);
                if (ins->flags & INS_F_ARG1_VAR) {
                    fprintf(f, "%s", ins->sval2);
                } else {
                    fprintf(f, "%d", ins->arg1);
                }
                if (ins->arg2 == 1) {
                    fprintf(f, " + ");
                } else if (ins->arg2 == -1) {
                    fprintf(f, " - ");
                }
                if (ins->arg2 != 0) {
                    if (ins->flags & INS_F_ARG3_VAR) {
                        fprintf(f, "%s", ins->sval3);
                    } else {
                        fprintf(f, "%d", ins->arg3);
                    }
                }
                fprintf(f, "\n");
                break;
            }
            case OP_WHILE:
            case OP_IF: {
                char left[64] = "";
                char right[64] = "";
                if (ins->flags & INS_F_ARG1_VAR) strcpy(left, ins->sval);
                else sprintf(left, "%d", ins->arg1);
                if (ins->flags & INS_F_ARG3_VAR) strcpy(right, ins->sval2);
                else sprintf(right, "%d", ins->arg3);
                fprintf(f, "%s %s %s %s\n", op_text, left, operador_texto(ins->arg2), right);
                break;
            }
            case OP_REPEAT:
                fprintf(f, "REPEAT %d\n", ins->arg1);
                break;
            default:
                fprintf(f, "%s\n", op_text);
                break;
        }
    }

    fclose(f);
    return 0;
}

static char* trim(char* s) {
    while (*s && isspace((unsigned char)*s)) s++;
    if (*s == '\0') return s;
    char* end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) *end-- = '\0';
    return s;
}

static char* leer_cadena_entre_comillas(char* linea) {
    char* inicio = strchr(linea, '"');
    if (!inicio) return NULL;
    inicio++;
    char* fin = strchr(inicio, '"');
    if (!fin) return NULL;
    *fin = '\0';
    return inicio;
}

int ensamblador_leer(const char* ruta, Instruccion* programa, int* longitud) {
    FILE* f = fopen(ruta, "r");
    if (!f) return 1;

    char linea[512];
    char linea_orig[512];   /* copia intacta para buscar strings entre comillas */
    int len = 0;
    while (fgets(linea, sizeof(linea), f)) {
        char* p = trim(linea);
        if (*p == '\0' || *p == '#') continue;

        /* Guardar copia ANTES de que strtok mutile la cadena */
        strncpy(linea_orig, p, sizeof(linea_orig) - 1);
        linea_orig[sizeof(linea_orig) - 1] = '\0';

        char* token = strtok(p, " \t\n");
        if (!token) continue;

        Instruccion ins;
        memset(&ins, 0, sizeof(ins));

        if (strcmp(token, "MOVE") == 0 || strcmp(token, "MOVEJ") == 0 ||
            strcmp(token, "APPROACH") == 0) {
            ins.opcode = (strcmp(token, "MOVE") == 0) ? OP_MOVE :
                         (strcmp(token, "MOVEJ") == 0) ? OP_MOVEJ : OP_APPROACH;
            ins.arg1 = atoi(strtok(NULL, " \t\n"));
            ins.arg2 = atoi(strtok(NULL, " \t\n"));
            ins.arg3 = atoi(strtok(NULL, " \t\n"));
        } else if (strcmp(token, "DEPART") == 0) {
            ins.opcode = OP_DEPART;
            ins.arg1 = atoi(strtok(NULL, " \t\n"));
        } else if (strcmp(token, "HOME") == 0) {
            ins.opcode = OP_HOME;
        } else if (strcmp(token, "OPEN") == 0) {
            ins.opcode = OP_OPEN;
        } else if (strcmp(token, "CLOSE") == 0) {
            ins.opcode = OP_CLOSE;
        } else if (strcmp(token, "SPEED") == 0) {
            ins.opcode = OP_SPEED;
            ins.arg1 = atoi(strtok(NULL, " \t\n"));
        } else if (strcmp(token, "WAIT") == 0) {
            ins.opcode = OP_WAIT;
            ins.arg1 = atoi(strtok(NULL, " \t\n"));
        } else if (strcmp(token, "PRINT") == 0) {
            ins.opcode = OP_PRINT;
            char* msg = leer_cadena_entre_comillas(linea_orig);
            if (!msg) {
                fclose(f);
                return 1;
            }
            strncpy(ins.sval, msg, sizeof(ins.sval) - 1);
        } else if (strcmp(token, "VAR") == 0) {
            ins.opcode = OP_VAR;
            char* nombre = strtok(NULL, " \t\n");
            ins.arg1 = atoi(strtok(NULL, " \t\n"));
            strncpy(ins.sval, nombre, sizeof(ins.sval) - 1);
        } else if (strcmp(token, "POINT") == 0) {
            ins.opcode = OP_POINT;
            char* nombre = strtok(NULL, " \t\n");
            ins.arg1 = atoi(strtok(NULL, " \t\n"));
            ins.arg2 = atoi(strtok(NULL, " \t\n"));
            ins.arg3 = atoi(strtok(NULL, " \t\n"));
            strncpy(ins.sval, nombre, sizeof(ins.sval) - 1);
        } else if (strcmp(token, "ASSIGN") == 0) {
            ins.opcode = OP_ASSIGN;
            char* destino = strtok(NULL, " \t\n");
            strncpy(ins.sval, destino, sizeof(ins.sval) - 1);
            char* op1 = strtok(NULL, " \t\n");
            char* maybe_op = strtok(NULL, " \t\n");
            if (!op1) {
                fclose(f);
                return 1;
            }
            if (!maybe_op) {
                int tmp_val;
                int tmp_is_var;
                char tmp_name[64];
                parsear_operando(op1, &tmp_val, &tmp_is_var, tmp_name);
                if (tmp_is_var) {
                    ins.flags |= INS_F_ARG1_VAR;
                    strcpy(ins.sval2, tmp_name);
                } else {
                    ins.arg1 = tmp_val;
                }
                ins.arg2 = 0;
                ins.arg3 = 0;
            } else {
                // op1 <op> op2
                int tmp_val;
                int tmp_is_var;
                char tmp_name[64];
                parsear_operando(op1, &tmp_val, &tmp_is_var, tmp_name);
                if (tmp_is_var) {
                    ins.flags |= INS_F_ARG1_VAR;
                    strcpy(ins.sval2, tmp_name);
                } else {
                    ins.arg1 = tmp_val;
                }
                if (strcmp(maybe_op, "+") == 0) ins.arg2 = 1;
                else if (strcmp(maybe_op, "-") == 0) ins.arg2 = -1;
                else {
                    fclose(f);
                    return 1;
                }
                char* op2 = strtok(NULL, " \t\n");
                if (!op2) {
                    fclose(f);
                    return 1;
                }
                parsear_operando(op2, &tmp_val, &tmp_is_var, tmp_name);
                if (tmp_is_var) {
                    ins.flags |= INS_F_ARG3_VAR;
                    strcpy(ins.sval3, tmp_name);
                } else {
                    ins.arg3 = tmp_val;
                }
            }
        } else if (strcmp(token, "WHILE") == 0 || strcmp(token, "IF") == 0) {
            ins.opcode = (strcmp(token, "WHILE") == 0) ? OP_WHILE : OP_IF;
            char* op1 = strtok(NULL, " \t\n");
            char* op = strtok(NULL, " \t\n");
            char* op2 = strtok(NULL, " \t\n");
            if (!op1 || !op || !op2) {
                fclose(f);
                return 1;
            }
            int tmp_val;
            int tmp_is_var;
            char tmp_name[64];
            parsear_operando(op1, &tmp_val, &tmp_is_var, tmp_name);
            if (tmp_is_var) {
                ins.flags |= INS_F_ARG1_VAR;
                strcpy(ins.sval, tmp_name);
            } else {
                ins.arg1 = tmp_val;
            }
            ins.arg2 = operador_tipo(op);
            parsear_operando(op2, &tmp_val, &tmp_is_var, tmp_name);
            if (tmp_is_var) {
                ins.flags |= INS_F_ARG3_VAR;
                strcpy(ins.sval2, tmp_name);
            } else {
                ins.arg3 = tmp_val;
            }
        } else if (strcmp(token, "REPEAT") == 0) {
            ins.opcode = OP_REPEAT;
            ins.arg1 = atoi(strtok(NULL, " \t\n"));
        } else if (strcmp(token, "END_WHILE") == 0) {
            ins.opcode = OP_END_WHILE;
        } else if (strcmp(token, "ELSE") == 0) {
            ins.opcode = OP_ELSE;
        } else if (strcmp(token, "END_IF") == 0) {
            ins.opcode = OP_END_IF;
        } else if (strcmp(token, "END_REPEAT") == 0) {
            ins.opcode = OP_END_REPEAT;
        } else if (strcmp(token, "HALT") == 0) {
            ins.opcode = OP_HALT;
        } else {
            fclose(f);
            return 1;
        }

        programa[len++] = ins;
        if (len >= *longitud) break;
    }

    fclose(f);
    *longitud = len;
    return 0;
}