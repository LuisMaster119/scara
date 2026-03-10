#ifndef LEXER_H
#define LEXER_H

typedef enum {
    TOK_MOVE, TOK_MOVEJ, TOK_APPROACH, TOK_DEPART, TOK_HOME,
    TOK_OPEN, TOK_CLOSE,
    TOK_REPEAT, TOK_WHILE, TOK_IF, TOK_ELSE, TOK_END,
    TOK_WAIT, TOK_PROGRAM,
    TOK_VAR, TOK_POINT, TOK_SPEED, TOK_PRINT,
    TOK_ASSIGN, TOK_PLUS, TOK_MINUS, TOK_LESS, TOK_GREATER, TOK_EQUAL,
    TOK_NUMBER, TOK_STRING, TOK_IDENT,
    TOK_EOF, TOK_UNKNOWN
} TipoToken;

typedef struct {
    TipoToken tipo;
    char      valor[64];
    int       linea;
} Token;

typedef struct {
    const char* fuente;
    int         pos;
    int         linea;
} Lexer;

void  lexer_init(Lexer* lex, const char* fuente);
Token lexer_siguiente(Lexer* lex);

#endif
