/*
 * Agim - Parser Implementation
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "lang/parser.h"
#include "util/alloc.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Parser Structure */

/* Maximum recursion depth for expression parsing (prevents stack overflow) */
#define MAX_PARSE_DEPTH 256

struct Parser {
    Lexer *lexer;
    Token current;
    Token previous;
    char *error;
    int error_line;
    bool had_error;
    bool panic_mode;
    int depth;  /* Current recursion depth */
};

/* Error Handling */

static void error_at(Parser *parser, Token *token, const char *message) {
    if (parser->panic_mode) return;
    parser->panic_mode = true;
    parser->had_error = true;

    /* Calculate required buffer size to avoid overflow */
    size_t msg_len = message ? strlen(message) : 0;
    size_t token_len = token->length < 100 ? token->length : 100; /* Limit token in message */
    size_t needed = 50 + msg_len + token_len; /* Generous buffer for formatting */

    parser->error = agim_alloc(needed);
    if (!parser->error) {
        parser->error_line = token->line;
        return;
    }

    if (token->type == TOK_EOF) {
        snprintf(parser->error, needed, "line %d: at end: %s",
                 token->line, message ? message : "");
    } else if (token->type == TOK_ERROR) {
        snprintf(parser->error, needed, "line %d: %.*s",
                 token->line, (int)token_len, token->start);
    } else {
        snprintf(parser->error, needed, "line %d: at '%.*s': %s",
                 token->line, (int)token_len, token->start, message ? message : "");
    }

    parser->error_line = token->line;
}

static void error(Parser *parser, const char *message) {
    error_at(parser, &parser->previous, message);
}

static void error_at_current(Parser *parser, const char *message) {
    error_at(parser, &parser->current, message);
}

/* Token Handling */

static void advance(Parser *parser) {
    parser->previous = parser->current;

    for (;;) {
        parser->current = lexer_next(parser->lexer);
        if (parser->current.type != TOK_ERROR) break;
        error_at_current(parser, parser->current.start);
    }
}

static bool check(Parser *parser, TokenType type) {
    return parser->current.type == type;
}

static bool match(Parser *parser, TokenType type) {
    if (!check(parser, type)) return false;
    advance(parser);
    return true;
}

static void consume(Parser *parser, TokenType type, const char *message) {
    if (parser->current.type == type) {
        advance(parser);
        return;
    }
    error_at_current(parser, message);
}

static void skip_newlines(Parser *parser) {
    while (match(parser, TOK_NEWLINE)) {
        /* Skip */
    }
}

static void synchronize(Parser *parser) {
    parser->panic_mode = false;

    while (parser->current.type != TOK_EOF) {
        if (parser->previous.type == TOK_NEWLINE) return;

        switch (parser->current.type) {
        case TOK_TOOL:
        case TOK_FN:
        case TOK_LET:
        case TOK_CONST:
        case TOK_IF:
        case TOK_WHILE:
        case TOK_FOR:
        case TOK_RETURN:
        case TOK_IMPORT:
        case TOK_EXPORT:
        case TOK_MATCH:
        case TOK_STRUCT:
        case TOK_ENUM:
        case TOK_ALIAS:
            return;
        default:
            break;
        }

        advance(parser);
    }
}

/* Operator Precedence */

typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT,    /* = += -= */
    PREC_RANGE,         /* .. ..= */
    PREC_TERNARY,       /* ?: */
    PREC_OR,            /* or */
    PREC_AND,           /* and */
    PREC_EQUALITY,      /* == != */
    PREC_COMPARISON,    /* < > <= >= */
    PREC_TERM,          /* + - */
    PREC_FACTOR,        /* * / % */
    PREC_UNARY,         /* not - */
    PREC_CALL,          /* . () [] */
    PREC_PRIMARY,
} Precedence;

static Precedence get_precedence(TokenType type) {
    switch (type) {
    case TOK_ASSIGN:
    case TOK_PLUS_ASSIGN:
    case TOK_MINUS_ASSIGN:
    case TOK_STAR_ASSIGN:
    case TOK_SLASH_ASSIGN:
        return PREC_ASSIGNMENT;
    case TOK_QUESTION:
        return PREC_TERNARY;
    case TOK_OR:
        return PREC_OR;
    case TOK_AND:
        return PREC_AND;
    case TOK_EQ:
    case TOK_NE:
        return PREC_EQUALITY;
    case TOK_LT:
    case TOK_LE:
    case TOK_GT:
    case TOK_GE:
        return PREC_COMPARISON;
    case TOK_PLUS:
    case TOK_MINUS:
        return PREC_TERM;
    case TOK_STAR:
    case TOK_SLASH:
    case TOK_PERCENT:
        return PREC_FACTOR;
    case TOK_RANGE:
    case TOK_RANGE_INCL:
        return PREC_RANGE;
    case TOK_LPAREN:
    case TOK_DOT:
    case TOK_LBRACKET:
    case TOK_LBRACE:
        return PREC_CALL;
    default:
        return PREC_NONE;
    }
}

/* Forward Declarations */

static AstNode *parse_expression(Parser *parser);
static AstNode *parse_precedence(Parser *parser, Precedence min_prec);
static AstNode *parse_statement(Parser *parser);
static AstNode *parse_block(Parser *parser);
static AstNode *parse_match_expr(Parser *parser);
static AstNode *parse_try_expr(Parser *parser);
static AstNode *parse_ok_expr(Parser *parser);
static AstNode *parse_err_expr(Parser *parser);
static AstNode *parse_some_expr(Parser *parser);
static AstNode *parse_fn_decl(Parser *parser, bool is_tool);
static AstNode *parse_let_stmt(Parser *parser, bool is_const);
static AstNode *parse_type(Parser *parser);
static AstNode *parse_struct_decl(Parser *parser);
static AstNode *parse_enum_decl(Parser *parser);
static AstNode *parse_type_alias(Parser *parser);

/* Expression Parsing */

static AstNode *parse_number(Parser *parser) {
    Token token = parser->previous;

    /* Remove underscores for parsing */
    char buffer[64];
    size_t j = 0;
    for (size_t i = 0; i < token.length && j < sizeof(buffer) - 1; i++) {
        if (token.start[i] != '_') {
            buffer[j++] = token.start[i];
        }
    }
    buffer[j] = '\0';

    if (token.type == TOK_FLOAT) {
        double value = strtod(buffer, NULL);
        return ast_float(value, token.line);
    } else {
        int64_t value = strtoll(buffer, NULL, 10);
        return ast_int(value, token.line);
    }
}

static AstNode *parse_string(Parser *parser) {
    Token token = parser->previous;
    /* Skip opening and closing quotes */
    return ast_string(token.start + 1, token.length - 2, token.line);
}

static AstNode *parse_identifier(Parser *parser) {
    Token token = parser->previous;

    /* Check for enum variant syntax: EnumType::Variant */
    if (match(parser, TOK_COLON_COLON)) {
        char enum_type[256];
        size_t type_len = token.length < 255 ? token.length : 255;
        memcpy(enum_type, token.start, type_len);
        enum_type[type_len] = '\0';

        consume(parser, TOK_IDENT, "expected variant name after '::'");
        char variant_name[256];
        size_t var_len = parser->previous.length < 255 ? parser->previous.length : 255;
        memcpy(variant_name, parser->previous.start, var_len);
        variant_name[var_len] = '\0';

        /* Check for payload: EnumType::Variant(payload) */
        AstNode *payload = NULL;
        if (match(parser, TOK_LPAREN)) {
            payload = parse_expression(parser);
            consume(parser, TOK_RPAREN, "expected ')' after enum payload");
        }

        return ast_enum_variant(enum_type, variant_name, payload, token.line);
    }

    return ast_ident(token.start, token.length, token.line);
}

static AstNode *parse_grouping(Parser *parser) {
    AstNode *expr = parse_expression(parser);
    consume(parser, TOK_RPAREN, "expected ')' after expression");
    return expr;
}

static AstNode *parse_unary(Parser *parser) {
    TokenType op = parser->previous.type;
    int line = parser->previous.line;

    AstNode *operand = parse_precedence(parser, PREC_UNARY);
    return ast_unary(op, operand, line);
}

static AstNode *parse_array_literal(Parser *parser) {
    int line = parser->previous.line;
    AstNode *node = ast_new(NODE_ARRAY, line);
    node->as.array.elements = NULL;
    node->as.array.count = 0;

    skip_newlines(parser);

    if (!check(parser, TOK_RBRACKET)) {
        size_t capacity = 8;
        node->as.array.elements = agim_alloc(sizeof(AstNode *) * capacity);

        do {
            skip_newlines(parser);
            if (check(parser, TOK_RBRACKET)) break;

            AstNode *elem = parse_expression(parser);
            if (node->as.array.count >= capacity) {
                if (capacity > SIZE_MAX / 2) {
                    error(parser, "array literal too large");
                    ast_free(node);
                    return NULL;
                }
                capacity *= 2;
                node->as.array.elements = agim_realloc(
                    node->as.array.elements,
                    sizeof(AstNode *) * capacity
                );
            }
            node->as.array.elements[node->as.array.count++] = elem;

            skip_newlines(parser);
        } while (match(parser, TOK_COMMA));
    }

    skip_newlines(parser);
    consume(parser, TOK_RBRACKET, "expected ']' after array elements");
    return node;
}

static AstNode *parse_map_literal(Parser *parser) {
    int line = parser->previous.line;
    AstNode *node = ast_new(NODE_MAP, line);
    node->as.map.keys = NULL;
    node->as.map.values = NULL;
    node->as.map.count = 0;

    skip_newlines(parser);

    if (!check(parser, TOK_RBRACE)) {
        size_t capacity = 8;
        node->as.map.keys = agim_alloc(sizeof(char *) * capacity);
        node->as.map.values = agim_alloc(sizeof(AstNode *) * capacity);

        do {
            skip_newlines(parser);
            if (check(parser, TOK_RBRACE)) break;

            /* Key can be identifier or string */
            char *key = NULL;
            if (match(parser, TOK_IDENT)) {
                key = agim_alloc(parser->previous.length + 1);
                memcpy(key, parser->previous.start, parser->previous.length);
                key[parser->previous.length] = '\0';
            } else if (match(parser, TOK_STRING)) {
                size_t len = parser->previous.length - 2;
                key = agim_alloc(len + 1);
                memcpy(key, parser->previous.start + 1, len);
                key[len] = '\0';
            } else {
                error(parser, "expected map key");
                ast_free(node);
                return NULL;
            }

            consume(parser, TOK_COLON, "expected ':' after map key");
            skip_newlines(parser);

            AstNode *value = parse_expression(parser);

            if (node->as.map.count >= capacity) {
                if (capacity > SIZE_MAX / 2) {
                    error(parser, "map literal too large");
                    ast_free(node);
                    return NULL;
                }
                capacity *= 2;
                node->as.map.keys = agim_realloc(node->as.map.keys, sizeof(char *) * capacity);
                node->as.map.values = agim_realloc(node->as.map.values, sizeof(AstNode *) * capacity);
            }

            node->as.map.keys[node->as.map.count] = key;
            node->as.map.values[node->as.map.count] = value;
            node->as.map.count++;

            skip_newlines(parser);
        } while (match(parser, TOK_COMMA));
    }

    skip_newlines(parser);
    consume(parser, TOK_RBRACE, "expected '}' after map entries");
    return node;
}

static AstNode *parse_primary(Parser *parser) {
    if (match(parser, TOK_TRUE)) return ast_bool(true, parser->previous.line);
    if (match(parser, TOK_FALSE)) return ast_bool(false, parser->previous.line);
    if (match(parser, TOK_NIL)) return ast_nil(parser->previous.line);

    if (match(parser, TOK_INT) || match(parser, TOK_FLOAT)) {
        return parse_number(parser);
    }

    if (match(parser, TOK_STRING)) {
        return parse_string(parser);
    }

    if (match(parser, TOK_IDENT)) {
        return parse_identifier(parser);
    }

    if (match(parser, TOK_LPAREN)) {
        return parse_grouping(parser);
    }

    if (match(parser, TOK_LBRACKET)) {
        return parse_array_literal(parser);
    }

    if (match(parser, TOK_LBRACE)) {
        return parse_map_literal(parser);
    }

    if (match(parser, TOK_NOT) || match(parser, TOK_MINUS)) {
        return parse_unary(parser);
    }

    if (match(parser, TOK_MATCH)) {
        return parse_match_expr(parser);
    }

    if (match(parser, TOK_TRY)) {
        return parse_try_expr(parser);
    }

    if (match(parser, TOK_OK)) {
        return parse_ok_expr(parser);
    }

    if (match(parser, TOK_ERR)) {
        return parse_err_expr(parser);
    }

    if (match(parser, TOK_SOME)) {
        return parse_some_expr(parser);
    }

    if (match(parser, TOK_NONE)) {
        return ast_none(parser->previous.line);
    }

    if (match(parser, TOK_SPREAD)) {
        int line = parser->previous.line;
        AstNode *expr = parse_precedence(parser, PREC_UNARY);
        return ast_spread(expr, line);
    }

    error_at_current(parser, "expected expression");
    return NULL;
}

static AstNode *parse_call(Parser *parser, AstNode *callee) {
    int line = parser->previous.line;
    AstNode *node = ast_new(NODE_CALL, line);
    node->as.call.callee = callee;
    node->as.call.args = NULL;
    node->as.call.arg_count = 0;

    skip_newlines(parser);

    if (!check(parser, TOK_RPAREN)) {
        size_t capacity = 8;
        node->as.call.args = agim_alloc(sizeof(AstNode *) * capacity);

        do {
            skip_newlines(parser);
            AstNode *arg = parse_expression(parser);
            if (node->as.call.arg_count >= capacity) {
                if (capacity > SIZE_MAX / 2) {
                    error(parser, "too many function arguments");
                    ast_free(node);
                    return NULL;
                }
                capacity *= 2;
                node->as.call.args = agim_realloc(
                    node->as.call.args,
                    sizeof(AstNode *) * capacity
                );
            }
            node->as.call.args[node->as.call.arg_count++] = arg;
            skip_newlines(parser);
        } while (match(parser, TOK_COMMA));
    }

    consume(parser, TOK_RPAREN, "expected ')' after arguments");
    return node;
}

static AstNode *parse_member(Parser *parser, AstNode *object) {
    int line = parser->previous.line;
    consume(parser, TOK_IDENT, "expected property name after '.'");

    AstNode *node = ast_new(NODE_MEMBER, line);
    node->as.member.object = object;
    node->as.member.field = agim_alloc(parser->previous.length + 1);
    memcpy(node->as.member.field, parser->previous.start, parser->previous.length);
    node->as.member.field[parser->previous.length] = '\0';
    return node;
}

static AstNode *parse_index(Parser *parser, AstNode *object) {
    int line = parser->previous.line;
    AstNode *index = parse_expression(parser);
    consume(parser, TOK_RBRACKET, "expected ']' after index");

    AstNode *node = ast_new(NODE_INDEX, line);
    node->as.index_expr.object = object;
    node->as.index_expr.index = index;
    return node;
}

static AstNode *parse_struct_init(Parser *parser, AstNode *type_node) {
    /* type_node is the identifier for the struct type name */
    int line = parser->previous.line;

    /* Extract type name from identifier node */
    char type_name[256];
    size_t len = strlen(type_node->as.ident.name);
    if (len > 255) len = 255;
    memcpy(type_name, type_node->as.ident.name, len);
    type_name[len] = '\0';

    /* Free the identifier node since we're replacing it */
    ast_free(type_node);

    AstNode *node = ast_struct_init(type_name, line);

    skip_newlines(parser);

    /* Parse field initializers: name: value, ... */
    while (!check(parser, TOK_RBRACE) && !check(parser, TOK_EOF)) {
        skip_newlines(parser);
        if (check(parser, TOK_RBRACE)) break;

        /* Check for spread: ...expr */
        if (match(parser, TOK_SPREAD)) {
            AstNode *spread_expr = parse_expression(parser);
            ast_struct_init_set_spread(node, spread_expr);
            skip_newlines(parser);
            if (!check(parser, TOK_RBRACE)) {
                match(parser, TOK_COMMA);
            }
            continue;
        }

        /* Parse field: name: value */
        consume(parser, TOK_IDENT, "expected field name");
        char field_name[256];
        size_t field_len = parser->previous.length < 255 ? parser->previous.length : 255;
        memcpy(field_name, parser->previous.start, field_len);
        field_name[field_len] = '\0';

        consume(parser, TOK_COLON, "expected ':' after field name");
        skip_newlines(parser);

        AstNode *value = parse_expression(parser);
        ast_struct_init_add_field(node, field_name, value);

        skip_newlines(parser);
        if (!check(parser, TOK_RBRACE)) {
            if (!match(parser, TOK_COMMA)) {
                skip_newlines(parser);
            }
        }
    }

    consume(parser, TOK_RBRACE, "expected '}' after struct fields");
    return node;
}

static AstNode *parse_infix(Parser *parser, AstNode *left, TokenType op) {
    int line = parser->previous.line;
    Precedence prec = get_precedence(op);

    /* Handle assignment */
    if (op == TOK_ASSIGN || op == TOK_PLUS_ASSIGN || op == TOK_MINUS_ASSIGN ||
        op == TOK_STAR_ASSIGN || op == TOK_SLASH_ASSIGN) {
        AstNode *value = parse_precedence(parser, prec);
        AstNode *node = ast_new(NODE_ASSIGN, line);
        node->as.assign.target = left;
        node->as.assign.op = op;
        node->as.assign.value = value;
        return node;
    }

    /* Handle ternary */
    if (op == TOK_QUESTION) {
        AstNode *then_expr = parse_expression(parser);
        consume(parser, TOK_COLON, "expected ':' in ternary expression");
        AstNode *else_expr = parse_precedence(parser, prec);
        AstNode *node = ast_new(NODE_TERNARY, line);
        node->as.ternary.cond = left;
        node->as.ternary.then_expr = then_expr;
        node->as.ternary.else_expr = else_expr;
        return node;
    }

    /* Handle range */
    if (op == TOK_RANGE || op == TOK_RANGE_INCL) {
        AstNode *end = parse_precedence(parser, prec + 1);
        bool inclusive = (op == TOK_RANGE_INCL);
        return ast_range(left, end, inclusive, line);
    }

    /* Binary operators */
    AstNode *right = parse_precedence(parser, prec + 1);
    return ast_binary(op, left, right, line);
}

static AstNode *parse_precedence(Parser *parser, Precedence min_prec) {
    /* Check recursion depth to prevent stack overflow */
    if (++parser->depth > MAX_PARSE_DEPTH) {
        error(parser, "expression too deeply nested");
        parser->depth--;
        return NULL;
    }

    AstNode *left = parse_primary(parser);
    if (!left) {
        parser->depth--;
        return NULL;
    }

    while (!parser->had_error) {
        TokenType op = parser->current.type;
        Precedence prec = get_precedence(op);

        if (prec < min_prec) break;

        /* Special case: { only applies as struct init when following a type name (uppercase identifier) */
        if (op == TOK_LBRACE) {
            if (left->type != NODE_IDENT) {
                break;  /* Not struct init, { is start of something else (e.g., match body) */
            }
            /* Check if identifier looks like a type name (starts with uppercase) */
            const char *name = left->as.ident.name;
            if (name[0] < 'A' || name[0] > 'Z') {
                break;  /* Lowercase identifier, not a type name */
            }
        }

        advance(parser);

        /* Postfix operators */
        if (op == TOK_LPAREN) {
            left = parse_call(parser, left);
        } else if (op == TOK_DOT) {
            left = parse_member(parser, left);
        } else if (op == TOK_LBRACKET) {
            left = parse_index(parser, left);
        } else if (op == TOK_LBRACE) {
            /* Struct initialization: TypeName { field: value, ... } */
            /* We already checked left->type == NODE_IDENT above */
            left = parse_struct_init(parser, left);
        } else {
            left = parse_infix(parser, left, op);
        }

        if (!left) {
            parser->depth--;
            return NULL;
        }
    }

    parser->depth--;
    return left;
}

static AstNode *parse_expression(Parser *parser) {
    return parse_precedence(parser, PREC_ASSIGNMENT);
}

/* Type Parsing */
static AstNode *parse_type(Parser *parser) {
    /* Check recursion depth to prevent stack overflow from deeply nested types */
    if (++parser->depth > MAX_PARSE_DEPTH) {
        error(parser, "type nesting too deep");
        parser->depth--;
        return NULL;
    }

    int line = parser->current.line;
    AstNode *result = NULL;

    /* Array type: [T] */
    if (match(parser, TOK_LBRACKET)) {
        AstNode *elem_type = parse_type(parser);
        consume(parser, TOK_RBRACKET, "expected ']' after array element type");
        result = ast_type_array(elem_type, line);
        goto done;
    }

    /* Function type: fn(A, B) -> C */
    if (match(parser, TOK_FN)) {
        consume(parser, TOK_LPAREN, "expected '(' in function type");

        AstNode **param_types = NULL;
        size_t param_count = 0;
        size_t capacity = 0;

        if (!check(parser, TOK_RPAREN)) {
            capacity = 4;
            param_types = agim_alloc(sizeof(AstNode *) * capacity);

            do {
                AstNode *ptype = parse_type(parser);
                if (param_count >= capacity) {
                    if (capacity > SIZE_MAX / 2) {
                        error(parser, "too many function type parameters");
                        result = NULL;
                        goto done;
                    }
                    capacity *= 2;
                    param_types = agim_realloc(param_types, sizeof(AstNode *) * capacity);
                }
                param_types[param_count++] = ptype;
            } while (match(parser, TOK_COMMA));
        }

        consume(parser, TOK_RPAREN, "expected ')' after function parameter types");

        AstNode *return_type = NULL;
        if (match(parser, TOK_ARROW)) {
            return_type = parse_type(parser);
        }

        result = ast_type_func(param_types, param_count, return_type, line);
        goto done;
    }

    /* Built-in type names or identifiers */
    const char *type_name = NULL;
    size_t type_len = 0;

    if (match(parser, TOK_TYPE_INT)) {
        type_name = "int";
        type_len = 3;
    } else if (match(parser, TOK_TYPE_FLOAT)) {
        type_name = "float";
        type_len = 5;
    } else if (match(parser, TOK_TYPE_STRING)) {
        type_name = "string";
        type_len = 6;
    } else if (match(parser, TOK_TYPE_BOOL)) {
        type_name = "bool";
        type_len = 4;
    } else if (match(parser, TOK_TYPE_VOID)) {
        type_name = "void";
        type_len = 4;
    } else if (match(parser, TOK_TYPE_BYTES)) {
        type_name = "bytes";
        type_len = 5;
    } else if (match(parser, TOK_TYPE_PID)) {
        type_name = "Pid";
        type_len = 3;
    } else if (match(parser, TOK_TYPE_OPTION)) {
        /* Option<T> */
        if (!match(parser, TOK_LT)) {
            error(parser, "expected '<' after Option");
            result = NULL;
            goto done;
        }
        AstNode *inner = parse_type(parser);
        consume(parser, TOK_GT, "expected '>' after Option type parameter");

        AstNode **type_args = agim_alloc(sizeof(AstNode *));
        type_args[0] = inner;
        result = ast_type_generic("Option", type_args, 1, line);
        goto done;
    } else if (match(parser, TOK_TYPE_RESULT)) {
        /* Result<T, E> */
        if (!match(parser, TOK_LT)) {
            error(parser, "expected '<' after Result");
            result = NULL;
            goto done;
        }
        AstNode *ok_type = parse_type(parser);
        consume(parser, TOK_COMMA, "expected ',' in Result<T, E>");
        AstNode *err_type = parse_type(parser);
        consume(parser, TOK_GT, "expected '>' after Result type parameters");

        AstNode **type_args = agim_alloc(sizeof(AstNode *) * 2);
        type_args[0] = ok_type;
        type_args[1] = err_type;
        result = ast_type_generic("Result", type_args, 2, line);
        goto done;
    } else if (match(parser, TOK_TYPE_MAP)) {
        /* map<K, V> */
        if (!match(parser, TOK_LT)) {
            error(parser, "expected '<' after map");
            result = NULL;
            goto done;
        }
        AstNode *key_type = parse_type(parser);
        consume(parser, TOK_COMMA, "expected ',' in map<K, V>");
        AstNode *val_type = parse_type(parser);
        consume(parser, TOK_GT, "expected '>' after map type parameters");

        result = ast_type_map(key_type, val_type, line);
        goto done;
    } else if (match(parser, TOK_IDENT)) {
        type_name = parser->previous.start;
        type_len = parser->previous.length;

        /* Check for generic: TypeName<T, ...> */
        if (match(parser, TOK_LT)) {
            char *name = agim_alloc(type_len + 1);
            memcpy(name, type_name, type_len);
            name[type_len] = '\0';

            AstNode **type_args = NULL;
            size_t arg_count = 0;
            size_t capacity = 4;
            type_args = agim_alloc(sizeof(AstNode *) * capacity);

            do {
                AstNode *arg = parse_type(parser);
                if (arg_count >= capacity) {
                    if (capacity > SIZE_MAX / 2) {
                        error(parser, "too many generic type arguments");
                        result = NULL;
                        goto done;
                    }
                    capacity *= 2;
                    type_args = agim_realloc(type_args, sizeof(AstNode *) * capacity);
                }
                type_args[arg_count++] = arg;
            } while (match(parser, TOK_COMMA));

            consume(parser, TOK_GT, "expected '>' after generic type parameters");

            AstNode *node = ast_type_generic(name, type_args, arg_count, line);
            agim_free(name);
            result = node;
            goto done;
        }
    } else {
        error_at_current(parser, "expected type");
        result = NULL;
        goto done;
    }

    result = ast_type_name(type_name, type_len, line);

done:
    parser->depth--;
    return result;
}

/* Statement Parsing */

static AstNode *parse_let_stmt(Parser *parser, bool is_const) {
    int line = parser->previous.line;

    /* Check for 'mut' keyword after 'let' */
    bool is_mutable = false;
    if (!is_const && match(parser, TOK_MUT)) {
        is_mutable = true;
    }

    consume(parser, TOK_IDENT, "expected variable name");

    char *name = agim_alloc(parser->previous.length + 1);
    memcpy(name, parser->previous.start, parser->previous.length);
    name[parser->previous.length] = '\0';

    /* Optional type annotation: name: Type */
    AstNode *type_ann = NULL;
    if (match(parser, TOK_COLON)) {
        type_ann = parse_type(parser);
    }

    consume(parser, TOK_ASSIGN, "expected '=' after variable name");

    AstNode *value = parse_expression(parser);

    AstNode *node = ast_new(is_const ? NODE_CONST : NODE_LET, line);
    node->as.var_decl.name = name;
    node->as.var_decl.type_ann = type_ann;
    node->as.var_decl.value = value;
    node->as.var_decl.is_mutable = is_mutable;
    return node;
}

static AstNode *parse_if_stmt(Parser *parser) {
    int line = parser->previous.line;

    AstNode *cond = parse_expression(parser);
    AstNode *then_block = parse_block(parser);

    AstNode *else_block = NULL;
    skip_newlines(parser);
    if (match(parser, TOK_ELSE)) {
        skip_newlines(parser);
        if (match(parser, TOK_IF)) {
            else_block = parse_if_stmt(parser);
        } else {
            else_block = parse_block(parser);
        }
    }

    AstNode *node = ast_new(NODE_IF, line);
    node->as.if_stmt.cond = cond;
    node->as.if_stmt.then_block = then_block;
    node->as.if_stmt.else_block = else_block;
    return node;
}

static AstNode *parse_while_stmt(Parser *parser) {
    int line = parser->previous.line;

    AstNode *cond = parse_expression(parser);
    AstNode *body = parse_block(parser);

    AstNode *node = ast_new(NODE_WHILE, line);
    node->as.while_stmt.cond = cond;
    node->as.while_stmt.body = body;
    return node;
}

static AstNode *parse_for_stmt(Parser *parser) {
    int line = parser->previous.line;

    consume(parser, TOK_IDENT, "expected variable name");
    char *var = agim_alloc(parser->previous.length + 1);
    memcpy(var, parser->previous.start, parser->previous.length);
    var[parser->previous.length] = '\0';

    char *index_var = NULL;
    if (match(parser, TOK_COMMA)) {
        consume(parser, TOK_IDENT, "expected index variable name");
        index_var = agim_alloc(parser->previous.length + 1);
        memcpy(index_var, parser->previous.start, parser->previous.length);
        index_var[parser->previous.length] = '\0';
    }

    consume(parser, TOK_IN, "expected 'in' after variable");

    AstNode *iterable = parse_expression(parser);
    AstNode *body = parse_block(parser);

    AstNode *node = ast_new(NODE_FOR, line);
    node->as.for_stmt.var = var;
    node->as.for_stmt.index_var = index_var;
    node->as.for_stmt.iterable = iterable;
    node->as.for_stmt.body = body;
    return node;
}

static AstNode *parse_return_stmt(Parser *parser) {
    int line = parser->previous.line;
    AstNode *value = NULL;

    /* Check if there's a value on the same line */
    if (!check(parser, TOK_NEWLINE) && !check(parser, TOK_RBRACE) && !check(parser, TOK_EOF)) {
        value = parse_expression(parser);
    }

    AstNode *node = ast_new(NODE_RETURN, line);
    node->as.return_stmt.value = value;
    return node;
}

static AstNode *parse_block(Parser *parser) {
    skip_newlines(parser);
    consume(parser, TOK_LBRACE, "expected '{'");
    skip_newlines(parser);

    AstNode *block = ast_block(parser->previous.line);

    while (!check(parser, TOK_RBRACE) && !check(parser, TOK_EOF)) {
        AstNode *stmt = parse_statement(parser);
        if (stmt) {
            ast_block_add(block, stmt);
        }
        skip_newlines(parser);
    }

    consume(parser, TOK_RBRACE, "expected '}'");
    return block;
}

/* Match Expression Parsing */

static AstNode *parse_match_expr(Parser *parser) {
    int line = parser->previous.line;

    AstNode *expr = parse_expression(parser);
    skip_newlines(parser);
    consume(parser, TOK_LBRACE, "expected '{' after match expression");
    skip_newlines(parser);

    AstNode *node = ast_new(NODE_MATCH, line);
    node->as.match_expr.expr = expr;
    node->as.match_expr.arms = NULL;
    node->as.match_expr.arm_count = 0;

    size_t capacity = 4;
    node->as.match_expr.arms = agim_alloc(sizeof(AstNode *) * capacity);

    while (!check(parser, TOK_RBRACE) && !check(parser, TOK_EOF)) {
        skip_newlines(parser);
        if (check(parser, TOK_RBRACE)) break;

        /* Parse arm: ok(name) => expr, err(name) => expr, some(name) => expr, none => expr, or VariantName(name) => expr */
        AstNode *arm = ast_new(NODE_MATCH_ARM, parser->current.line);
        arm->as.match_arm.binding_name = NULL;
        arm->as.match_arm.variant_name = NULL;

        if (match(parser, TOK_OK)) {
            arm->as.match_arm.pattern_kind = MATCH_PATTERN_OK;
        } else if (match(parser, TOK_ERR)) {
            arm->as.match_arm.pattern_kind = MATCH_PATTERN_ERR;
        } else if (match(parser, TOK_SOME)) {
            arm->as.match_arm.pattern_kind = MATCH_PATTERN_SOME;
        } else if (match(parser, TOK_NONE)) {
            arm->as.match_arm.pattern_kind = MATCH_PATTERN_NONE;
        } else if (match(parser, TOK_IDENT)) {
            /* Enum variant pattern: VariantName or VariantName(binding) */
            arm->as.match_arm.pattern_kind = MATCH_PATTERN_ENUM;
            arm->as.match_arm.variant_name = agim_alloc(parser->previous.length + 1);
            memcpy(arm->as.match_arm.variant_name, parser->previous.start, parser->previous.length);
            arm->as.match_arm.variant_name[parser->previous.length] = '\0';

            /* Optional payload binding */
            if (match(parser, TOK_LPAREN)) {
                consume(parser, TOK_IDENT, "expected binding name");
                arm->as.match_arm.binding_name = agim_alloc(parser->previous.length + 1);
                memcpy(arm->as.match_arm.binding_name, parser->previous.start, parser->previous.length);
                arm->as.match_arm.binding_name[parser->previous.length] = '\0';
                consume(parser, TOK_RPAREN, "expected ')' after binding name");
            }
        } else {
            error_at_current(parser, "expected pattern in match arm");
            ast_free(arm);
            ast_free(node);
            return NULL;
        }

        /* none has no binding, others may */
        if (arm->as.match_arm.pattern_kind != MATCH_PATTERN_NONE &&
            arm->as.match_arm.pattern_kind != MATCH_PATTERN_ENUM) {
            consume(parser, TOK_LPAREN, "expected '(' after pattern keyword");
            consume(parser, TOK_IDENT, "expected binding name");

            arm->as.match_arm.binding_name = agim_alloc(parser->previous.length + 1);
            memcpy(arm->as.match_arm.binding_name, parser->previous.start, parser->previous.length);
            arm->as.match_arm.binding_name[parser->previous.length] = '\0';

            consume(parser, TOK_RPAREN, "expected ')' after binding name");
        }

        consume(parser, TOK_FAT_ARROW, "expected '=>' after pattern");

        /* Parse arm body - can be expression, return statement, or block */
        if (match(parser, TOK_RETURN)) {
            arm->as.match_arm.body = parse_return_stmt(parser);
        } else if (check(parser, TOK_LBRACE)) {
            arm->as.match_arm.body = parse_block(parser);
        } else {
            arm->as.match_arm.body = parse_expression(parser);
        }

        if (node->as.match_expr.arm_count >= capacity) {
            if (capacity > SIZE_MAX / 2) {
                error(parser, "too many match arms");
                ast_free(node);
                return NULL;
            }
            capacity *= 2;
            node->as.match_expr.arms = agim_realloc(
                node->as.match_expr.arms,
                sizeof(AstNode *) * capacity
            );
        }
        node->as.match_expr.arms[node->as.match_expr.arm_count++] = arm;

        skip_newlines(parser);
        /* Optional comma between arms */
        match(parser, TOK_COMMA);
        skip_newlines(parser);
    }

    consume(parser, TOK_RBRACE, "expected '}' after match arms");
    return node;
}

static AstNode *parse_try_expr(Parser *parser) {
    int line = parser->previous.line;
    AstNode *node = ast_new(NODE_TRY, line);
    node->as.try_expr.expr = parse_expression(parser);
    return node;
}

static AstNode *parse_ok_expr(Parser *parser) {
    int line = parser->previous.line;
    consume(parser, TOK_LPAREN, "expected '(' after 'ok'");
    AstNode *value = parse_expression(parser);
    consume(parser, TOK_RPAREN, "expected ')' after ok value");

    AstNode *node = ast_new(NODE_RESULT_OK, line);
    node->as.result_expr.value = value;
    return node;
}

static AstNode *parse_err_expr(Parser *parser) {
    int line = parser->previous.line;
    consume(parser, TOK_LPAREN, "expected '(' after 'err'");
    AstNode *value = parse_expression(parser);
    consume(parser, TOK_RPAREN, "expected ')' after err value");

    AstNode *node = ast_new(NODE_RESULT_ERR, line);
    node->as.result_expr.value = value;
    return node;
}

static AstNode *parse_some_expr(Parser *parser) {
    int line = parser->previous.line;
    consume(parser, TOK_LPAREN, "expected '(' after 'some'");
    AstNode *value = parse_expression(parser);
    consume(parser, TOK_RPAREN, "expected ')' after some value");

    return ast_some(value, line);
}

/* Struct/Enum Declarations */
static AstNode *parse_struct_decl(Parser *parser) {
    int line = parser->previous.line;

    consume(parser, TOK_IDENT, "expected struct name");
    char name[256];
    size_t name_len = parser->previous.length < 255 ? parser->previous.length : 255;
    memcpy(name, parser->previous.start, name_len);
    name[name_len] = '\0';

    AstNode *node = ast_struct_decl(name, line);

    skip_newlines(parser);
    consume(parser, TOK_LBRACE, "expected '{' after struct name");
    skip_newlines(parser);

    while (!check(parser, TOK_RBRACE) && !check(parser, TOK_EOF)) {
        /* Parse field: name: Type */
        consume(parser, TOK_IDENT, "expected field name");
        char field_name[256];
        size_t field_len = parser->previous.length < 255 ? parser->previous.length : 255;
        memcpy(field_name, parser->previous.start, field_len);
        field_name[field_len] = '\0';

        consume(parser, TOK_COLON, "expected ':' after field name");
        AstNode *field_type = parse_type(parser);

        ast_struct_add_field(node, field_name, field_type, parser->previous.line);

        skip_newlines(parser);
        if (!check(parser, TOK_RBRACE)) {
            if (!match(parser, TOK_COMMA)) {
                /* Allow trailing comma or newline separation */
                skip_newlines(parser);
            } else {
                skip_newlines(parser);
            }
        }
    }

    consume(parser, TOK_RBRACE, "expected '}' after struct fields");
    return node;
}

static AstNode *parse_enum_decl(Parser *parser) {
    int line = parser->previous.line;

    consume(parser, TOK_IDENT, "expected enum name");
    char name[256];
    size_t name_len = parser->previous.length < 255 ? parser->previous.length : 255;
    memcpy(name, parser->previous.start, name_len);
    name[name_len] = '\0';

    AstNode *node = ast_enum_decl(name, line);

    skip_newlines(parser);
    consume(parser, TOK_LBRACE, "expected '{' after enum name");
    skip_newlines(parser);

    while (!check(parser, TOK_RBRACE) && !check(parser, TOK_EOF) && !parser->had_error) {
        /* Parse variant: Name or Name(Type) */
        consume(parser, TOK_IDENT, "expected variant name");
        if (parser->had_error) break;
        char var_name[256];
        size_t var_len = parser->previous.length < 255 ? parser->previous.length : 255;
        memcpy(var_name, parser->previous.start, var_len);
        var_name[var_len] = '\0';

        AstNode *payload_type = NULL;
        if (match(parser, TOK_LPAREN)) {
            payload_type = parse_type(parser);
            consume(parser, TOK_RPAREN, "expected ')' after variant payload type");
        }

        ast_enum_add_variant(node, var_name, payload_type, parser->previous.line);

        skip_newlines(parser);
        if (!check(parser, TOK_RBRACE)) {
            if (!match(parser, TOK_COMMA)) {
                skip_newlines(parser);
            } else {
                skip_newlines(parser);
            }
        }
    }

    consume(parser, TOK_RBRACE, "expected '}' after enum variants");
    return node;
}

static AstNode *parse_type_alias(Parser *parser) {
    int line = parser->previous.line;

    consume(parser, TOK_IDENT, "expected type alias name");
    char name[256];
    size_t name_len = parser->previous.length < 255 ? parser->previous.length : 255;
    memcpy(name, parser->previous.start, name_len);
    name[name_len] = '\0';

    consume(parser, TOK_ASSIGN, "expected '=' after type alias name");

    AstNode *aliased = parse_type(parser);

    return ast_type_alias(name, aliased, line);
}

/* Import Parsing */

static AstNode *parse_import(Parser *parser) {
    int line = parser->previous.line;

    /* Check for selective import: import { ... } from "path" */
    if (check(parser, TOK_LBRACE)) {
        advance(parser);

        AstNode *node = ast_new(NODE_IMPORT_FROM, line);
        node->as.import_from.names = NULL;
        node->as.import_from.name_count = 0;
        node->as.import_from.path = NULL;

        size_t capacity = 8;
        node->as.import_from.names = agim_alloc(sizeof(char *) * capacity);

        /* Parse name list */
        do {
            skip_newlines(parser);
            if (check(parser, TOK_RBRACE)) break;

            consume(parser, TOK_IDENT, "expected identifier in import list");

            if (node->as.import_from.name_count >= capacity) {
                if (capacity > SIZE_MAX / 2) {
                    error(parser, "too many import names");
                    ast_free(node);
                    return NULL;
                }
                capacity *= 2;
                node->as.import_from.names = agim_realloc(
                    node->as.import_from.names,
                    sizeof(char *) * capacity
                );
            }

            char *name = agim_alloc(parser->previous.length + 1);
            memcpy(name, parser->previous.start, parser->previous.length);
            name[parser->previous.length] = '\0';
            node->as.import_from.names[node->as.import_from.name_count++] = name;

            skip_newlines(parser);
        } while (match(parser, TOK_COMMA));

        consume(parser, TOK_RBRACE, "expected '}' after import names");
        consume(parser, TOK_FROM, "expected 'from' after import names");
        consume(parser, TOK_STRING, "expected module path string");

        /* Extract path without quotes */
        size_t path_len = parser->previous.length - 2;
        node->as.import_from.path = agim_alloc(path_len + 1);
        memcpy(node->as.import_from.path, parser->previous.start + 1, path_len);
        node->as.import_from.path[path_len] = '\0';

        return node;
    }

    /* Simple import: import "path" */
    consume(parser, TOK_STRING, "expected module path string");

    AstNode *node = ast_new(NODE_IMPORT, line);
    /* Extract path without quotes */
    size_t path_len = parser->previous.length - 2;
    node->as.import_stmt.path = agim_alloc(path_len + 1);
    memcpy(node->as.import_stmt.path, parser->previous.start + 1, path_len);
    node->as.import_stmt.path[path_len] = '\0';

    return node;
}

static AstNode *parse_export(Parser *parser) {
    int line = parser->previous.line;

    AstNode *node = ast_new(NODE_EXPORT, line);

    /* export fn/tool/let/const/struct/enum/type */
    if (match(parser, TOK_FN)) {
        node->as.export_stmt.decl = parse_fn_decl(parser, false);
    } else if (match(parser, TOK_TOOL)) {
        node->as.export_stmt.decl = parse_fn_decl(parser, true);
    } else if (match(parser, TOK_LET)) {
        node->as.export_stmt.decl = parse_let_stmt(parser, false);
    } else if (match(parser, TOK_CONST)) {
        node->as.export_stmt.decl = parse_let_stmt(parser, true);
    } else if (match(parser, TOK_STRUCT)) {
        node->as.export_stmt.decl = parse_struct_decl(parser);
    } else if (match(parser, TOK_ENUM)) {
        node->as.export_stmt.decl = parse_enum_decl(parser);
    } else if (match(parser, TOK_ALIAS)) {
        node->as.export_stmt.decl = parse_type_alias(parser);
    } else {
        error_at_current(parser, "expected declaration after 'export'");
        ast_free(node);
        return NULL;
    }

    return node;
}

static AstNode *parse_statement(Parser *parser) {
    skip_newlines(parser);

    if (match(parser, TOK_LET)) {
        return parse_let_stmt(parser, false);
    }
    if (match(parser, TOK_CONST)) {
        return parse_let_stmt(parser, true);
    }
    if (match(parser, TOK_IF)) {
        return parse_if_stmt(parser);
    }
    if (match(parser, TOK_WHILE)) {
        return parse_while_stmt(parser);
    }
    if (match(parser, TOK_FOR)) {
        return parse_for_stmt(parser);
    }
    if (match(parser, TOK_RETURN)) {
        return parse_return_stmt(parser);
    }
    if (match(parser, TOK_BREAK)) {
        return ast_new(NODE_BREAK, parser->previous.line);
    }
    if (match(parser, TOK_CONTINUE)) {
        return ast_new(NODE_CONTINUE, parser->previous.line);
    }

    /* Expression statement */
    AstNode *expr = parse_expression(parser);
    if (!expr) return NULL;

    AstNode *node = ast_new(NODE_EXPR_STMT, expr->line);
    node->as.return_stmt.value = expr; /* Reuse same layout */
    return node;
}

/* Declaration Parsing */

static AstNode *parse_param(Parser *parser) {
    consume(parser, TOK_IDENT, "expected parameter name");

    AstNode *param = ast_new(NODE_PARAM, parser->previous.line);
    param->as.param.name = agim_alloc(parser->previous.length + 1);
    memcpy(param->as.param.name, parser->previous.start, parser->previous.length);
    param->as.param.name[parser->previous.length] = '\0';

    param->as.param.type_ann = NULL;
    if (match(parser, TOK_COLON)) {
        param->as.param.type_ann = parse_type(parser);
    }

    return param;
}

static AstNode *parse_fn_decl(Parser *parser, bool is_tool) {
    int line = parser->previous.line;

    consume(parser, TOK_IDENT, "expected function name");

    char *name = agim_alloc(parser->previous.length + 1);
    memcpy(name, parser->previous.start, parser->previous.length);
    name[parser->previous.length] = '\0';

    consume(parser, TOK_LPAREN, "expected '(' after function name");

    /* Parse parameters */
    AstNode **params = NULL;
    size_t param_count = 0;
    size_t param_capacity = 0;

    if (!check(parser, TOK_RPAREN)) {
        param_capacity = 8;
        params = agim_alloc(sizeof(AstNode *) * param_capacity);

        do {
            AstNode *param = parse_param(parser);
            if (param_count >= param_capacity) {
                if (param_capacity > SIZE_MAX / 2) {
                    error(parser, "too many function parameters");
                    return NULL;
                }
                param_capacity *= 2;
                params = agim_realloc(params, sizeof(AstNode *) * param_capacity);
            }
            params[param_count++] = param;
        } while (match(parser, TOK_COMMA));
    }

    consume(parser, TOK_RPAREN, "expected ')' after parameters");

    /* Optional return type: -> Type */
    AstNode *return_type = NULL;
    if (match(parser, TOK_ARROW)) {
        return_type = parse_type(parser);
    }

    /* Body */
    AstNode *body = parse_block(parser);

    AstNode *node = ast_new(is_tool ? NODE_TOOL_DECL : NODE_FN_DECL, line);
    node->as.fn_decl.name = name;
    node->as.fn_decl.params = params;
    node->as.fn_decl.param_count = param_count;
    node->as.fn_decl.return_type = return_type;
    node->as.fn_decl.body = body;
    node->as.fn_decl.description = NULL;
    node->as.fn_decl.params_map = NULL;
    return node;
}

static AstNode *parse_tool_decorator(Parser *parser) {
    (void)parser->previous.line;  /* Line available for future error reporting */

    char *description = NULL;
    AstNode *params_map = NULL;

    /* Optional decorator arguments: @tool(...) */
    if (match(parser, TOK_LPAREN)) {
        skip_newlines(parser);

        /* Parse key: value pairs */
        while (!check(parser, TOK_RPAREN) && !check(parser, TOK_EOF)) {
            consume(parser, TOK_IDENT, "expected decorator key");
            char *key = agim_alloc(parser->previous.length + 1);
            memcpy(key, parser->previous.start, parser->previous.length);
            key[parser->previous.length] = '\0';

            consume(parser, TOK_COLON, "expected ':' after decorator key");
            skip_newlines(parser);

            if (strcmp(key, "description") == 0) {
                consume(parser, TOK_STRING, "expected string for description");
                /* Extract string without quotes */
                size_t len = parser->previous.length - 2;
                description = agim_alloc(len + 1);
                memcpy(description, parser->previous.start + 1, len);
                description[len] = '\0';
            } else if (strcmp(key, "params") == 0) {
                /* Parse params map */
                params_map = parse_expression(parser);
            } else {
                /* Skip unknown keys - just parse the value */
                parse_expression(parser);
            }

            agim_free(key);
            skip_newlines(parser);

            if (!match(parser, TOK_COMMA)) break;
            skip_newlines(parser);
        }

        consume(parser, TOK_RPAREN, "expected ')' after decorator arguments");
    }

    skip_newlines(parser);

    /* Now expect 'fn' */
    if (!match(parser, TOK_FN)) {
        error_at_current(parser, "expected 'fn' after @tool decorator");
        if (description) agim_free(description);
        if (params_map) ast_free(params_map);
        return NULL;
    }

    /* Parse the function as a tool */
    AstNode *fn = parse_fn_decl(parser, true);
    if (!fn) {
        if (description) agim_free(description);
        if (params_map) ast_free(params_map);
        return NULL;
    }

    /* Store the description and params_map in the function node */
    fn->as.fn_decl.description = description;
    fn->as.fn_decl.params_map = params_map;

    return fn;
}

static AstNode *parse_declaration(Parser *parser) {
    skip_newlines(parser);

    /* Check for @tool decorator */
    if (match(parser, TOK_AT)) {
        /* 'tool' is a keyword, so check for TOK_TOOL not TOK_IDENT */
        if (match(parser, TOK_TOOL)) {
            return parse_tool_decorator(parser);
        } else {
            error_at_current(parser, "expected 'tool' after '@'");
            return NULL;
        }
    }

    if (match(parser, TOK_IMPORT)) {
        return parse_import(parser);
    }
    if (match(parser, TOK_EXPORT)) {
        return parse_export(parser);
    }
    if (match(parser, TOK_TOOL)) {
        return parse_fn_decl(parser, true);
    }
    if (match(parser, TOK_FN)) {
        return parse_fn_decl(parser, false);
    }
    if (match(parser, TOK_STRUCT)) {
        return parse_struct_decl(parser);
    }
    if (match(parser, TOK_ENUM)) {
        return parse_enum_decl(parser);
    }
    if (match(parser, TOK_ALIAS)) {
        return parse_type_alias(parser);
    }

    /* Top-level statement */
    return parse_statement(parser);
}

/* Public API */

Parser *parser_new(Lexer *lexer) {
    Parser *parser = agim_alloc(sizeof(Parser));
    parser->lexer = lexer;
    parser->error = NULL;
    parser->error_line = 0;
    parser->had_error = false;
    parser->panic_mode = false;
    parser->depth = 0;

    /* Prime the parser */
    advance(parser);
    return parser;
}

void parser_free(Parser *parser) {
    if (parser) {
        if (parser->error) {
            agim_free(parser->error);
        }
        agim_free(parser);
    }
}

AstNode *parser_parse(Parser *parser) {
    AstNode *program = ast_program(1);

    skip_newlines(parser);
    while (!check(parser, TOK_EOF)) {
        AstNode *decl = parse_declaration(parser);
        if (decl) {
            ast_program_add(program, decl);
        }

        if (parser->panic_mode) {
            synchronize(parser);
        }

        skip_newlines(parser);
    }

    if (parser->had_error) {
        ast_free(program);
        return NULL;
    }

    return program;
}

const char *parser_error(Parser *parser) {
    return parser->error;
}

int parser_error_line(Parser *parser) {
    return parser->error_line;
}
