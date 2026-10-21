# MiniGLSL: a mini compiler for a shader language

MiniGLSL compiles a subset of **GLSL**, the OpenGL shading language, using
**lex + yacc + C**. The compiler prints the result of every phase, so you can
follow a shader from its tokens to optimized three-address code and readable
educational vector assembly.

| # | Phase | Files | Status |
|---|-------|-------|--------|
| 1 | Lexical analysis | `shader_final.l` | done (Review 1) |
| 2 | Syntax analysis, now builds an AST | `shader.y`, `ast.c`, `ast.h` | done (extended on this branch) |
| 3 | Semantic analysis: scoped symbol table and type checking | `semantic.c`, `symtab.c`, `types.c` | **done on this branch** |
| 4 | Intermediate code: three-address code as quadruples | `icg.c` | **done on this branch** |
| 5 | Optimization | `optimizer.c`, `optimizer.h` | **done** |
| 6 | Target code (vector assembly) | `codegen.c`, `codegen.h` | **done** |
| 7 | Standalone C code | `c_codegen.c`, `c_codegen.h` | **done** |

---

## 1. How to run

### Requirements

You need `flex`, `bison` (or `yacc`) and `gcc`. These are the same tools used in
Review 1, on an Ubuntu terminal.

```bash
sudo apt install flex bison gcc make
```

**On Windows**, use WSL (Ubuntu): open the project folder in a WSL terminal,
for example `cd /mnt/c/Users/<you>/vit/complier/shader-compiler`, and run the
commands below there.

### Build

Choose one of these:

```bash
make
```

```bash
sh build.sh
```

Or run the steps by hand (equivalent to Review 1, plus the new C files):

```bash
bison -y -d shader.y          # or: yacc -d shader.y   -> y.tab.c, y.tab.h
flex shader_final.l           # or: lex shader_final.l -> lex.yy.c
gcc -w -o minic lex.yy.c y.tab.c ast.c types.c symtab.c semantic.c icg.c optimizer.c codegen.c c_codegen.c main.c
```

Each of these produces the executable `./minic`.

### Run

```bash
./minic test.frag
```

To type a shader instead, run `./minic` with no file and finish with Ctrl+D.

| Option | Effect |
|--------|--------|
| `--no-tokens` | don't print the token stream (phase 1) |
| `--no-ast` | don't print the typed AST |
| `--no-symtab` | don't print the symbol table |
| `--no-quads` | don't print the quadruple table |
| `--ir-only` | print only the verdicts, errors and three-address code |
| `--no-opt` | keep the original TAC for target generation and report zero optimization changes |
| `--no-target` | suppress the target vector assembly section |
| `--no-c` | suppress the standalone C source section |
| `--c-output file` | also write the generated standalone C99 source to `file` |
| `--target-only` | validate all earlier phases but print only target assembly and the final verdict |
| `--ascii` | draw the AST with `|--` and `` `-- `` instead of box-drawing characters (for terminals without UTF-8) |
| `--no-color` | don't colour the AST (colour is only used when output goes to a terminal; `NO_COLOR` is also respected) |

Examples:

```bash
./minic --no-tokens test.frag
```

```bash
./minic --ir-only tests/valid/control_flow.frag
```

```bash
./minic tests/semantic_errors/types.frag
```

The exit status is `0` for a valid shader and `1` if there was any lexical,
syntax or semantic error. Errors go to stderr, prefixed with the line number.

### Run the test suite

```bash
sh run_tests.sh
```

(`make test` does the same.) The script checks that every shader in
`tests/valid/` compiles and that every shader in `tests/semantic_errors/` and
`tests/syntax_errors/` is rejected.

---

## 2. What the output looks like

A run prints these sections in order. A phase runs only if the earlier
phases found no errors.

```
========== LEXICAL ANALYSIS ==========      token table (unchanged from Review 1)
========== SYNTAX ANALYSIS ==========       "Valid syntax" + counts of globals/structs/functions
========== SEMANTIC ANALYSIS ==========
---------- Symbol table ----------           every struct, variable, parameter and function
---------- Typed abstract syntax tree -------- the AST with the type of every expression
N semantic error(s), M warning(s)
========== INTERMEDIATE CODE (three-address code) ==========
---------- Quadruples ----------             the same code as an (op, arg1, arg2, result) table
---------- Temporaries ----------            the type of every temporary (t1 : vec3, ...)
========== OPTIMIZATION ==========
---------- Optimized TAC ----------          transformed Quad IR plus actual pass statistics
========== TARGET CODE ==========
---------- Vector Assembly ----------        deterministic educational vector instructions
========== C CODE ==========
---------- Standalone C99 ----------          compilable C99 with the generated MGValue runtime
Valid shader program
```

The typed AST is drawn as a tree. Each child shows its role (`cond:`,
`then:`, `body:`, `init:`, `target:`, `value:`, `index:`), each expression
shows its type, and declarations and statements show their source line. On
a terminal, node kinds, types and roles are shown in colour. Excerpt for
`test.frag`:

```
└── Function void main()   [line 26]
    └── body: Block
        ├── VarDecl vec3 n   [line 28]
        │   └── init: Call normalize : vec3
        │       └── Id vNormal : vec3
        ...
        ├── For   [line 33]
        │   ├── init: VarDecl int i   [line 33]
        │   │   └── init: Const 0 : int
        │   ├── cond: Binary < : bool
        │   │   ├── Id i : int
        │   │   └── Const 4 : int
        │   ├── step: Unary ++ : int
        │   │   └── Id i : int
        │   └── body: Block
        │       └── ExprStmt
        │           └── Assign *= : float
        │               ├── target: Id diff : float
        │               └── value: Index [] : float
        │                   ├── Id uWeights : float[4]
        │                   └── index: Id i : int
```

Implicit conversions inserted by the semantic phase appear as their own nodes:

```
VarDecl float f   [line 3]
`-- init: ImplicitConv to float
    `-- Const 1 : int
```

The symbol table and three-address code for `test.frag`:

```
NAME             KIND       TYPE                   QUALIFIERS         SCOPE        DEPTH  LINE
------------------------------------------------------------------------------------------------
vNormal          variable   vec3                   in                 global       0      11
uLight           variable   Light                  uniform            global       0      16
lambert          function   float(vec3,vec3)       defined            global       0      21
n                parameter  vec3                                      lambert      1      21
i                variable   int                                       main         2      33
...

  func lambert:
      t1 = -l
      param n
      param t1
      t2 = call dot, 2
      param t2
      param 0.0
      t3 = call max, 2
      return t3
  endfunc lambert
  func main:
      ...
      i = 0
  L1:
      t7 = i < 4
      ifFalse t7 goto L3
      t8 = uWeights[i]
      diff = diff * t8
  L2:
      i = i + 1
      goto L1
  L3:
      ...
      t13 = tex.xyz                 <- swizzle .rgb, normalised to xyz
      t14 = uLight.color
      t15 = t13 * t14               <- vec3 * vec3 in one vector-valued temporary
```

---

## 3. What was done on this branch

This branch (`feature/parser-semantic-icg`) starts from `feature/lexer`, where
the lexer and parser could only accept or reject a shader. It adds phases 3
and 4 and changes the parser so that it builds a tree for them.

### Phase 2: syntax analysis now builds an AST

* The grammar in `shader.y` keeps the Review 1 productions and precedence
  rules. Every production now has a semantic action that builds a node of the
  **abstract syntax tree** (`ast.h`). Node kinds cover declarations, structs,
  interface blocks, functions, each statement kind and each expression kind.
* **Error recovery**: the productions `external: error ';'` and
  `stmt: error ';'` let the parser report several syntax errors in one run
  instead of stopping at the first one.
* Two small grammar additions: array constructors `float[3](a, b, c)`, and
  `struct Name {...}` where `Name` is already a type, so that a redefinition
  is reported as a semantic error instead of a confusing syntax error.
* The grammar still has **0 shift/reduce and 0 reduce/reduce conflicts**
  (checked with `bison -v`).
* The lexer (`shader_final.l`) now passes each lexeme to the parser through
  `yylval`, so constants keep their text. It also accepts `--no-tokens`.
* `main()` moved from `shader.y` into `main.c`, which runs all the phases.

### Phase 3: semantic analysis (`semantic.c`, `symtab.c`, `types.c`)

**Type representation (`types.c`).** A type is a base (`bool`, `int`,
`uint`, `float`, `double`, sampler, image or struct) plus a vector size,
matrix columns and an array size. This covers every scalar, `vecN`, `ivecN`,
`uvecN`, `bvecN`, `dvecN`, `matN`, `matCxR`, sampler and struct type.

**Scoped symbol table (`symtab.c`).**

* It is a stack of scopes. The global scope is depth 0. A function's
  parameters and its body share one scope. Each `{ }` block and each `for`
  statement opens a new scope.
* Functions are stored separately so that they can be **overloaded**. A
  prototype and its later definition are matched together.
* Struct types are kept in a struct table, and interface blocks become
  struct-like types.
* Built-in variables are predeclared: `gl_Position`, `gl_FragCoord`
  (read-only), `gl_FragDepth`, `gl_PointSize`, `gl_VertexID` and others.
* Every declaration is logged, so the whole table can be printed after
  analysis.
* Each symbol gets a unique **IR name**. A shadowed variable becomes
  `i_1`, and an overloaded function becomes `scale(vec3)`, so that the
  three-address code is unambiguous.

**Checks performed** (each error has a line number):

| Category | Examples of what is rejected |
|----------|------------------------------|
| Names | undeclared identifier, redeclaration in the same scope, struct or function redefinition, calling an undeclared or never-defined function, a variable used as a function, user names starting with `gl_` |
| Operators | `vec3 + vec4`, `mat3 * vec4`, `<` on vectors (use `lessThan`), `%` and bitwise operators on floats, `!` on non-bool, `&&` on non-bool |
| Implicit conversions | `int -> uint -> float -> double` are allowed and inserted as explicit `ImplicitConv` nodes; `float -> int` and anything to/from `bool` are errors |
| Swizzles | more than 4 components, a component beyond the vector size (`v3.w`), mixing sets (`.xg`), repeated components on the left of `=` (`v.xx = ...`) |
| Structs / arrays | unknown member, index on a non-array, non-integer index, a constant index out of range, a non-constant or non-positive array size |
| Constructors | too many or too few components (`vec4(v3, 1.0, 2.0)`), wrong struct constructor arity, element types of array constructors |
| Built-in functions | about 60 built-ins (`dot`, `normalize`, `mix`, `clamp`, `texture`, `texelFetch`, `transpose`, `lessThan`, ...) with GLSL's genType rules, e.g. `texture(sampler2D, float)` is rejected because a `vec2` is needed |
| Qualifiers / lvalues | assigning to a `const`, a `uniform`, a shader input `in` or a read-only built-in; `const` without an initializer; `in`/`out`/`uniform` on locals; samplers that are not `uniform`; `out` arguments that are not lvalues |
| Statements | `if`/`while`/`for`/`?:` conditions must be `bool`; `break` outside a loop or switch; `continue` outside a loop; `case`/`default` outside a switch; duplicate `case` values; non-constant case labels; switch on a non-integer |
| Functions | a `return` value of the wrong type, a value returned from a `void` function, a missing return value, conflicting return types between overloads, a missing `void main()` |
| Warnings | a non-void function that may reach its end without `return` |

The output of this phase is the **typed AST**, which is printed, and the
**symbol table**.

### Phase 4: intermediate code generation (`icg.c`)

`icg.c` walks the typed AST and produces **three-address code stored as
quadruples** `(op, arg1, arg2, result)`. Temporaries `t1, t2, ...` carry
GLSL types, so a single temporary can hold a whole `vec4` or `mat4`
(**vector-valued temporaries**, as planned in Review 1).

| Quadruple op | Printed as | Used for |
|--------------|-----------|----------|
| `+ - * / % << >> & \| ^ ^^ < > <= >= == !=` | `t = a op b` | binary operators (including vector and matrix `*`) |
| `minus`, `!`, `~` | `t = -a` | unary operators |
| `=` | `x = a` | copies |
| `conv` | `t = (float) a` | implicit conversions inserted by the semantic phase |
| `swizzle`, `member` | `t = v.xyz`, `t = s.field` | swizzles (normalised to `xyzw`) and struct members |
| `swizzle=`, `member=` | `v.xy = a` | writes to swizzles and members |
| `[]`, `[]=` | `t = a[i]`, `a[i] = t` | array, vector and matrix indexing |
| `param`, `refparam` | `param a` | arguments; `refparam` passes `out`/`inout` by reference |
| `call` | `t = call f, n` | user and built-in functions |
| `construct` | `t = construct vec4, n` | constructors (arguments are passed with `param`) |
| `label`, `goto`, `if`, `ifFalse` | `ifFalse t goto L3` | control flow |
| `return`, `discard` | | |
| `func`, `endfunc` | | function boundaries |

Translation schemes:

* **if / else, while, do-while, for**: the standard layouts with labels. In
  each loop, `continue` jumps to the step (`for`) or to the condition
  (`do-while`).
* **switch**: the value is compared with each `case`, followed by a jump to
  `default` or to the end. Fall-through between cases is kept, and `break`
  jumps to the end.
* **`&&` and `||`** use short-circuit jumps, so the right operand is only
  evaluated when needed.
* **`?:`** uses a result temporary that both branches assign.
* **`x++`, `++x` and `x += y`** are expanded into loads, an operation and a
  store.
* **Compound lvalues** such as `ps[i].pos.x = 3.0` or `m[1][2] += f` are
  done as read-modify-write: load the enclosing value into a temporary,
  update it, and store it back.
* **`out` arguments that are not plain variables**, e.g. `get(v.y)`, are
  passed in a temporary and copied back after the call.
* **Global initializers**, e.g. `const vec3 AMBIENT = vec3(0.1)`, go into a
  pseudo-function `__init_globals`.
* **No extra copies**: if a temporary would be copied straight into a
  variable, the instruction writes to the variable directly. So
  `x = a + b` is emitted instead of `t1 = a + b; x = t1`. Temporaries are
  renumbered afterwards so the numbers have no gaps. This is not
  optimization; constant folding, CSE and dead-code elimination are
  phase 5.

### Phase 5: optimization (`optimizer.c`)

The optimizer deep-copies `quads[]` into `optimized_quads[]`, preserving the
original TAC for comparison. It performs conservative scalar constant
propagation and folding, basic-block common-subexpression elimination, safe
unsigned-integer strength reduction, and dead temporary removal. It clears state at
control-flow and side-effect boundaries, so assignments inside an `if`, loop,
call, or output store cannot incorrectly leak a constant.

The optimization report includes actual counts for constant folds,
propagations, common subexpressions, strength reductions, and dead
instructions. `--no-opt` keeps the copied IR unchanged while still allowing
the target backend to run.

### Phase 6: target code generation (`codegen.c`)

The backend consumes only `optimized_quads` and maps `t1`, `t2`, ...
deterministically to `R0`, `R1`, .... It emits readable vector-machine
pseudo-instructions such as `ADD`, `MUL`, `CMP_*`, `JMP`, `LABEL`, `VEC2`--`VEC4`,
`MAT2`--`MAT4`, `TEX`, `NORM`, `LEN`, `LOADIDX`, `STOREIDX`, `CALL`, `RET`, and
`DISCARD`. Arithmetic remains vector-aware and element-wise; this output is an
educational assembly notation, not vendor GPU machine code. Runtime array
length (`length` Quad) is emitted as `ARRAYLEN`, distinct from the GLSL vector
length built-in (`LEN`).

### Phase 7: standalone C code generation (`c_codegen.c`)

The C backend also consumes only `optimized_quads`. It emits a complete C99
translation unit, including a small `MGValue` runtime that represents scalar,
vector, matrix and array values. Arithmetic and comparisons call runtime helper
functions, labels and jumps become ordinary C control flow, constructors become
`mg_make_vec`, and user functions receive an argument array. Built-ins such as
`dot`, `normalize`, `length` and texture calls have dedicated helpers; unknown
built-ins use a safe zero-valued fallback. `discard` sets a runtime flag and
returns from the shader function. The runtime is intentionally educational and
does not require OpenGL headers or a GLSL implementation.

Use `--c-output generated.c` to save only the generated C source, then compile
it independently, for example `gcc -std=c99 -c generated.c -o generated.o`.

---

## 4. Files and what each one does

### 4.1 How the files work together

```
              test.frag  (shader source)
                  │
                  ▼
 ┌─────────────────────────────────┐   tokens    ┌──────────────────────────────┐
 │ shader_final.l   (lexer)        │ ──────────▶ │ shader.y   (parser)          │
 │ phase 1: characters → tokens    │ ◀────────── │ phase 2: tokens → AST        │
 └─────────────────────────────────┘ struct names└──────────────┬───────────────┘
                                                                │ builds nodes with
                                                                ▼
                                                 ┌──────────────────────────────┐
                                                 │ ast.h / ast.c                │
                                                 │ the tree + the tree printer  │
                                                 └──────────────┬───────────────┘
                                                                │ ast_root
                                                                ▼
 ┌─────────────────────────────────┐   uses      ┌──────────────────────────────┐
 │ symtab.h / symtab.c             │ ◀────────── │ semantic.h / semantic.c      │
 │ scopes, functions, structs      │             │ phase 3: checks + types the  │
 ├─────────────────────────────────┤   uses      │ AST                          │
 │ types.h / types.c               │ ◀────────── │                              │
 │ GLSL types and conversions      │             └──────────────┬───────────────┘
 └─────────────────────────────────┘                            │ typed AST
                                                                ▼
                                                 ┌──────────────────────────────┐
                                                 │ icg.h / icg.c                │
                                                 │ phase 4: typed AST → TAC     │
                                                 │ (quadruples)                 │
                                                 └──────────────────────────────┘

 main.c calls each phase in order and prints its output.
```

### 4.2 Compiler source files

#### `shader_final.l`: lexical analyzer (phase 1, lex/flex)

* **Role:** reads the shader one character at a time and groups the
  characters into **tokens**, which the parser asks for one at a time
  through `yylex()`.
* **What it does:**
  * Prints each token as one row of the token table (`LINE TOKEN DETAIL LEXEME`).
  * Has a table of 162 GLSL keywords (`keywords[]`, searched by
    `lookup_keyword()`). Each keyword maps to a parser token: `TYPE_NAME`,
    `QUALIFIER`, `IF`, `FOR`, and so on.
  * Rejects the 39 reserved words such as `class` and `goto`.
  * Recognises int, uint, hex, octal, float and bool constants, every
    operator and punctuator, and comments. `skip_block_comment()` reports
    an unterminated comment.
  * Reports lexical errors with line numbers: malformed numbers (`12abc`),
    bad octals (`09`), stray characters.
  * An identifier that was declared as a struct (asked through
    `is_struct_name()` in `shader.y`) is returned as a `TYPE_NAME`, so
    `Light l;` parses as a declaration.
  * `emit()` prints the token, remembers it for syntax error messages
    (`last_lexeme`), and passes its text to the parser through
    `yylval.str`.
* **Output:** `lex.yy.c` (generated by flex).

#### `shader.y`: syntax analyzer (phase 2, yacc/bison)

* **Role:** checks that the token stream follows the GLSL grammar and
  builds the **abstract syntax tree (AST)**.
* **What it does:**
  * Is an LALR(1) grammar with about 150 productions for global declarations,
    structs, interface blocks, functions, the 8 kinds of statement and
    all expressions. It has **0 conflicts**.
  * Gives operator precedence and associativity with `%left` and `%right`,
    exactly as in C and GLSL. The dangling `else` is resolved with
    `%nonassoc LOWER_THAN_ELSE`.
  * Every production has an action that builds an AST node with
    `new_node()`, `node2()` and similar helpers. The finished tree is
    stored in `ast_root`.
  * `finish_decl()` combines `qualifiers type` with a list of declarators,
    so `uniform float a, b[4];` becomes two `VarDecl` nodes.
    `qualifier_flag()` turns `in`, `out`, `uniform`, ... into bit flags.
  * Records struct names with `add_struct_name()` so the lexer can return
    them as type names.
  * Error recovery (`error ';'`) and `yyerror()` print
    `line N: syntax error near 'x'` and continue parsing, so several
    errors can be reported in one run.
* **Output:** `y.tab.c` and `y.tab.h` (the token codes, which the lexer
  also includes).

#### `ast.h` / `ast.c`: abstract syntax tree

* **Role:** the data structure shared by phases 2, 3 and 4.
* **`ast.h`** defines:
  * `Node` (kind, line, children `a b c d`, a `next` link for lists,
    `type`, `sym`, ...).
  * The node kinds (`N_FUNC`, `N_IF`, `N_BINARY`, `N_CALL`, ...).
  * The operator codes (`OP_ADD`, `OP_LT`, ...).
  * The qualifier flags (`Q_CONST`, `Q_IN`, `Q_UNIFORM`, ...).
* **`ast.c`** contains:
  * The constructors (`new_node`, `node1`, `node2`, `node_op`,
    `node_str`) and list helpers (`append`, `list_len`).
  * `op_str()` and `quals_str()`, which turn operator codes and qualifier
    flags back into text.
  * **The tree printer:** `print_ast()`, `print_tree()`, `node_label()` and
    `node_children()`. It draws the typed AST with box-drawing lines
    (`--ascii` for plain ASCII) and colours. Each child shows its role
    (`cond:`, `then:`, ...) and each expression shows its type.

#### `types.h` / `types.c`: GLSL types

* **Role:** one representation of a GLSL type, used everywhere else.
* **What it does:**
  * `Type` holds `base` (bool, int, uint, float, double, sampler, image
    or struct), `vec` (number of components, or rows for a matrix),
    `cols` (number of matrix columns) and `arr` (array size).
  * `type_from_name()` turns `"vec3"`, `"mat2x4"`, `"sampler2D"` or a
    struct name into a `Type`.
  * `type_str()` turns a `Type` back into text (`"vec3"`, `"float[4]"`).
  * Classification helpers: `is_scalar`, `is_vector`, `is_matrix`,
    `is_numeric`, `is_integer`, `components`, ...
  * `can_convert()` holds the **implicit conversion** rule
    `int → uint → float → double`. Shapes must match and `bool` never
    converts.

#### `symtab.h` / `symtab.c`: symbol table

* **Role:** remembers every declared name: its kind, type, qualifiers,
  scope and line.
* **What it does:**
  * **Scopes:** `scope_push()` and `scope_pop()` keep a stack of scopes,
    with depth 0 for globals. `sym_insert()` adds a name to the current
    scope. `sym_lookup()` searches from the innermost scope outwards, and
    `sym_lookup_current()` searches only the current scope, to detect
    redeclarations.
  * **Functions** are kept in their own list so they can be overloaded.
    `func_insert()` adds one, and `func_find_exact()`, `func_first()` and
    `func_next()` look them up. `func_mangle_overloads()` gives overloads
    distinct IR names such as `scale(vec3)`.
  * **Structs:** `struct_define()`, `struct_lookup()` and
    `struct_member()` (which looks up a member's type).
  * `make_irname()` gives each variable a unique name for the
    intermediate code. A shadowed `i` becomes `i_1`, and a user variable
    named like a temporary (`t1`) becomes `t1_`.
  * `symtab_print()` prints the struct types and the symbol table shown
    in the output.

#### `semantic.h` / `semantic.c`: semantic analyzer (phase 3)

* **Role:** checks the meaning of the program, reports semantic errors,
  and annotates the AST with types (the **typed AST**).
* **What it does:**
  * `semantic_analyze()` is the entry point. It predeclares the built-in
    variables (`add_builtins()`: `gl_Position`, `gl_FragCoord`, ...),
    walks every global declaration and function, then checks that
    `void main()` exists and that every called function has a body.
  * **Declarations:**
    * `declare_var()` checks storage qualifiers, `const`, samplers and
      redeclarations.
    * `check_init()` checks initializers and initializer lists.
    * `check_struct_def()` and `check_block()` handle structs and
      interface blocks.
    * `check_function()` handles parameters, prototypes, overloads,
      `main()` and missing returns.
    * `resolve_type()` checks array sizes, which must be constant;
      `const_eval()` computes them.
  * **Expressions:** `check_expr()` computes the type of each node and
    calls the matching checker:
    * `check_binary()`, `check_unary()` and `check_assign()` apply the
      operator rules. `shape_result()` gives the result shape for
      vector/matrix arithmetic, e.g. `mat4 * vec4 → vec4`.
    * `check_field()` handles swizzles and struct members.
    * `check_index()` handles arrays, vectors and matrices.
    * `check_ctor()` handles constructors.
    * `check_call()` handles user functions and overload resolution.
    * `check_builtin()` handles about 60 built-in functions such as
      `dot`, `mix` and `texture`.
  * **Implicit conversions:** `coerce()` inserts an `ImplicitConv` node
    wherever a conversion is needed, so phase 4 sees every conversion
    explicitly.
  * **Lvalues:** `writable()` and `check_lvalue()` reject assignments to
    `const`, `uniform`, `in` and read-only built-ins, and swizzles that
    repeat a component.
  * **Statements:** `check_stmt()` checks that conditions are `bool`,
    that `break`/`continue`/`case`/`default` are in the right place,
    that case labels are not duplicated, and that `return` types match.
  * Errors are printed by `sem_error()` and warnings by `sem_warning()`.

#### `icg.h` / `icg.c`: intermediate code generator (phase 4)

* **Role:** translates the typed AST into **three-address code**, stored
  as **quadruples** `(op, arg1, arg2, result)`.
* **What it does:**
  * `icg_generate()` is the entry point. It emits `__init_globals` for
    global initializers, then a `func ... endfunc` block for each
    function.
  * `gen_expr()` returns the operand that holds an expression's value
    (a variable, a constant or a new temporary `tN`).
    `gen_short_circuit()` handles `&&` and `||`, and `gen_args()`
    handles call and constructor arguments (`param` / `refparam`).
  * `gen_store()` writes to an lvalue. Nested targets such as
    `a[i].pos.x` are handled as read-modify-write.
  * `gen_stmt()` and `gen_switch()` translate statements into labels and
    jumps (`if` / `ifFalse` / `goto`). `break` and `continue` labels are
    kept on stacks.
  * `gen_init()` handles declarations with initializers and initializer
    lists.
  * `new_temp()` creates a **typed** temporary, so one temporary can hold
    a whole `vec4`. `new_label()` creates `L1`, `L2`, ...
  * `assign_to()` avoids a needless copy (`x = a + b` instead of
    `t1 = a + b; x = t1`), and `renumber_temps()` then closes the gaps in
    the temporary numbers.
  * `icg_print_tac()`, `icg_print_quads()` and `icg_print_temps()` print
    the three sections of the phase 4 output.
* The quadruple array `quads[]` / `n_quads` is the input for the optimizer;
  target generation consumes the optimizer's `optimized_quads[]` copy.

#### `optimizer.h` / `optimizer.c`: optimizer (phase 5)

* **Role:** transforms a private copy of the Phase 4 Quad array while keeping
  the original TAC available for comparison.
* **What it does:** constant propagation and folding, basic-block CSE, safe
  integer strength reduction, and conservative dead temporary elimination.
  State is cleared around control-flow, calls, reference arguments, returns,
  discard, and aggregate stores.
* **Output:** `optimized_quads[]`, actual pass statistics, optimized TAC, and
  an optimized quadruple table.

#### `codegen.h` / `codegen.c`: vector target generator (phase 6)

* **Role:** translates only `optimized_quads[]` into deterministic educational
  vector assembly.
* **What it does:** maps `tN` to `RN`, emits arithmetic/comparison/control
  flow, vector and matrix constructors, swizzles, indexing, conversions,
  calls, `TEX`, `NORM`, `LEN`, `DISCARD`, `RET`, and function boundaries.
  It buffers constructor and call parameters so the output remains readable.

#### `c_codegen.h` / `c_codegen.c`: standalone C backend (phase 7)

* **Role:** translates the same optimized Quad IR into a self-contained C99
  source file.
* **What it does:** emits the `MGValue` runtime, global declarations, function
  prototypes, generated shader functions, and a C `main` wrapper. It preserves
  vector operations, branches, calls, constructors, swizzles, indexing and
  `discard` without requiring GLSL libraries.

#### `main.c`: compiler driver

* **Role:** the program's `main()`, which runs the phases in order.
* **What it does:**
  * Reads the command-line options (`--no-tokens`, `--no-ast`,
    `--no-symtab`, `--no-quads`, `--ir-only`, `--no-opt`, `--no-target`,
    `--target-only`, `--no-c`, `--c-output`, `--ascii`, `--no-color`).
  * Opens the input file, or reads from stdin.
  * Runs `yyparse()` (phases 1 and 2), then `semantic_analyze()`
    (phase 3), `icg_generate()` (phase 4), `optimize_ir()` (phase 5),
    `codegen_generate()` (phase 6), and `c_codegen_generate()` (phase 7),
    printing each enabled section.
  * Stops at the first phase that reports errors and prints the verdict
    (`Valid shader program` / `Invalid shader program (...)`). The exit
    status is 0 or 1.

### 4.3 Build, test and configuration files

| File | What it does |
|------|--------------|
| `Makefile` | `make` runs bison on `shader.y`, flex on `shader_final.l`, then compiles all C files into `./minic`. `make test` runs the tests, and `make clean` deletes the generated files. |
| `build.sh` | The same three build steps as a plain shell script, for machines without `make`. |
| `run_tests.sh` | Runs every existing and optimization/backend shader, checks exit statuses, asserts representative optimized TAC/vector assembly/C output, and compiles representative generated C99 files. |
| `.gitignore` | Keeps generated files (`minic`, `lex.yy.c`, `y.tab.c`, `y.tab.h`, `y.output`) out of git. |
| `.gitattributes` | Keeps LF line endings for `*.sh` and `Makefile`, so they still run under Linux or WSL after a Windows checkout. |

### 4.4 Generated files (not in git; created by the build)

| File | Created by | Contents |
|------|-----------|----------|
| `lex.yy.c` | `flex shader_final.l` | the scanner `yylex()` in C |
| `y.tab.c` | `bison -y -d shader.y` | the LALR(1) parser `yyparse()` in C |
| `y.tab.h` | `bison -y -d shader.y` | token codes (`IDENTIFIER`, `TYPE_NAME`, ...) and `yylval`, shared with the lexer |
| `y.output` | `bison -v shader.y` (optional) | the parser states, used to confirm there are 0 conflicts |
| `minic` | `gcc` | the compiler executable |

### 4.5 Test shaders

| File | Purpose |
|------|---------|
| `test.frag` | The lit fragment shader from Review 1: structs, uniforms, a loop, if/else, `discard`, swizzles, `texture()`. |
| `tests/valid/control_flow.frag` | while, do-while, for, switch with `default`, `break`/`continue`, `&&`/`\|\|`, `?:`, `out`/`inout` parameters, overloading, a prototype defined after `main`, arrays, initializer lists, swizzle writes, and shadowing. |
| `tests/valid/vertex.vert` | A vertex shader: `layout(location = n)`, a uniform block with an instance name, `mat4` products, a `mat3(mat4)` constructor, `gl_Position`, `gl_PointSize`. |
| `tests/optimizer_codegen/` | Focused Phase 5/6 shaders for folding, propagation, reassignment safety, CSE, strength reduction, DCE, control flow, vectors, swizzles, constructors, calls, built-ins, conversions, indexing, and discard. |
| `tests/semantic_errors/types.frag` | 11 type errors, one per marked line: vector size mismatch, a bad swizzle, constructor arity, `mat3 * vec4`, a non-bool condition, wrong `texture()` arguments, `%` on float, ... |
| `tests/semantic_errors/scopes.frag` | 17 errors and 1 warning about names, scopes and qualifiers: redeclarations, undeclared names, writes to a uniform/const/`in`/built-in, `break` outside a loop, an undefined or unmatched function, ... |
| `tests/semantic_errors/no_main.frag` | A shader without `void main()`. |
| `tests/syntax_errors/missing_semicolon.frag` | A syntax error, reported with its line number. |
| `tests/syntax_errors/lexical.frag` | The lexical errors from Review 1: a reserved word, a malformed number, an invalid octal. |

## 5. Error examples

| Input | Phase | Message |
|-------|-------|---------|
| `float x = 1.0` (no `;`) | syntax | `line 4: syntax error near 'float'` |
| `float class = 1.0;` | lexical | `lexical error: reserved keyword 'class'` |
| `vec3 c = a + b;` (a vec3, b vec4) | semantic | `incompatible operands for '+': vec3 and vec4` |
| `vec2 d = a.xyzw;` (a vec3) | semantic | `swizzle '.xyzw' selects component 'w' beyond vec3` |
| `uTime = 1.0;` (uniform) | semantic | `cannot assign to this expression: it is a uniform` |
| `if (f) {}` (f float) | semantic | `condition of 'if' must be a bool scalar, got float` |
| `texture(uTex, 1.0)` | semantic | `no matching overload for built-in function texture(sampler2D, float)` |
| `break;` outside a loop | semantic | `'break' outside a loop or switch` |

## 6. Limitations

* Arrays of arrays (`float a[2][3]`) are reported as unsupported.
* Preprocessor lines (`#version`, `#define`) are listed as tokens but not
  processed. Macros are out of scope, as stated in Review 1.
* The shader stage is not inferred, so `discard` is accepted in any shader.
* Phases 5 (optimization) and 6 (target vector assembly) are for the final
  review. The quadruple list in `icg.c` (`quads[]`) is the input they will
  use.
