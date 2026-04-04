#ifndef PARSER_H
#define PARSER_H

#include "lexer.h"

// --- Opcodes ---------------------------------------------

typedef enum {
    OP_MOVE,    OP_MOVEJ,   OP_APPROACH, OP_DEPART,  OP_HOME,
    OP_OPEN,    OP_CLOSE,
    OP_SPEED,   OP_WAIT,    OP_PRINT,
    OP_WHILE,   OP_END_WHILE,
    OP_IF,      OP_ELSE,    OP_END_IF,
    OP_REPEAT,  OP_END_REPEAT,
    OP_VAR,     OP_POINT,   OP_ASSIGN,
    OP_HALT
} Opcode;

// --- Instruccion de bytecode ------------------------------

typedef struct {
    Opcode opcode;
    int    arg1;
    int    arg2;
    int    arg3;
    int    flags;
    char   sval[64];
    char   sval2[64];
    char   sval3[64];
} Instruccion;

#define INS_F_ARG1_VAR 1
#define INS_F_ARG3_VAR 2

// --- Estado del Parser ------------------------------------

typedef struct {
    Lexer* lex;
    Token  actual;
    Token  siguiente;
} Parser;

// --- Bytecode generado ------------------------------------

#define MAX_INSTRUCCIONES 256
extern Instruccion bytecode[MAX_INSTRUCCIONES];
extern int         bytecode_len;

// --- Funciones publicas -----------------------------------

void parser_init(Parser* p, Lexer* lex);
void parsear_programa(Parser* p);
const char* opcode_a_texto(Opcode op);

#endif
