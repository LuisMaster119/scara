#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "lexer.h"
#include "errores.h"

// --- Tabla de palabras clave ------------------------------

typedef struct { const char* palabra; TipoToken tipo; } Keyword;

Keyword keywords[] = {
    {"MOVE",     TOK_MOVE},    {"MOVEJ",   TOK_MOVEJ},
    {"APPROACH", TOK_APPROACH},{"DEPART",  TOK_DEPART},
    {"HOME",     TOK_HOME},    {"OPEN",    TOK_OPEN},
    {"CLOSE",    TOK_CLOSE},   {"REPEAT",  TOK_REPEAT},
    {"WHILE",    TOK_WHILE},   {"IF",      TOK_IF},
    {"ELSE",     TOK_ELSE},    {"END",     TOK_END},
    {"WAIT",     TOK_WAIT},    {"PROGRAM", TOK_PROGRAM},
    {"VAR",      TOK_VAR},     {"POINT",   TOK_POINT},
    {"SPEED",    TOK_SPEED},   {"PRINT",   TOK_PRINT},
    {NULL, TOK_UNKNOWN}
};

// --- Funciones auxiliares ---------------------------------

void lexer_init(Lexer* lex, const char* fuente) {
    lex->fuente = fuente;
    lex->pos    = 0;
    lex->linea  = 1;
}

char lexer_peek(Lexer* lex) {
    return lex->fuente[lex->pos];
}

char lexer_avanzar(Lexer* lex) {
    char c = lex->fuente[lex->pos++];
    if (c == '\n') lex->linea++;
    return c;
}

void lexer_saltar_espacios(Lexer* lex) {
    while (isspace(lexer_peek(lex)))
        lexer_avanzar(lex);
}

TipoToken buscar_keyword(const char* palabra) {
    for (int i = 0; keywords[i].palabra != NULL; i++)
        if (strcmp(keywords[i].palabra, palabra) == 0)
            return keywords[i].tipo;
    return TOK_IDENT;
}

// --- Funcion principal ------------------------------------

Token lexer_siguiente(Lexer* lex) {
    Token tok;
    tok.linea = lex->linea;

    lexer_saltar_espacios(lex);
    char c = lexer_peek(lex);

    // fin de archivo
    if (c == '\0') {
        tok.tipo = TOK_EOF;
        strcpy(tok.valor, "EOF");
        return tok;
    }

    // comentarios
    if (c == '#') {
        while (lexer_peek(lex) != '\n' && lexer_peek(lex) != '\0')
            lexer_avanzar(lex);
        return lexer_siguiente(lex);
    }

    // palabras clave e identificadores
    if (isalpha(c) || c == '_') {
        int i = 0;
        while (isalnum(lexer_peek(lex)) || lexer_peek(lex) == '_')
            tok.valor[i++] = lexer_avanzar(lex);
        tok.valor[i] = '\0';
        tok.tipo = buscar_keyword(tok.valor);
        return tok;
    }

    // numeros
    if (isdigit(c) || (c == '-' && isdigit(lex->fuente[lex->pos + 1]))) {
        int i = 0;
        tok.valor[i++] = lexer_avanzar(lex);
        while (isdigit(lexer_peek(lex)))
            tok.valor[i++] = lexer_avanzar(lex);
        tok.valor[i] = '\0';
        tok.tipo = TOK_NUMBER;
        return tok;
    }

    // strings
    if (c == '"') {
        lexer_avanzar(lex);
        int i = 0;
        while (lexer_peek(lex) != '"' && lexer_peek(lex) != '\0')
            tok.valor[i++] = lexer_avanzar(lex);
        lexer_avanzar(lex);
        tok.valor[i] = '\0';
        tok.tipo = TOK_STRING;
        return tok;
    }

    // operadores
    lexer_avanzar(lex);
    tok.valor[0] = c;
    tok.valor[1] = '\0';

    switch (c) {
        case '=':
            if (lexer_peek(lex) == '=') {
                lexer_avanzar(lex);
                strcpy(tok.valor, "==");
                tok.tipo = TOK_EQUAL;
            } else {
                tok.tipo = TOK_ASSIGN;
            }
            break;
        case '+': tok.tipo = TOK_PLUS;    break;
        case '-': tok.tipo = TOK_MINUS;   break;
        case '<': tok.tipo = TOK_LESS;    break;
        case '>': tok.tipo = TOK_GREATER; break;
        default:
            // error lexico: caracter desconocido
            error_reportar(ERR_LEXICO, lex->linea, tok.valor,
                "Caracter desconocido");
            tok.tipo = TOK_UNKNOWN;
            break;
    }
    return tok;
}
