/*
 * types.h -- type representation shared by the parser, the semantic
 *            analyzer and the intermediate code generator.
 *
 * A GLSL type is described by
 *   base : the scalar component type (float, int, ...) or a special kind
 *          (sampler, image, struct, void)
 *   vec  : number of components (1 = scalar, 2..4 = vector);
 *          for a matrix this is the number of ROWS
 *   cols : 0 for non-matrices, otherwise the number of COLUMNS
 *   arr  : 0 = not an array, n > 0 = array of n, -1 = unsized array
 *   name : type name for samplers, images and structs
 */
#ifndef TYPES_H
#define TYPES_H

typedef enum {
    TY_ERROR,       /* result of an ill-typed expression */
    TY_VOID,
    TY_BOOL,
    TY_INT,
    TY_UINT,
    TY_FLOAT,
    TY_DOUBLE,
    TY_SAMPLER,
    TY_IMAGE,
    TY_ATOMIC,
    TY_STRUCT
} BaseType;

typedef struct {
    BaseType base;
    int vec;
    int cols;
    int arr;
    char *name;
} Type;

Type mk_type(BaseType base, int vec, int cols);
Type type_from_name(const char *name);
Type error_type(void);
Type void_type(void);
Type elem_type(Type t);              /* element of an array */
Type with_base(Type t, BaseType b);  /* same shape, other component type */
char *type_str(Type t);              /* printable name, e.g. "vec3", "float[4]" */
char *base_str(BaseType b);

int type_equal(Type a, Type b);
int is_error(Type t);
int is_void(Type t);
int is_array(Type t);
int is_scalar(Type t);
int is_vector(Type t);
int is_matrix(Type t);
int is_numeric(Type t);             /* int/uint/float/double scalar, vector or matrix */
int is_integer(Type t);             /* int/uint scalar or vector */
int is_bool(Type t);                /* bool scalar */
int is_bool_any(Type t);            /* bool scalar or bvec */
int is_float_based(Type t);         /* float/double scalar, vector or matrix */
int components(Type t);             /* number of scalar components */
int base_rank(BaseType b);          /* for implicit conversions */
int can_convert(Type from, Type to);/* implicit conversion allowed? */

#endif
