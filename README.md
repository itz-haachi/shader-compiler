# MiniGLSL: a mini compiler for a shader language

MiniGLSL compiles a subset of **GLSL**, the OpenGL shading language, using
**lex + yacc + C**. The compiler prints the result of every phase, so you can
follow a shader from its tokens to three-address code.

| # | Phase | Files | Status |
|---|-------|-------|--------|
| 1 | Lexical analysis | `shader_final.l` | done (Review 1) |
| 2 | Syntax analysis, now builds an AST | `shader.y`, `ast.c`, `ast.h` | done (extended on this branch) |
| 3 | Semantic analysis: scoped symbol table and type checking | `semantic.c`, `symtab.c`, `types.c` | **done on this branch** |
| 4 | Intermediate code: three-address code as quadruples | `icg.c` | **done on this branch** |
| 5 | Optimization | – | planned (final review) |
| 6 | Target code (vector assembly) | – | planned (final review) |

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
gcc -w -o minic lex.yy.c y.tab.c ast.c types.c symtab.c semantic.c icg.c main.c
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
Valid shader program
```

Excerpt for `test.frag`:

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

---

## 4. Project layout

```
shader_final.l      phase 1  lexer (lex/flex)
shader.y            phase 2  grammar + AST construction (yacc/bison)
ast.h / ast.c       AST node definitions, constructors, tree printer
types.h / types.c   GLSL type representation and conversion rules
symtab.h / symtab.c scoped symbol table, function table, struct table
semantic.h / .c     phase 3  semantic analysis / type checking
icg.h / icg.c       phase 4  three-address code (quadruples)
main.c              driver: runs the phases, command-line options
Makefile, build.sh  build scripts
run_tests.sh        test runner
test.frag           sample fragment shader from Review 1
tests/valid/              shaders that must compile
tests/semantic_errors/    shaders with semantic errors (each line marked // ERROR)
tests/syntax_errors/      shaders with lexical / syntax errors
```

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
