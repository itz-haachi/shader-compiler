/*
 * types.c -- construction, printing and classification of GLSL types.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "types.h"

Type mk_type(BaseType base, int vec, int cols)
{
    Type t;
    t.base = base;
    t.vec = vec;
    t.cols = cols;
    t.arr = 0;
    t.name = 0;
    return t;
}

Type error_type(void) { return mk_type(TY_ERROR, 1, 0); }
Type void_type(void)  { return mk_type(TY_VOID, 1, 0); }

/* vec3 -> 3, mat2x4 -> cols 2 rows 4, mat3 -> 3x3 */
static int parse_matrix(const char *s, int *cols, int *rows)
{
    if (s[0] < '2' || s[0] > '4')
        return 0;
    *cols = s[0] - '0';
    if (s[1] == '\0') {
        *rows = *cols;
        return 1;
    }
    if (s[1] == 'x' && s[2] >= '2' && s[2] <= '4' && s[3] == '\0') {
        *rows = s[2] - '0';
        return 1;
    }
    return 0;
}

Type type_from_name(const char *name)
{
    Type t = mk_type(TY_STRUCT, 1, 0);
    int c, r;

    if (strcmp(name, "void") == 0)        return void_type();
    if (strcmp(name, "bool") == 0)        return mk_type(TY_BOOL, 1, 0);
    if (strcmp(name, "int") == 0)         return mk_type(TY_INT, 1, 0);
    if (strcmp(name, "uint") == 0)        return mk_type(TY_UINT, 1, 0);
    if (strcmp(name, "float") == 0)       return mk_type(TY_FLOAT, 1, 0);
    if (strcmp(name, "double") == 0)      return mk_type(TY_DOUBLE, 1, 0);
    if (strcmp(name, "atomic_uint") == 0) {
        t = mk_type(TY_ATOMIC, 1, 0);
        t.name = "atomic_uint";
        return t;
    }

    if (strncmp(name, "vec", 3) == 0 && name[3] >= '2' && name[3] <= '4' && !name[4])
        return mk_type(TY_FLOAT, name[3] - '0', 0);
    if (name[1] == 'v' && strncmp(name + 1, "vec", 3) == 0 && name[4] >= '2' && name[4] <= '4' && !name[5]) {
        int n = name[4] - '0';
        switch (name[0]) {
        case 'i': return mk_type(TY_INT, n, 0);
        case 'u': return mk_type(TY_UINT, n, 0);
        case 'b': return mk_type(TY_BOOL, n, 0);
        case 'd': return mk_type(TY_DOUBLE, n, 0);
        }
    }
    if (strncmp(name, "mat", 3) == 0 && parse_matrix(name + 3, &c, &r))
        return mk_type(TY_FLOAT, r, c);
    if (strncmp(name, "dmat", 4) == 0 && parse_matrix(name + 4, &c, &r))
        return mk_type(TY_DOUBLE, r, c);

    if (strstr(name, "sampler") == name || strstr(name, "sampler") == name + 1) {
        t = mk_type(TY_SAMPLER, 1, 0);
        t.name = strdup(name);
        return t;
    }
    if (strstr(name, "image") == name || strstr(name, "image") == name + 1) {
        t = mk_type(TY_IMAGE, 1, 0);
        t.name = strdup(name);
        return t;
    }

    /* anything else is a user-defined struct name */
    t.name = strdup(name);
    return t;
}

Type elem_type(Type t)
{
    t.arr = 0;
    return t;
}

Type with_base(Type t, BaseType b)
{
    t.base = b;
    return t;
}

char *base_str(BaseType b)
{
    switch (b) {
    case TY_ERROR:   return "<error>";
    case TY_VOID:    return "void";
    case TY_BOOL:    return "bool";
    case TY_INT:     return "int";
    case TY_UINT:    return "uint";
    case TY_FLOAT:   return "float";
    case TY_DOUBLE:  return "double";
    case TY_SAMPLER: return "sampler";
    case TY_IMAGE:   return "image";
    case TY_ATOMIC:  return "atomic_uint";
    case TY_STRUCT:  return "struct";
    }
    return "?";
}

/* returns one of several static buffers so a few calls can share a printf */
char *type_str(Type t)
{
    static char bufs[8][80];
    static int next = 0;
    char *buf = bufs[next];
    char *prefix = "";
    next = (next + 1) % 8;

    switch (t.base) {
    case TY_SAMPLER: case TY_IMAGE: case TY_ATOMIC: case TY_STRUCT:
        strcpy(buf, t.name ? t.name : "?");
        break;
    case TY_ERROR: case TY_VOID:
        strcpy(buf, base_str(t.base));
        break;
    default:
        if (t.cols > 0) {
            prefix = t.base == TY_DOUBLE ? "d" : "";
            if (t.cols == t.vec)
                sprintf(buf, "%smat%d", prefix, t.cols);
            else
                sprintf(buf, "%smat%dx%d", prefix, t.cols, t.vec);
        } else if (t.vec > 1) {
            switch (t.base) {
            case TY_INT:    prefix = "i"; break;
            case TY_UINT:   prefix = "u"; break;
            case TY_BOOL:   prefix = "b"; break;
            case TY_DOUBLE: prefix = "d"; break;
            default:        prefix = "";  break;
            }
            sprintf(buf, "%svec%d", prefix, t.vec);
        } else {
            strcpy(buf, base_str(t.base));
        }
    }

    if (t.arr > 0)
        sprintf(buf + strlen(buf), "[%d]", t.arr);
    else if (t.arr < 0)
        strcat(buf, "[]");
    return buf;
}

int type_equal(Type a, Type b)
{
    if (a.base != b.base || a.vec != b.vec || a.cols != b.cols || a.arr != b.arr)
        return 0;
    if (a.base == TY_STRUCT || a.base == TY_SAMPLER || a.base == TY_IMAGE)
        return a.name && b.name && strcmp(a.name, b.name) == 0;
    return 1;
}

int is_error(Type t)  { return t.base == TY_ERROR; }
int is_void(Type t)   { return t.base == TY_VOID; }
int is_array(Type t)  { return t.arr != 0; }

static int is_plain(Type t)
{
    return t.arr == 0 && t.base >= TY_BOOL && t.base <= TY_DOUBLE;
}

int is_scalar(Type t) { return is_plain(t) && t.vec == 1 && t.cols == 0; }
int is_vector(Type t) { return is_plain(t) && t.vec > 1 && t.cols == 0; }
int is_matrix(Type t) { return is_plain(t) && t.cols > 0; }

int is_numeric(Type t)
{
    return is_plain(t) && t.base != TY_BOOL;
}

int is_integer(Type t)
{
    return is_plain(t) && t.cols == 0 && (t.base == TY_INT || t.base == TY_UINT);
}

int is_bool(Type t)     { return is_scalar(t) && t.base == TY_BOOL; }
int is_bool_any(Type t) { return is_plain(t) && t.cols == 0 && t.base == TY_BOOL; }

int is_float_based(Type t)
{
    return is_plain(t) && (t.base == TY_FLOAT || t.base == TY_DOUBLE);
}

int components(Type t)
{
    if (!is_plain(t))
        return 0;
    return t.cols > 0 ? t.vec * t.cols : t.vec;
}

/* int -> uint -> float -> double ; bool never converts implicitly */
int base_rank(BaseType b)
{
    switch (b) {
    case TY_INT:    return 1;
    case TY_UINT:   return 2;
    case TY_FLOAT:  return 3;
    case TY_DOUBLE: return 4;
    default:        return 0;
    }
}

int can_convert(Type from, Type to)
{
    if (type_equal(from, to))
        return 1;
    if (!is_plain(from) || !is_plain(to))
        return 0;
    if (from.vec != to.vec || from.cols != to.cols)
        return 0;
    if (base_rank(from.base) == 0 || base_rank(to.base) == 0)
        return 0;
    /* matrices only exist for float/double */
    return base_rank(from.base) < base_rank(to.base);
}
