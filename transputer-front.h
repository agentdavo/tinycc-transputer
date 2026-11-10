/*
 *  Transputer: the front end's share - what tccgen.c does differently for
 *  this target, where that is more than a line or two.
 *
 *  Code, not declarations: tccgen.c includes this once, under
 *  TCC_TARGET_TRANSPUTER, ahead of unary(), because it parses with tccgen.c's
 *  static state (the value stack, scopes, `loc`, the token stream).  tccgen.c
 *  uses these functions of it:
 *
 *    tp_inline_call()            at each call, before gfunc_call(): a tiny
 *                                inline function is expanded in place
 *    tp_inline_function_start()  as each function body begins
 *    resolve_inline_sections()   before gen_inline_functions() emits anything:
 *                                which section each out-of-line copy goes to
 *    tp_block_while()            a while loop, its test after its body
 *    tp_block_for()              a for loop, likewise
 *    tp_unary_builtin()          at a name unary() has no case for
 */

#include "transputer-knobs.h"

static void new_scope(struct scope *o);
static void prev_scope(struct scope *o, int is_expr);

/* ------------------------------------------------------------------ */
/* Sections of the out-of-line copies */

/* TinyCC does not perform general source-level inlining.  It records an
   inline definition and emits one out-of-line copy if the definition is
   referenced.  Record the sections of calls from normal functions here;
   resolve_inline_sections later combines these roots with the complete graph
   of deferred inline bodies before any outline is emitted. */
static void note_inline_call_section(SValue *call_target)
{
    Sym *call_sym;
    int i;

    if (!cur_text_section || nocode_wanted
        || !(call_target->r & VT_SYM) || !call_target->sym)
        return;

    call_sym = call_target->sym;
    for (i = 0; i < tcc_state->nb_inline_fns; ++i) {
        InlineFunc *fn = tcc_state->inline_fns[i];
        Sym *inline_sym = fn->sym;

        if (!inline_sym)
            continue;
        if (inline_sym != call_sym
            && (!inline_sym->v || inline_sym->v != call_sym->v))
            continue;
        if (fn->section || fn->call_section_conflict)
            return;
        if (!fn->call_section)
            fn->call_section = cur_text_section;
        else if (fn->call_section != cur_text_section)
            fn->call_section_conflict = 1;
        return;
    }
}

typedef struct InlineSectionEdge {
    int callee;
    unsigned char indirect;
} InlineSectionEdge;

typedef struct InlineSectionNode {
    InlineSectionEdge *edges;
    int nb_edges;
    Section *placement;
    unsigned char reachable;
} InlineSectionNode;

static void add_inline_section_edge(InlineSectionNode *node, int callee,
                                    int indirect)
{
    InlineSectionEdge *edge;
    int i;

    for (i = 0; i < node->nb_edges; ++i) {
        edge = &node->edges[i];
        if (edge->callee == callee) {
            if (indirect)
                edge->indirect = 1;
            return;
        }
    }
    node->edges = tcc_realloc(node->edges,
                              (node->nb_edges + 1) * sizeof *node->edges);
    edge = &node->edges[node->nb_edges++];
    edge->callee = callee;
    edge->indirect = indirect;
}

/* Build a conservative call graph from the already-preprocessed token
   strings.  A function-name token immediately followed by '(' is a direct
   edge.  Any other reference may escape through a pointer, so it forces the
   referenced outline to ordinary .text once its containing function is
   reachable. */
static void scan_inline_section_edges(TCCState *s, InlineSectionNode *nodes,
                                      int *by_token, int caller)
{
    TokenString stream;
    int pending = -1;

    stream = *s->inline_fns[caller]->func_str;
    begin_macro(&stream, 0);
    next();
    while (tok != TOK_EOF) {
        if (pending >= 0) {
            add_inline_section_edge(&nodes[caller], pending, tok != '(');
            pending = -1;
        }
        if (tok >= TOK_IDENT && tok < tok_ident && by_token[tok])
            pending = by_token[tok] - 1;
        next();
    }
    if (pending >= 0)
        add_inline_section_edge(&nodes[caller], pending, 1);
    end_macro();
}

static Section *join_inline_section(Section *old, Section *incoming)
{
    if (!old)
        return incoming;
    if (old == incoming)
        return old;
    /* Ordinary .text is the conservative top of the placement lattice. */
    return text_section;
}

/* Resolve all deferred-inline placements before emitting any of them.

   Parsing an outline can make another outline live.  Deciding placement at
   that point is order-dependent: an .init.text path may be seen before a
   later permanent path to the same callee.  Instead, seed this graph with
   calls from normal functions, then propagate reachability and section joins
   to a fixed point.  Explicit section attributes are barriers and remain
   authoritative.  Address escapes, conflicting callers, and unknown roots
   use ordinary .text. */
static void resolve_inline_sections(TCCState *s)
{
    InlineSectionNode *nodes;
    int *by_token;
    unsigned char *scanned;
    int changed, i, j;

    nodes = tcc_mallocz(s->nb_inline_fns * sizeof *nodes);
    by_token = tcc_mallocz(tok_ident * sizeof *by_token);
    scanned = tcc_mallocz(s->nb_inline_fns);
    for (i = 0; i < s->nb_inline_fns; ++i) {
        InlineFunc *fn = s->inline_fns[i];
        if (fn->sym && fn->sym->v >= TOK_IDENT && fn->sym->v < tok_ident)
            by_token[fn->sym->v] = i + 1;
    }
    for (i = 0; i < s->nb_inline_fns; ++i) {
        InlineFunc *fn = s->inline_fns[i];
        Sym *sym = fn->sym;

        if (!sym)
            continue;
        if (sym->c || !(sym->type.t & VT_INLINE)) {
            nodes[i].reachable = 1;
            if (fn->section)
                nodes[i].placement = fn->section;
            else if (sym->a.addrtaken || fn->call_section_conflict
                     || !fn->call_section)
                nodes[i].placement = text_section;
            else
                nodes[i].placement = fn->call_section;
        }
    }

    do {
        changed = 0;
        for (i = 0; i < s->nb_inline_fns; ++i) {
            InlineSectionNode *caller = &nodes[i];

            if (!caller->reachable || !caller->placement)
                continue;
            if (!scanned[i]) {
                scan_inline_section_edges(s, nodes, by_token, i);
                scanned[i] = 1;
            }
            for (j = 0; j < caller->nb_edges; ++j) {
                InlineSectionEdge *edge = &caller->edges[j];
                InlineSectionNode *callee = &nodes[edge->callee];
                InlineFunc *callee_fn = s->inline_fns[edge->callee];
                Sym *callee_sym = callee_fn->sym;
                Section *incoming, *joined;

                if (!callee_sym)
                    continue;
                if (!callee->reachable) {
                    callee->reachable = 1;
                    changed = 1;
                }
                if (callee_fn->section) {
                    joined = callee_fn->section;
                } else {
                    incoming = edge->indirect ? text_section
                                              : caller->placement;
                    if (callee_sym->a.addrtaken)
                        incoming = text_section;
                    joined = join_inline_section(callee->placement, incoming);
                }
                if (joined != callee->placement) {
                    callee->placement = joined;
                    changed = 1;
                }
            }
        }
    } while (changed);
    tcc_free(scanned);
    tcc_free(by_token);

    for (i = 0; i < s->nb_inline_fns; ++i) {
        InlineFunc *fn = s->inline_fns[i];
        if (!fn->section && nodes[i].reachable)
            fn->call_section = nodes[i].placement ? nodes[i].placement
                                                  : text_section;
        tcc_free(nodes[i].edges);
    }
    tcc_free(nodes);
}

/* ------------------------------------------------------------------ */
/*
 * Calls to tiny inline functions, expanded in place.
 *
 * TCC emits an inline function out of line and calls it.  On the transputer
 * the call, the callee's frame and its return cost more than a one-line
 * body - rol32(), an empty instrumentation hook, an accessor - and the kernel
 * makes hundreds of thousands of such calls while it boots.  A call to an
 * inline function whose saved body is expression statements and at most a
 * final `return x;` is replaced by the body: the arguments, already
 * converted to the parameter types, go to frame temporaries named like the
 * parameters in a scope of their own, and the body's tokens are parsed again
 * in place, as gen_inline_functions() parses them to emit the function.  A
 * parameter the body never names gets no temporary.  Later expansions in the
 * same function reuse the temporaries.
 *
 * Only a body that means the same in the caller qualifies: no declarations,
 * labels, control statements, statement expressions or asm, no keyword
 * outside the types, qualifiers, sizeof, typeof and three builtins, and no
 * name but a parameter's that a local of the caller could be shadowing.
 * Parameters and the result are scalars, the prototype has no ellipsis, and
 * the body is at most TP_INLINE_MAX_TOKS tokens.  Not with debug information.
 * TCC_TRANSPUTER_INLINE=0 turns it off.
 */
#define TP_INLINE_MAX_TOKS  32
#define TP_INLINE_MAX_ARGS  6
#define TP_INLINE_MAX_DEPTH 3
#define TP_INLINE_NTMP      32

static int tp_inline_depth;
static struct { int loc, size, align, busy; } tp_inline_tmp[TP_INLINE_NTMP];
static int tp_inline_ntmp;

static int tp_inline_on(void)
{
    static int v = -1;
    if (v < 0) {
        const char *e = tp_knob(TPK_TCC_TRANSPUTER_INLINE);
        v = !(e && *e == '0');
    }
    return v;
}

static int tp_inline_scalar(CType *t)
{
    if (t->t & (VT_ARRAY | VT_VLA | VT_BITFIELD))
        return 0;
    switch (t->t & VT_BTYPE) {
    case VT_BYTE: case VT_SHORT: case VT_INT: case VT_LLONG:
    case VT_PTR: case VT_BOOL: case VT_FLOAT: case VT_DOUBLE:
        return 1;
    }
    return 0;
}

static int tp_inline_keyword_ok(int t)
{
    switch (t) {
    case TOK_VOID: case TOK_CHAR: case TOK_SHORT: case TOK_INT:
    case TOK_LONG: case TOK_FLOAT: case TOK_DOUBLE: case TOK_BOOL:
    case TOK_UNSIGNED: case TOK_SIGNED1: case TOK_SIGNED2: case TOK_SIGNED3:
    case TOK_CONST1: case TOK_CONST2: case TOK_CONST3:
    case TOK_VOLATILE1: case TOK_VOLATILE2: case TOK_VOLATILE3:
    case TOK_RESTRICT1: case TOK_RESTRICT2: case TOK_RESTRICT3:
    case TOK_STRUCT: case TOK_UNION: case TOK_ENUM: case TOK_SIZEOF:
    case TOK_ALIGNOF1: case TOK_ALIGNOF2: case TOK_ALIGNOF3:
    case TOK_TYPEOF1: case TOK_TYPEOF2: case TOK_TYPEOF3:
    case TOK_EXTENSION: case TOK_builtin_expect:
    case TOK_builtin_constant_p: case TOK_builtin_types_compatible_p:
        return 1;
    }
    return 0;
}

/* The saved body less its closing TOK_EOF, ending instead in the 0 that
   lets next() drop back to the caller's tokens after the final '}'. */
static TokenString *tp_inline_copy(TokenString *src)
{
    TokenString *c;
    int i, n = src->len - 1;

    if (n < 2 || src->str[n] != TOK_EOF)
        return NULL;
    c = tok_str_alloc();
    for (i = 0; i < n; i++)
        tok_str_add(c, src->str[i]);
    tok_str_add(c, 0);
    return c;
}

/* Can fn's body be expanded here?  Reads a copy of its tokens without
   parsing them, and leaves the caller's token stream as it was.  *used gets
   a bit for each parameter the body names, *written one for each it may
   assign or take the address of. */
static int tp_inline_shape(struct InlineFunc *fn, Sym *ftype, int *has_ret,
                           int *used, int *written)
{
    TokenString *c = tp_inline_copy(fn->func_str);
    int ok = 1, n = 0, braces = 0, paren = 0, quest = 0, stmt = 1;
    int prev = '{', ret = 0, i, last = -1;
    Sym *pa, *x;

    *has_ret = *used = *written = 0;
    if (!c)
        return 0;
    unget_tok(0);
    begin_macro(c, 1);
    next();                                 /* '{' */
    next();
    for (;;) {
        int t = tok;
        if (t == '}' && braces == 0)
            break;
        if (++n > TP_INLINE_MAX_TOKS)
            ok = 0;
        if (t == '{')
            braces++, ok = 0;               /* block, statement expression */
        else if (t == '}')
            braces--;
        else if (t == '(')
            paren++;
        else if (t == ')')
            paren--;
        if (last >= 0 && (t == '=' || TOK_ASSIGN(t) || t == TOK_INC
                          || t == TOK_DEC))
            *written |= 1 << last;
        last = -1;
        if (!ok) {
        } else if (ret && stmt) {
            ok = 0;                         /* anything after `return x;` */
        } else if (t == TOK_RETURN) {
            ok = stmt;
            ret = 1;
        } else if (t >= TOK_IDENT && t < TOK_UIDENT) {
            ok = !stmt && tp_inline_keyword_ok(t);
        } else if (t >= TOK_UIDENT && prev != '.' && prev != TOK_ARROW) {
            for (pa = ftype->next, i = 0; pa; pa = pa->next, i++)
                if ((pa->v & ~SYM_FIELD) == t)
                    break;
            if (pa) {
                *used |= 1 << i;
                last = i;
                if (prev == '&' || prev == TOK_INC || prev == TOK_DEC)
                    *written |= 1 << i;
            } else {
                x = (prev == TOK_STRUCT || prev == TOK_UNION ||
                     prev == TOK_ENUM) ? struct_find(t) : sym_find(t);
                if (!x || x->sym_scope)
                    ok = 0;                 /* a caller's local could hide it */
                else if (stmt && (x->type.t & VT_TYPEDEF))
                    ok = 0;                 /* a declaration */
            }
        } else if (t == '?') {
            quest++;
        } else if (t == ':') {
            ok = quest-- > 0;               /* a label otherwise */
        }
        stmt = t == ';' && paren == 0;
        prev = t;
        next();
    }
    next();                                 /* past '}': the caller's token */
    *has_ret = ret;
    return ok;
}

static int tp_inline_temp(int size, int align)
{
    int i;
    for (i = 0; i < tp_inline_ntmp; i++)
        if (!tp_inline_tmp[i].busy && tp_inline_tmp[i].size == size
            && tp_inline_tmp[i].align >= align) {
            tp_inline_tmp[i].busy = 1;
            return i;
        }
    if (tp_inline_ntmp == TP_INLINE_NTMP)
        return -1;
    loc = (loc - size) & -align;
    i = tp_inline_ntmp++;
    tp_inline_tmp[i].loc = loc;
    tp_inline_tmp[i].size = size;
    tp_inline_tmp[i].align = align;
    tp_inline_tmp[i].busy = 1;
    return i;
}

/* The tokens to replay: fn's body, with each parameter konst[] marks
   written as its constant, `( 16 )`, where the body names it - a literal of
   the parameter's own int type, no symbol.  Read from a copy the way the
   scan reads it, so the result also ends in the 0 that returns to the
   caller's tokens. */
static TokenString *tp_inline_body(struct InlineFunc *fn, Sym *ftype,
                                   const int *konst, const uint64_t *val)
{
    TokenString *c, *b;
    int braces = 0, prev = '{', i, any = 0;
    Sym *pa;

    for (pa = ftype->next, i = 0; pa; pa = pa->next, i++)
        any |= konst[i];
    c = tp_inline_copy(fn->func_str);
    if (!any)
        return c;
    b = tok_str_alloc();
    unget_tok(0);
    begin_macro(c, 1);
    next();                                 /* '{' */
    for (;;) {
        int t = tok;
        pa = NULL;
        if (t >= TOK_UIDENT && prev != '.' && prev != TOK_ARROW)
            for (pa = ftype->next, i = 0; pa; pa = pa->next, i++)
                if ((pa->v & ~SYM_FIELD) == t)
                    break;
        if (pa && konst[i]) {
            tok = '(';
            tok_str_add_tok(b);
            if (pa->type.t & VT_UNSIGNED)
                tok = TOK_CUINT, tokc.i = (uint32_t)val[i];
            else
                tok = TOK_CINT, tokc.i = (int)val[i];
            tok_str_add_tok(b);
            tok = ')';
            tok_str_add_tok(b);
        } else {
            tok_str_add_tok(b);
        }
        if (t == '{')
            braces++;
        else if (t == '}' && --braces == 0)
            break;
        prev = t;
        next();
    }
    next();                                 /* past '}': the caller's token */
    tok_str_add(b, 0);
    return b;
}

/* vstack: the function, then its nb_args arguments.  Returns 1 with the
   call's value on the vstack instead, or 0 to make the call. */
static int tp_inline_expand(Sym *ftype, int nb_args)
{
    SValue *fv = vtop - nb_args;
    Sym *fsym = fv->sym, *pa, *params[TP_INLINE_MAX_ARGS];
    struct InlineFunc *fn = NULL;
    int tmp[TP_INLINE_MAX_ARGS], konst[TP_INLINE_MAX_ARGS];
    uint64_t val[TP_INLINE_MAX_ARGS];
    int i, n = 0, bt, has_ret, used, written, size, align, has_val = 0;
    struct scope o;
    TokenString *body;

    if (!tp_inline_on() || nocode_wanted || debug_modes
        || tp_inline_depth >= TP_INLINE_MAX_DEPTH
        || (fv->r & (VT_VALMASK | VT_LVAL | VT_SYM)) != (VT_CONST | VT_SYM)
        || fv->c.i || !fsym || !(fsym->type.t & VT_INLINE)
        || fsym->f.func_noinline || ftype->f.func_type != FUNC_NEW
        || nb_args > TP_INLINE_MAX_ARGS)
        return 0;
    bt = ftype->type.t & VT_BTYPE;
    if (bt != VT_VOID && !tp_inline_scalar(&ftype->type))
        return 0;
    for (pa = ftype->next; pa; pa = pa->next) {
        if (n == nb_args || !tp_inline_scalar(&pa->type)
            || !(pa->v & ~SYM_FIELD))
            return 0;
        params[n++] = pa;
    }
    if (n != nb_args)
        return 0;
    for (i = 0; i < tcc_state->nb_inline_fns; i++)
        if (tcc_state->inline_fns[i]->sym == fsym) {
            fn = tcc_state->inline_fns[i];
            break;
        }
    if (!fn || !tp_inline_shape(fn, ftype, &has_ret, &used, &written)
        || (bt != VT_VOID && !has_ret))
        return 0;
    for (i = 0; i < n; i++) {
        SValue *a = vtop - (n - 1 - i);
        /* An int constant the body only reads is written into the body as
           that constant, so it folds there (rol32(x, 16) shifts by 16). */
        konst[i] = (a->r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST
            && (params[i]->type.t & VT_BTYPE) == VT_INT
            && (used & (1 << i)) && !(written & (1 << i));
        val[i] = a->c.i;
        tmp[i] = -1;
        if (!(used & (1 << i)) || konst[i])
            continue;
        size = type_size(&params[i]->type, &align);
        if ((tmp[i] = tp_inline_temp(size, align)) < 0) {
            while (--i >= 0)
                if (tmp[i] >= 0)
                    tp_inline_tmp[tmp[i]].busy = 0;
            return 0;
        }
    }

    new_scope(&o);
    tp_inline_depth++;
    for (i = n - 1; i >= 0; i--) {
        CType *pt = &params[i]->type;
        if (konst[i]) {
            vpop();                         /* it is in the body instead */
            continue;
        }
        if (tmp[i] < 0) {
            /* unnamed in the body: evaluated, and a volatile read kept */
            if ((vtop->r & VT_LVAL) && (vtop->type.t & VT_VOLATILE))
                gv(RC_TYPE(vtop->type.t));
            vpop();
            continue;
        }
        sym_push(params[i]->v & ~SYM_FIELD, pt, VT_LOCAL | VT_LVAL,
                 tp_inline_tmp[tmp[i]].loc);
        vset(pt, VT_LOCAL | VT_LVAL, tp_inline_tmp[tmp[i]].loc);
        vswap();
        vstore();
        vpop();
    }
    vpop();                                 /* the function */

    body = tp_inline_body(fn, ftype, konst, val);
    unget_tok(0);
    begin_macro(body, 1);
    next();                                 /* '{' */
    next();
    while (tok != '}') {
        if (tok == ';') {
            /* an empty statement, often what a no-op macro leaves */
        } else if (tok == TOK_RETURN) {
            next();
            if (tok != ';') {
                gexpr();
                gen_assign_cast(&ftype->type);
                has_val = 1;
            }
        } else {
            gexpr();
            vpop();
        }
        skip(';');
    }
    next();                                 /* past '}': the caller's token */

    if (!has_val) {
        CType vt;
        vt.t = VT_VOID;
        vt.ref = NULL;
        vpush(&vt);
    } else {
        /* An lvalue would name a temporary the next expansion reuses. */
        if (vtop->r & VT_LVAL)
            gv(RC_TYPE(vtop->type.t));
        /* as for a call's value (no PROMOTE_RET here) */
        if (bt == VT_BYTE || bt == VT_SHORT || bt == VT_BOOL)
            vtop->type.t = VT_INT;
    }
    prev_scope(&o, 1);
    for (i = 0; i < n; i++)
        if (tmp[i] >= 0)
            tp_inline_tmp[tmp[i]].busy = 0;
    tp_inline_depth--;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Loops */

static void lblock(int *bsym, int *csym);
static void new_scope_s(struct scope *o);
static void prev_scope_s(struct scope *o);

/* Record the tokens of one loop clause - a for(;;) init/test/increment, or a
   while() test - *without* parsing them, so the clause can be generated later,
   at a different point in the instruction stream (see TOK_FOR and TOK_WHILE).
   Stops on `term' at bracket level 0.  Unlike skip_or_save_block(), a
   top-level ',' is not a terminator: these clauses may legally be comma
   expressions.  Returns NULL for an empty clause.

   Sets *tag_decl if the clause defines an enum (`enum {' or `enum tag {') or
   a tagged struct/union (`struct tag {'): the enumerators or tag are in scope
   in the loop body, so such a clause cannot be moved after the body.  An
   anonymous `struct {' declares no name - the kernel's ARRAY_SIZE() puts one
   in many loop tests - and anything nested in it is scanned anyway. */
static TokenString *for_clause_save(int term, int *tag_decl)
{
    TokenString *str;
    int level = 0, prev = 0, prev2 = 0;

#define IS_TAG_KW(t) ((t) == TOK_STRUCT || (t) == TOK_UNION || (t) == TOK_ENUM)
    if (tok == term)
        return NULL;
    str = tok_str_alloc();
    for (;;) {
        int t = tok;
        if (level == 0 && t == term)
            break;
        if (t == TOK_EOF)
            tcc_error("unexpected end of file");
        if (t == '(' || t == '[' || t == '{')
            level++;
        else if (t == ')' || t == ']' || t == '}')
            level--;
        if (t == '{' && (prev == TOK_ENUM
                         || (prev >= TOK_UIDENT && IS_TAG_KW(prev2))))
            *tag_decl = 1;
        prev2 = prev, prev = t;
        tok_str_add_tok(str);
        next();
    }
#undef IS_TAG_KW
    tok_str_add(str, TOK_EOF);
    return str;
}

/* `while' has been read: compile the loop. */
static void tp_block_while(void)
{
    int a, b, c, d, e;
    struct scope o;
    /* Same rotation as TOK_FOR: record the controlling expression and
       generate it after the body, so a running iteration executes only the
       backward conditional branch and no unconditional jump.

                 j    test              ; once, on entry
          body:  BODY
          test:  CONDITION              ; `continue' lands here
                 cj   body
    */
    int body_addr, tag_decl = 0;
    TokenString *cond_str;

    new_scope_s(&o);
    skip('(');
    cond_str = for_clause_save(')', &tag_decl);
    if (!cond_str)
        expect("expression");
    skip(')');
    a = b = 0;
    TP_LOOP_POOL_OPEN();    /* preheader: dominates the loop */
    if (tag_decl) {
        /* The condition declares something the body can see: parse it
           first and keep the test at the top. */
        d = gind();
        unget_tok(0);
        begin_macro(cond_str, 1);
        next();
        gexpr();
        a = gvtst(1, 0);
        if (tok != TOK_EOF)
            expect(")");
        end_macro();
        next();
        lblock(&a, &b);
        gjmp_addr(d);
    } else {
        e = gjmp(0);            /* entry -> test */
        body_addr = gind();
        lblock(&a, &b);
        gsym(e);                /* test label; also re-enables codegen */
        d = gind();             /* `continue' target is the test */
        unget_tok(0);
        begin_macro(cond_str, 1);
        next();
        TP_CMP_BRANCH_TRUE_BEGIN();  /* the back-edge branches when true */
        gexpr();
        TP_CMP_BRANCH_TRUE_END();
        c = gvtst(0, 0);
        if (tok != TOK_EOF)
            expect(")");
        end_macro();
        next();                 /* restore the token after the loop */
        gsym_addr(c, body_addr);
    }
    gsym_addr(b, d);
    gsym(a);
    TP_LOOP_POOL_CLOSE();
    prev_scope_s(&o);
}

/* `for' has been read: compile the loop. */
static void tp_block_for(void)
{
    int a, b, c, d, e;
    struct scope o;
    /* Rotated layout.  The straightforward one-pass emission order
       (test, jump-over-increment, increment, jump-back-to-test, body,
       jump-to-increment) costs two taken unconditional jumps on every
       iteration on top of the conditional one.  Instead record the
       controlling expression and the increment as token strings and
       generate them *after* the body:

                 j    test              ; once, on entry
          body:  BODY
          incr:  INCREMENT              ; `continue' lands here
          test:  CONDITION
                 cj   body              ; taken while the loop runs

       so a running iteration executes no unconditional jump at all.  The
       clauses are lexed once and parsed once — only the point at which
       they are parsed moves — so this is not a second pass and nothing is
       evaluated twice at compile time or at run time. */
    int body_addr, tag_decl = 0;
    TokenString *cond_str, *incr_str;

    new_scope(&o);

    skip('(');
    if (tok != ';') {
        /* c99 for-loop init decl? */
        if (!decl(VT_JMP)) {
            /* no, regular for-loop init expr */
            gexpr();
            vpop();
            tp_stmt_end();
        }
    }
    skip(';');
    a = b = 0;
    cond_str = for_clause_save(';', &tag_decl);
    skip(';');
    incr_str = for_clause_save(')', &tag_decl);
    skip(')');

    TP_LOOP_POOL_OPEN();            /* preheader: dominates the loop */
    if (tag_decl) {
        /* A clause declares something the body can see: parse the
           clauses first, in source order, and keep the plain layout. */
        unget_tok(0);
        c = d = gind();
        if (cond_str) {
            begin_macro(cond_str, 1);
            next();
            gexpr();
            a = gvtst(1, 0);
            if (tok != TOK_EOF)
                expect(";");
            end_macro();
        }
        if (incr_str) {
            e = gjmp(0);
            d = gind();
            begin_macro(incr_str, 1);
            next();
            gexpr();
            vpop();
            tp_stmt_end();
            if (tok != TOK_EOF)
                expect(")");
            end_macro();
            gjmp_addr(c);
            gsym(e);
        }
        next();
        lblock(&a, &b);
        gjmp_addr(d);
    } else {
        e = cond_str ? gjmp(0) : 0;     /* entry -> test */
        body_addr = gind();
        lblock(&a, &b);
        /* gind() is also what re-enables code generation when the body
           ended with a return/goto: the increment and the test are still
           reachable, from the `continue' edge and from the entry jump,
           so they must be emitted even then. */
        d = gind();                     /* `continue' target */

        if (cond_str || incr_str) {
            /* Replaying a token string clobbers the current token; stash
               it and restore it with the final next(). */
            unget_tok(0);
            if (incr_str) {
                begin_macro(incr_str, 1);
                next();
                gexpr();
                vpop();
                tp_stmt_end();
                if (tok != TOK_EOF)
                    expect(")");
                end_macro();
            }
            if (cond_str) {
                gsym(e);                /* test label */
                begin_macro(cond_str, 1);
                next();
                TP_CMP_BRANCH_TRUE_BEGIN();  /* branches when true */
                gexpr();
                TP_CMP_BRANCH_TRUE_END();
                c = gvtst(0, 0);        /* jump back into the body if true */
                if (tok != TOK_EOF)
                    expect(";");
                end_macro();
                gsym_addr(c, body_addr);
            } else {
                gjmp_addr(body_addr);   /* for (init; ; incr) */
            }
            next();                     /* restore the token after the loop */
        } else {
            gjmp_addr(body_addr);       /* for (;;) */
        }
    }

    gsym_addr(b, d);
    gsym(a);
    TP_LOOP_POOL_CLOSE();
    prev_scope(&o, 0);
}

/* ------------------------------------------------------------------ */
/* Builtins */

/* unary() has met a name that is no case of its own.  If it is a builtin of this
   target, compile the whole call - `tok` is the builtin's name - leave its
   value on the vstack and return 1.  The GCC-style ones are here; the
   instruction builtins (__in, __fpsqrt, the TVX set, ...) are the back
   end's, by its table (gen_transputer_builtin_call). */
static int tp_unary_builtin(void)
{
    int n;
    CType type;

    switch (tok) {
    case TOK_builtin_clz:
    case TOK_builtin_clzl:
    case TOK_builtin_clzll:
        {
            int bits = tok == TOK_builtin_clzll ? 64 :
                       tok == TOK_builtin_clzl ? LONG_SIZE * 8 : 32;
            unsigned long long value;

            next();
            skip('(');
            expr_eq();
            skip(')');
            /* Convert to the parameter type before widening: __builtin_clz
               takes an unsigned int, so the high half of a wider argument
               must not reach the 64-bit helper. */
            if (bits < 64)
                gen_cast_s(VT_INT | VT_UNSIGNED);
            gen_cast_s(VT_LLONG | VT_UNSIGNED);

            if ((vtop->r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST) {
                value = vtop->c.i;
                if (bits < 64)
                    value &= (1ULL << bits) - 1;
                n = 0;
                if (!value) {
                    n = bits;
                } else {
                    unsigned long long mask = 1ULL << (bits - 1);
                    while (!(value & mask))
                        ++n, mask >>= 1;
                }
                vpop();
                vpushi(n);
            } else {
                /* libtcc1 and the Transputer kernel both provide the 64-bit
                   helper.  Narrow variants are zero-extended and adjusted. */
                vpush_helper_func(tok_alloc_const("__builtin_clzll"));
                vrott(2);
                gfunc_call(1);
                vpushi(0);
                PUT_R_RET(vtop, VT_INT);
                if (bits < 64) {
                    vpushi(bits - 64);
                    gen_op('+');
                }
            }
        }
        break;
    case TOK_builtin_strlen:
        next();
        skip('(');
        if (tok == TOK_STR) {
            CString *literal = parse_mult_str("string constant");
            n = strlen(literal->data);
            skip(')');
            vpushs(n);
        } else {
            expr_eq();
            skip(')');
            vpush_helper_func(tok_alloc_const("strlen"));
            vrott(2);
            gfunc_call(1);
            type.t = VT_SIZE_T;
            type.ref = NULL;
            vpush(&type);
            PUT_R_RET(vtop, type.t);
        }
        break;
    case TOK_builtin_strcmp:
    case TOK_builtin_strncmp:
        {
            /* GCC folds strcmp/strncmp of constant C strings.  Linux
             * vruntime_cmp/vruntime_op are written as
             *   if (!__builtin_strcmp(OP, "-")) ...
             *   else __BUILD_BUG_vruntime_op();
             * so the undefined BUILD_BUG symbol is only a link-time
             * assertion.  Without the fold TCC emits a runtime strcmp
             * plus a CALL whose reloc target is 0 (Iptr := 0). */
            int is_ncmp = tok == TOK_builtin_strncmp;
            const char *sa, *sb;
            int narg = is_ncmp ? 3 : 2;

            next();
            skip('(');
            expr_eq();
            skip(',');
            expr_eq();
            if (is_ncmp) {
                skip(',');
                expr_eq();
            }
            skip(')');

            sa = cstr_from_sv(&vtop[is_ncmp ? -2 : -1]);
            sb = cstr_from_sv(&vtop[is_ncmp ? -1 : 0]);
            if (sa && sb && (!is_ncmp
                             || ((vtop->r & (VT_VALMASK | VT_LVAL | VT_SYM))
                                 == VT_CONST))) {
                int r = is_ncmp
                    ? strncmp(sa, sb, (size_t)vtop->c.i)
                    : strcmp(sa, sb);
                while (narg--)
                    vpop();
                vpushi(r);
            } else {
                vpush_helper_func(tok_alloc_const(is_ncmp ? "strncmp"
                                                          : "strcmp"));
                vrott(narg + 1);
                gfunc_call(narg);
                vpushi(0);
                PUT_R_RET(vtop, VT_INT);
            }
        }
        break;
    case TOK_builtin_add_overflow:
    case TOK_builtin_sub_overflow:
    case TOK_builtin_mul_overflow:
        {
            int overflow_tok = tok;
            int bt, bits, is_unsigned;
            CType result_type;
            char helper[48];

            /* GCC's overflow builtins are type-generic, with the result
               pointer selecting the arithmetic width and signedness.  Lower
               the operation to a small typed runtime helper.  This also
               avoids depending on target condition-code conventions. */
            next();
            skip('(');
            expr_eq();
            skip(',');
            expr_eq();
            skip(',');
            expr_eq();
            skip(')');

            if ((vtop->type.t & VT_BTYPE) != VT_PTR)
                tcc_error("third argument to %s must be a pointer",
                          get_tok_str(overflow_tok, 0));
            result_type = *pointed_type(&vtop->type);
            bt = result_type.t & VT_BTYPE;
            if (bt != VT_BYTE && bt != VT_SHORT && bt != VT_INT
                && bt != VT_LLONG)
                tcc_error("result of %s must point to an integer type",
                          get_tok_str(overflow_tok, 0));

            bits = bt == VT_BYTE ? 8 : bt == VT_SHORT ? 16
                   : bt == VT_INT ? 32 : 64;
            is_unsigned = !!(result_type.t & VT_UNSIGNED);

            /* Cast both operands to the result type.  Linux's checked
               arithmetic interfaces use matching operand/result types; the
               casts also give old-style helper calls the correct 64-bit ABI. */
            vrotb(3);              /* b, result pointer, a */
            gen_cast(&result_type);
            vrott(3);              /* a, b, result pointer */
            vswap();               /* a, result pointer, b */
            gen_cast(&result_type);
            vswap();               /* a, b, result pointer */

            snprintf(helper, sizeof(helper), "__tcc_%s_overflow_%c%d",
                     overflow_tok == TOK_builtin_add_overflow ? "add" :
                     overflow_tok == TOK_builtin_sub_overflow ? "sub" : "mul",
                     is_unsigned ? 'u' : 's', bits);
            vpush_helper_func(tok_alloc_const(helper));
            vrott(4);
            gfunc_call(3);
            vpushi(0);
            PUT_R_RET(vtop, VT_INT);
        }
        break;
    default:
        return gen_transputer_builtin_call(tok);
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* The two entries the parser uses while it compiles a function */

/* A call is about to be made.  vstack: the function, then its nb_args
   arguments.  Returns 1 if the call was expanded in place, its value now on
   the vstack instead; `expand` is 0 where the caller cannot take a value
   that way.  Returns 0 to make the call, having noted the section it is
   made from for resolve_inline_sections(). */
static int tp_inline_call(Sym *ftype, int nb_args, int expand)
{
    if (expand && tp_inline_expand(ftype, nb_args))
        return 1;
    note_inline_call_section(vtop - nb_args);
    return 0;
}

/* The frame temporaries belong to one function's frame. */
static void tp_inline_function_start(void)
{
    tp_inline_ntmp = 0;
}
