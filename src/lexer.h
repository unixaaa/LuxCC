#ifndef LEXER_H_
#define LEXER_H_

#include "pre.h"

typedef enum {
    /*
     * Punctuators:
     *   [ ] ( ) { } . ->
     *   ++ -- & * + - ~ !
     *   / % << >> < > <= >= == != ^ | && ||
     *   ? : ; ... ,
     *   = *= /= %= += -= <<= >>= &= ^= |=
     */
    TOK_LBRACKET,
    TOK_RBRACKET,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_DOT,
    TOK_ARROW,
    TOK_INC,
    TOK_DEC,
    TOK_AMPERSAND,
    TOK_ASTERISK,
    TOK_PLUS,
    TOK_MINUS,
    TOK_COMPLEMENT,
    TOK_NEGATION,
    TOK_DIV,
    TOK_MOD,
    TOK_LSHIFT,
    TOK_RSHIFT,
    TOK_LT,
    TOK_GT,
    TOK_LET,
    TOK_GET,
    TOK_EQ,
    TOK_NEQ,
    TOK_BW_XOR,
    TOK_BW_OR,
    TOK_AND,
    TOK_OR,
    TOK_CONDITIONAL,
    TOK_COLON,
    TOK_SEMICOLON,
    TOK_ELLIPSIS,
    TOK_COMMA,
    TOK_ASSIGN,
    TOK_MUL_ASSIGN,
    TOK_DIV_ASSIGN,
    TOK_MOD_ASSIGN,
    TOK_PLUS_ASSIGN,
    TOK_MINUS_ASSIGN,
    TOK_LSHIFT_ASSIGN,
    TOK_RSHIFT_ASSIGN,
    TOK_BW_AND_ASSIGN,
    TOK_BW_XOR_ASSIGN,
    TOK_BW_OR_ASSIGN,
    /*
     * Keywords.
     */
    TOK_AUTO,
    TOK_BREAK,
    TOK_CASE,
    TOK_CHAR,
    TOK_CONST,
    TOK_CONTINUE,
    TOK_DEFAULT,
    TOK_DO,
    TOK_DOUBLE,
    TOK_ELSE,
    TOK_ENUM,
    TOK_EXTERN,
    TOK_FLOAT,
    TOK_FOR,
    TOK_GOTO,
    TOK_IF,
    TOK_INLINE,
    TOK_INT,
    TOK_LONG,
    TOK_REGISTER,
    TOK_RESTRICT,
    TOK_RETURN,
    TOK_SHORT,
    TOK_SIGNED,
    TOK_SIZEOF,
    TOK_STATIC,
    TOK_STRUCT,
    TOK_SWITCH,
    TOK_TYPEDEF,
    TOK_UNION,
    TOK_UNSIGNED,
    TOK_VOID,
    TOK_VOLATILE,
    TOK_WHILE,
    /*
     * User defined.
     */
    TOK_ID,
    TOK_STRLIT,
    TOK_ICONST,
    // TOK_ERROR,
    TOK_EOF
} Token;

typedef struct TokenNode TokenNode;
struct TokenNode {
    Token token;
    char *lexeme, *file;
    TokenNode *next;
    int src_line;
};

TokenNode *lexer(PreTokenNode *pre_token_list);

#endif
