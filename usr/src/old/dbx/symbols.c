/* Copyright (c) 1982 Regents of the University of California */

static	char sccsid[] = "@(#)symbols.c	1.13 (Berkeley) %G%";

/*
 * Symbol management.
 */

#include "defs.h"
#include "symbols.h"
#include "languages.h"
#include "printsym.h"
#include "tree.h"
#include "operators.h"
#include "eval.h"
#include "mappings.h"
#include "events.h"
#include "process.h"
#include "runtime.h"
#include "machine.h"
#include "names.h"

#ifndef public
typedef struct Symbol *Symbol;

#include "machine.h"
#include "names.h"
#include "languages.h"

/*
 * Symbol classes
 */

typedef enum {
    BADUSE, CONST, TYPE, VAR, ARRAY, PTRFILE, RECORD, FIELD,
    PROC, FUNC, FVAR, REF, PTR, FILET, SET, RANGE, 
    LABEL, WITHPTR, SCAL, STR, PROG, IMPROPER, VARNT,
    FPROC, FFUNC, MODULE, TAG, COMMON, EXTREF, TYPEREF
} Symclass;

typedef enum { R_CONST, R_TEMP, R_ARG, R_ADJUST } Rangetype; 

struct Symbol {
    Name name;
    Language language;
    Symclass class : 8;
    Integer level : 8;
    Symbol type;
    Symbol chain;
    union {
	int offset;		/* variable address */
	long iconval;		/* integer constant value */
	double fconval;		/* floating constant value */
	struct {		/* field offset and size (both in bits) */
	    int offset;
	    int length;
	} field;
	struct {		/* common offset and chain; used to relocate */
	    int offset;         /* vars in global BSS */
	    Symbol chain;
	} common;
	struct {		/* range bounds */
            Rangetype lowertype : 16; 
            Rangetype uppertype : 16;  
	    long lower;
	    long upper;
	} rangev;
	struct {
	    int offset : 16;	/* offset for of function value */
	    Boolean src : 1;	/* true if there is source line info */
	    Boolean inline : 1;	/* true if no separate act. rec. */
	    Boolean intern : 1; /* internal calling sequence */
	    int unused : 13;
	    Address beginaddr;	/* address of function code */
	} funcv;
	struct {		/* variant record info */
	    int size;
	    Symbol vtorec;
	    Symbol vtag;
	} varnt;
	String typeref;		/* type defined by "<module>:<type>" */
	Symbol extref;		/* indirect symbol for external reference */
    } symvalue;
    Symbol block;		/* symbol containing this symbol */
    Symbol next_sym;		/* hash chain */
};

/*
 * Basic types.
 */

Symbol t_boolean;
Symbol t_char;
Symbol t_int;
Symbol t_real;
Symbol t_nil;
Symbol t_open;

Symbol program;
Symbol curfunc;

#define symname(s) ident(s->name)
#define codeloc(f) ((f)->symvalue.funcv.beginaddr)
#define isblock(s) (Boolean) ( \
    s->class == FUNC or s->class == PROC or \
    s->class == MODULE or s->class == PROG \
)
#define isroutine(s) (Boolean) ( \
    s->class == FUNC or s->class == PROC \
)

#define nosource(f) (not (f)->symvalue.funcv.src)
#define isinline(f) ((f)->symvalue.funcv.inline)

#include "tree.h"

/*
 * Some macros to make finding a symbol with certain attributes.
 */

#define find(s, withname) \
{ \
    s = lookup(withname); \
    while (s != nil and not (s->name == (withname) and

#define where /* qualification */

#define endfind(s) )) { \
	s = s->next_sym; \
    } \
}

#endif

/*
 * Symbol table structure currently does not support deletions.
 */

#define HASHTABLESIZE 2003

private Symbol hashtab[HASHTABLESIZE];

#define hash(name) ((((unsigned) name) >> 2) mod HASHTABLESIZE)

/*
 * Allocate a new symbol.
 */

#define SYMBLOCKSIZE 100

typedef struct Sympool {
    struct Symbol sym[SYMBLOCKSIZE];
    struct Sympool *prevpool;
} *Sympool;

private Sympool sympool = nil;
private Integer nleft = 0;

public Symbol symbol_alloc()
{
    register Sympool newpool;

    if (nleft <= 0) {
	newpool = new(Sympool);
	bzero(newpool, sizeof(newpool));
	newpool->prevpool = sympool;
	sympool = newpool;
	nleft = SYMBLOCKSIZE;
    }
    --nleft;
    return &(sympool->sym[nleft]);
}


public symbol_dump(func)
Symbol func;
{
  register Symbol s;
  register Integer i;

	printf(" symbols in %s \n",symname(func));
	for(i=0; i< HASHTABLESIZE; i++)
	   for(s=hashtab[i]; s != nil; s=s->next_sym)  {
		if (s->block == func) psym(s);
		}
}

/*
 * Free all the symbols currently allocated.
 */
public symbol_free()
{
    Sympool s, t;
    register Integer i;

    s = sympool;
    while (s != nil) {
	t = s->prevpool;
	dispose(s);
	s = t;
    }
    for (i = 0; i < HASHTABLESIZE; i++) {
	hashtab[i] = nil;
    }
    sympool = nil;
    nleft = 0;
}

/*
 * Create a new symbol with the given attributes.
 */

public Symbol newSymbol(name, blevel, class, type, chain)
Name name;
Integer blevel;
Symclass class;
Symbol type;
Symbol chain;
{
    register Symbol s;

    s = symbol_alloc();
    s->name = name;
    s->level = blevel;
    s->class = class;
    s->type = type;
    s->chain = chain;
    return s;
}

/*
 * Insert a symbol into the hash table.
 */

public Symbol insert(name)
Name name;
{
    register Symbol s;
    register unsigned int h;

    h = hash(name);
    s = symbol_alloc();
    s->name = name;
    s->next_sym = hashtab[h];
    hashtab[h] = s;
    return s;
}

/*
 * Symbol lookup.
 */

public Symbol lookup(name)
Name name;
{
    register Symbol s;
    register unsigned int h;

    h = hash(name);
    s = hashtab[h];
    while (s != nil and s->name != name) {
	s = s->next_sym;
    }
    return s;
}

/*
 * Delete a symbol from the symbol table.
 */

public delete (s)
Symbol s;
{
    register Symbol t;
    register unsigned int h;

    h = hash(s->name);
    t = hashtab[h];
    if (t == nil) {
	panic("delete of non-symbol '%s'", symname(s));
    } else if (t == s) {
	hashtab[h] = s->next_sym;
    } else {
	while (t->next_sym != s) {
	    t = t->next_sym;
	    if (t == nil) {
		panic("delete of non-symbol '%s'", symname(s));
	    }
	}
	t->next_sym = s->next_sym;
    }
}

/*
 * Dump out all the variables associated with the given
 * procedure, function, or program at the given recursive level.
 *
 * This is quite inefficient.  We traverse the entire symbol table
 * each time we're called.  The assumption is that this routine
 * won't be called frequently enough to merit improved performance.
 */

public dumpvars(f, frame)
Symbol f;
Frame frame;
{
    register Integer i;
    register Symbol s;

    for (i = 0; i < HASHTABLESIZE; i++) {
	for (s = hashtab[i]; s != nil; s = s->next_sym) {
	    if (container(s) == f) {
		if (should_print(s)) {
		    printv(s, frame);
		    putchar('\n');
		} else if (s->class == MODULE) {
		    dumpvars(s, frame);
		}
	    }
	}
    }
}

/*
 * Create a builtin type.
 * Builtin types are circular in that btype->type->type = btype.
 */

public Symbol maketype(name, lower, upper)
String name;
long lower;
long upper;
{
    register Symbol s;

    s = newSymbol(identname(name, true), 0, TYPE, nil, nil);
    s->language = primlang;
    s->type = newSymbol(nil, 0, RANGE, s, nil);
    s->type->language = s->language;
    s->type->symvalue.rangev.lower = lower;
    s->type->symvalue.rangev.upper = upper;
    return s;
}

/*
 * These functions are now compiled inline.
 *
 * public String symname(s)
Symbol s;
{
    checkref(s);
    return ident(s->name);
}

 *
 * public Address codeloc(f)
Symbol f;
{
    checkref(f);
    if (not isblock(f)) {
	panic("codeloc: \"%s\" is not a block", ident(f->name));
    }
    return f->symvalue.funcv.beginaddr;
}
 *
 */

/*
 * Reduce type to avoid worrying about type names.
 */

public Symbol rtype(type)
Symbol type;
{
    register Symbol t;

    t = type;
    if (t != nil) {
	if (t->class == VAR or t->class == FIELD or t->class == REF ) {
	    t = t->type;
	}
	if (t->class == TYPEREF) {
	    resolveRef(t);
	}
	while (t->class == TYPE or t->class == TAG) {
	    t = t->type;
	    if (t->class == TYPEREF) {
		resolveRef(t);
	    }
	}
    }
    return t;
}

/*
 * Find the end of a module name.  Return nil if there is none
 * in the given string.
 */

private String findModuleMark (s)
String s;
{
    register char *p, *r;
    register boolean done;

    p = s;
    done = false;
    do {
	if (*p == ':') {
	    done = true;
	    r = p;
	} else if (*p == '\0') {
	    done = true;
	    r = nil;
	} else {
	    ++p;
	}
    } while (not done);
    return r;
}

/*
 * Resolve a type reference by modifying to be the appropriate type.
 *
 * If the reference has a name, then it refers to an opaque type and
 * the actual type is directly accessible.  Otherwise, we must use
 * the type reference string, which is of the form "module:{module:}name".
 */

public resolveRef (t)
Symbol t;
{
    register char *p;
    char *start;
    Symbol s, m, outer;
    Name n;

    if (t->name != nil) {
	s = t;
    } else {
	start = t->symvalue.typeref;
	outer = program;
	p = findModuleMark(start);
	while (p != nil) {
	    *p = '\0';
	    n = identname(start, true);
	    find(m, n) where m->block == outer endfind(m);
	    if (m == nil) {
		p = nil;
		outer = nil;
		s = nil;
	    } else {
		outer = m;
		start = p + 1;
		p = findModuleMark(start);
	    }
	}
	if (outer != nil) {
	    n = identname(start, true);
	    find(s, n) where s->block == outer endfind(s);
	}
    }
    if (s != nil and s->type != nil) {
	t->name = s->type->name;
	t->class = s->type->class;
	t->type = s->type->type;
	t->chain = s->type->chain;
	t->symvalue = s->type->symvalue;
	t->block = s->type->block;
    }
}

public Integer level(s)
Symbol s;
{
    checkref(s);
    return s->level;
}

public Symbol container(s)
Symbol s;
{
    checkref(s);
    return s->block;
}

/*
 * Return the object address of the given symbol.
 *
 * There are the following possibilities:
 *
 *	globals		- just take offset
 *	locals		- take offset from locals base
 *	arguments	- take offset from argument base
 *	register	- offset is register number
 */

#define isglobal(s)		(s->level == 1)
#define islocaloff(s)		(s->level >= 2 and s->symvalue.offset < 0)
#define isparamoff(s)		(s->level >= 2 and s->symvalue.offset >= 0)
#define isreg(s)		(s->level < 0)

public Address address(s, frame)
Symbol s;
Frame frame;
{
    register Frame frp;
    register Address addr;
    register Symbol cur;

    checkref(s);
    if (not isactive(s->block)) {
	error("\"%s\" is not currently defined", symname(s));
    } else if (isglobal(s)) {
	addr = s->symvalue.offset;
    } else {
	frp = frame;
	if (frp == nil) {
	    cur = s->block;
	    while (cur != nil and cur->class == MODULE) {
		cur = cur->block;
	    }
	    if (cur == nil) {
		cur = whatblock(pc);
	    }
	    frp = findframe(cur);
	    if (frp == nil) {
		panic("unexpected nil frame for \"%s\"", symname(s));
	    }
	}
	if (islocaloff(s)) {
	    addr = locals_base(frp) + s->symvalue.offset;
	} else if (isparamoff(s)) {
	    addr = args_base(frp) + s->symvalue.offset;
	} else if (isreg(s)) {
	    addr = savereg(s->symvalue.offset, frp);
	} else {
	    panic("address: bad symbol \"%s\"", symname(s));
	}
    }
    return addr;
}

/*
 * Define a symbol used to access register values.
 */

public defregname(n, r)
Name n;
Integer r;
{
    register Symbol s, t;

    s = insert(n);
    t = newSymbol(nil, 0, PTR, t_int, nil);
    t->language = primlang;
    s->language = t->language;
    s->class = VAR;
    s->level = -3;
    s->type = t;
    s->block = program;
    s->symvalue.offset = r;
}

/*
 * Resolve an "abstract" type reference.
 *
 * It is possible in C to define a pointer to a type, but never define
 * the type in a particular source file.  Here we try to resolve
 * the type definition.  This is problematic, it is possible to
 * have multiple, different definitions for the same name type.
 */

public findtype(s)
Symbol s;
{
    register Symbol t, u, prev;

    u = s;
    prev = nil;
    while (u != nil and u->class != BADUSE) {
	if (u->name != nil) {
	    prev = u;
	}
	u = u->type;
    }
    if (prev == nil) {
	error("couldn't find link to type reference");
    }
    find(t, prev->name) where
	t != prev and t->block->class == MODULE and t->class == prev->class and
	t->type != nil and t->type->type != nil and
	t->type->type->class != BADUSE
    endfind(t);
    if (t == nil) {
	error("couldn't resolve reference");
    } else {
	prev->type = t->type;
    }
}

/*
 * Find the size in bytes of the given type.
 *
 * This is probably the WRONG thing to do.  The size should be kept
 * as an attribute in the symbol information as is done for structures
 * and fields.  I haven't gotten around to cleaning this up yet.
 */

#define MAXUCHAR 255
#define MAXUSHORT 65535L
#define MINCHAR -128
#define MAXCHAR 127
#define MINSHORT -32768
#define MAXSHORT 32767

/*
 * When necessary, compute the upper bound for an open array (Modula-2 style).
 */

public chkOpenArray (sym)
Symbol sym;
{
    Symbol t;
    Address a;
    integer n;

    if (sym->class == REF or sym->class == VAR) {
	t = rtype(sym->type);
	if (t->class == ARRAY and t->chain == t_open) {
	    a = address(sym, nil);
	    dread(&n, a + sizeof(Word), sizeof(n));
	    t->chain->type->symvalue.rangev.upper = n - 1;
	}
    }
}

public findbounds (u, lower, upper)
Symbol u;
long *lower, *upper;
{
    Rangetype lbt, ubt;
    long lb, ub;

    if (u->class == RANGE) {
	lbt = u->symvalue.rangev.lowertype;
	ubt = u->symvalue.rangev.uppertype;
	lb = u->symvalue.rangev.lower;
	ub = u->symvalue.rangev.upper;
	if (lbt == R_ARG or lbt == R_TEMP) {
	    if (not getbound(u, lb, lbt, lower)) {
		error("dynamic bounds not currently available");
	    }
	} else {
	    *lower = lb;
	}
	if (ubt == R_ARG or ubt == R_TEMP) {
	    if (not getbound(u, ub, ubt, upper)) {
		error("dynamic bounds not currently available");
	    }
	} else {
	    *upper = ub;
	}
    } else if (u->class == SCAL) {
	*lower = 0;
	*upper = u->symvalue.iconval - 1;
    } else {
	panic("unexpected array bound type");
    }
}

public integer size(sym)
Symbol sym;
{
    register Symbol s, t, u;
    register integer nel, elsize;
    long lower, upper;
    integer r, off, len;

    t = sym;
    checkref(t);
    if (t->class == TYPEREF) {
	resolveRef(t);
    }
    switch (t->class) {
	case RANGE:
	    lower = t->symvalue.rangev.lower;
	    upper = t->symvalue.rangev.upper;
	    if (upper == 0 and lower > 0) {
		/* real */
		r = lower;
	    } else if (lower > upper) {
		/* unsigned long */
		r = sizeof(long);
	    } else if (
  		(lower >= MINCHAR and upper <= MAXCHAR) or
  		(lower >= 0 and upper <= MAXUCHAR)
  	      ) {
		r = sizeof(char);
  	    } else if (
  		(lower >= MINSHORT and upper <= MAXSHORT) or
  		(lower >= 0 and upper <= MAXUSHORT)
  	      ) {
		r = sizeof(short);
	    } else {
		r = sizeof(long);
	    }
	    break;

	case ARRAY:
	    elsize = size(t->type);
	    nel = 1;
	    for (t = t->chain; t != nil; t = t->chain) {
		u = rtype(t);
		findbounds(u, &lower, &upper);
		nel *= (upper-lower+1);
	    }
	    r = nel*elsize;
	    break;

	case REF:
	case VAR:
	case FVAR:
	    chkOpenArray(t);
	    r = size(t->type);
	    /*
	     *
	    if (r < sizeof(Word) and isparam(t)) {
		r = sizeof(Word);
	    }
	    */
	    break;

	case CONST:
	    r = size(t->type);
	    break;

	case TYPE:
	    if (t->type->class == PTR and t->type->type->class == BADUSE) {
		findtype(t);
	    }
	    r = size(t->type);
	    break;

	case TAG:
	    r = size(t->type);
	    break;

	case FIELD:
	    off = t->symvalue.field.offset;
	    len = t->symvalue.field.length;
	    r = (off + len + 7) div 8 - (off div 8);
	    /* r = (t->symvalue.field.length + 7) div 8; */
	    break;

	case RECORD:
	case VARNT:
	    r = t->symvalue.offset;
	    if (r == 0 and t->chain != nil) {
		panic("missing size information for record");
	    }
	    break;

	case PTR:
	case FILET:
	    r = sizeof(Word);
	    break;

	case SCAL:
	    r = sizeof(Word);
	    /*
	     *
	    if (t->symvalue.iconval > 255) {
		r = sizeof(short);
	    } else {
		r = sizeof(char);
	    }
	     *
	     */
	    break;

	case FPROC:
	case FFUNC:
	    r = sizeof(Word);
	    break;

	case PROC:
	case FUNC:
	case MODULE:
	case PROG:
	    r = sizeof(Symbol);
	    break;

	case SET:
	    u = rtype(t->type);
	    switch (u->class) {
		case RANGE:
		    r = u->symvalue.rangev.upper - u->symvalue.rangev.lower + 1;
		    break;

		case SCAL:
		    r = u->symvalue.iconval;
		    break;

		default:
		    error("expected range for set base type");
		    break;
	    }
	    r = (r + BITSPERBYTE - 1) div BITSPERBYTE;
	    break;

	default:
	    if (ord(t->class) > ord(TYPEREF)) {
		panic("size: bad class (%d)", ord(t->class));
	    } else {
		fprintf(stderr, "!! size(%s) ??", classname(t));
	    }
	    r = 0;
	    break;
    }
    return r;
}

/*
 * Test if a symbol is a parameter.  This is true if there
 * is a cycle from s->block to s via chain pointers.
 */

public Boolean isparam(s)
Symbol s;
{
    register Symbol t;

    t = s->block;
    while (t != nil and t != s) {
	t = t->chain;
    }
    return (Boolean) (t != nil);
}

/*
 * Test if a type is an open array parameter type.
 */

public Boolean isopenarray (t)
Symbol t;
{
    return (Boolean) (t->class == ARRAY and t->chain == t_open);
}

/*
 * Test if a symbol is a var parameter, i.e. has class REF but
 * is not an open array parameter (those are treated special).
 */

public Boolean isvarparam(s)
Symbol s;
{
    return (Boolean) (s->class == REF);
}

/*
 * Test if a symbol is a variable (actually any addressible quantity
 * with do).
 */

public Boolean isvariable(s)
register Symbol s;
{
    return (Boolean) (s->class == VAR or s->class == FVAR or s->class == REF);
}

/*
 * Test if a symbol is a block, e.g. function, procedure, or the
 * main program.
 *
 * This function is now expanded inline for efficiency.
 *
 * public Boolean isblock(s)
register Symbol s;
{
    return (Boolean) (
	s->class == FUNC or s->class == PROC or
	s->class == MODULE or s->class == PROG
    );
}
 *
 */

/*
 * Test if a symbol is a module.
 */

public Boolean ismodule(s)
register Symbol s;
{
    return (Boolean) (s->class == MODULE);
}

/*
 * Test if a symbol is builtin, that is, a predefined type or
 * reserved word.
 */

public Boolean isbuiltin(s)
register Symbol s;
{
    return (Boolean) (s->level == 0 and s->class != PROG and s->class != VAR);
}

/*
 * Mark a procedure or function as internal, meaning that it is called
 * with a different calling sequence.
 */

public markInternal (s)
Symbol s;
{
    s->symvalue.funcv.intern = true;
}

public boolean isinternal (s)
Symbol s;
{
    return s->symvalue.funcv.intern;
}

/*
 * Test if two types match.
 * Equivalent names implies a match in any language.
 *
 * Special symbols must be handled with care.
 */

public Boolean compatible(t1, t2)
register Symbol t1, t2;
{
    Boolean b;
    Symbol rt1, rt2;

    if (t1 == t2) {
	b = true;
    } else if (t1 == nil or t2 == nil) {
	b = false;
    } else if (t1 == procsym) {
	b = isblock(t2);
    } else if (t2 == procsym) {
	b = isblock(t1);
    } else if (t1->language == primlang) {
	if (t2->language == primlang) {
	    rt1 = rtype(t1);
	    rt2 = rtype(t2);
	    b = (boolean) (
		(rt1->type == t_open and rt2->type == t_int) or
		(rt2->type == t_open and rt1->type == t_int) or
		rt1 == rt2
	    );
	} else {
	    b = (boolean) (*language_op(t2->language, L_TYPEMATCH))(t1, t2);
	}
    } else if (t2->language == primlang) {
	b = (boolean) (*language_op(t1->language, L_TYPEMATCH))(t1, t2);
    } else if (t1->language == nil) {
	if (t2->language == nil) {
	    b = false;
	} else {
	    b = (boolean) (*language_op(t2->language, L_TYPEMATCH))(t1, t2);
	}
    } else if (t2->language == nil) {
	b = (boolean) (*language_op(t1->language, L_TYPEMATCH))(t1, t2);
    } else if (isbuiltin(t1) or isbuiltin(t1->type)) {
	b = (boolean) (*language_op(t2->language, L_TYPEMATCH))(t1, t2);
    } else {
	b = (boolean) (*language_op(t1->language, L_TYPEMATCH))(t1, t2);
    }
    return b;
}

/*
 * Check for a type of the given name.
 */

public Boolean istypename(type, name)
Symbol type;
String name;
{
    Symbol t;
    Boolean b;

    t = type;
    checkref(t);
    b = (Boolean) (
	t->class == TYPE and streq(ident(t->name), name)
    );
    return b;
}

/*
 * Determine if a (value) parameter should actually be passed by address.
 */

public boolean passaddr (p, exprtype)
Symbol p, exprtype;
{
    boolean b;
    Language def;

    if (p == nil) {
	def = findlanguage(".c");
	b = (boolean) (*language_op(def, L_PASSADDR))(p, exprtype);
    } else if (p->language == nil or p->language == primlang) {
	b = false;
    } else if (isopenarray(p->type)) {
	b = true;
    } else {
	b = (boolean) (*language_op(p->language, L_PASSADDR))(p, exprtype);
    }
    return b;
}

/*
 * Test if the name of a symbol is uniquely defined or not.
 */

public Boolean isambiguous(s)
register Symbol s;
{
    register Symbol t;

    find(t, s->name) where t != s endfind(t);
    return (Boolean) (t != nil);
}

typedef char *Arglist;

#define nextarg(type)  ((type *) (ap += sizeof(type)))[-1]

private Symbol mkstring();
private Symbol namenode();

/*
 * Determine the type of a parse tree.
 * Also make some symbol-dependent changes to the tree such as
 * changing removing RVAL nodes for constant symbols.
 */

public assigntypes(p)
register Node p;
{
    register Node p1;
    register Symbol s;

    switch (p->op) {
	case O_SYM:
	    p->nodetype = namenode(p);
	    break;

	case O_LCON:
	    p->nodetype = t_int;
	    break;

	case O_FCON:
	    p->nodetype = t_real;
	    break;

	case O_SCON:
	    p->value.scon = strdup(p->value.scon);
	    s = mkstring(p->value.scon);
	    if (s == t_char) {
		p->op = O_LCON;
		p->value.lcon = p->value.scon[0];
	    }
	    p->nodetype = s;
	    break;

	case O_INDIR:
	    p1 = p->value.arg[0];
	    chkclass(p1, PTR);
	    p->nodetype = rtype(p1->nodetype)->type;
	    break;

	case O_DOT:
	    p->nodetype = p->value.arg[1]->value.sym;
	    break;

	case O_RVAL:
	    p1 = p->value.arg[0];
	    p->nodetype = p1->nodetype;
	    if (p1->op == O_SYM) {
		if (p1->nodetype->class == FUNC) {
		    p->op = O_CALL;
		    p->value.arg[1] = nil;
		} else if (p1->value.sym->class == CONST) {
		    if (compatible(p1->value.sym->type, t_real)) {
			p->op = O_FCON;
			p->value.fcon = p1->value.sym->symvalue.fconval;
			p->nodetype = t_real;
			dispose(p1);
		    } else {
			p->op = O_LCON;
			p->value.lcon = p1->value.sym->symvalue.iconval;
			p->nodetype = p1->value.sym->type;
			dispose(p1);
		    }
		} else if (isreg(p1->value.sym)) {
		    p->op = O_SYM;
		    p->value.sym = p1->value.sym;
		    dispose(p1);
		}
	    } else if (p1->op == O_INDIR and p1->value.arg[0]->op == O_SYM) {
		s = p1->value.arg[0]->value.sym;
		if (isreg(s)) {
		    p1->op = O_SYM;
		    dispose(p1->value.arg[0]);
		    p1->value.sym = s;
		    p1->nodetype = s;
		}
	    }
	    break;

	/*
	 * Perform a cast if the call is of the form "type(expr)".
	 */
	case O_CALL:
	    p1 = p->value.arg[0];
	    p->nodetype = rtype(p1->nodetype)->type;
	    break;

	case O_TYPERENAME:
	    p->nodetype = p->value.arg[1]->nodetype;
	    break;

	case O_ITOF:
	    p->nodetype = t_real;
	    break;

	case O_NEG:
	    s = p->value.arg[0]->nodetype;
	    if (not compatible(s, t_int)) {
		if (not compatible(s, t_real)) {
		    beginerrmsg();
		    fprintf(stderr, "\"");
		    prtree(stderr, p->value.arg[0]);
		    fprintf(stderr, "\" is improper type");
		    enderrmsg();
		} else {
		    p->op = O_NEGF;
		}
	    }
	    p->nodetype = s;
	    break;

	case O_ADD:
	case O_SUB:
	case O_MUL:
	    binaryop(p, nil);
	    break;

	case O_LT:
	case O_LE:
	case O_GT:
	case O_GE:
	case O_EQ:
	case O_NE:
	    binaryop(p, t_boolean);
	    break;

	case O_DIVF:
	    convert(&(p->value.arg[0]), t_real, O_ITOF);
	    convert(&(p->value.arg[1]), t_real, O_ITOF);
	    p->nodetype = t_real;
	    break;

	case O_DIV:
	case O_MOD:
	    convert(&(p->value.arg[0]), t_int, O_NOP);
	    convert(&(p->value.arg[1]), t_int, O_NOP);
	    p->nodetype = t_int;
	    break;

	case O_AND:
	case O_OR:
	    chkboolean(p->value.arg[0]);
	    chkboolean(p->value.arg[1]);
	    p->nodetype = t_boolean;
	    break;

	case O_QLINE:
	    p->nodetype = t_int;
	    break;

	default:
	    p->nodetype = nil;
	    break;
    }
}

/*
 * Process a binary arithmetic or relational operator.
 * Convert from integer to real if necessary.
 */

private binaryop (p, t)
Node p;
Symbol t;
{
    Node p1, p2;
    Boolean t1real, t2real;
    Symbol t1, t2;

    p1 = p->value.arg[0];
    p2 = p->value.arg[1];
    t1 = rtype(p1->nodetype);
    t2 = rtype(p2->nodetype);
    t1real = compatible(t1, t_real);
    t2real = compatible(t2, t_real);
    if (t1real or t2real) {
	p->op = (Operator) (ord(p->op) + 1);
	if (not t1real) {
	    p->value.arg[0] = build(O_ITOF, p1);
	} else if (not t2real) {
	    p->value.arg[1] = build(O_ITOF, p2);
	}
	p->nodetype = t_real;
    } else {
	if (size(p1->nodetype) > sizeof(integer)) {
	    beginerrmsg();
	    fprintf(stderr, "operation not defined on \"");
	    prtree(stderr, p1);
	    fprintf(stderr, "\"");
	    enderrmsg();
	} else if (size(p2->nodetype) > sizeof(integer)) {
	    beginerrmsg();
	    fprintf(stderr, "operation not defined on \"");
	    prtree(stderr, p2);
	    fprintf(stderr, "\"");
	    enderrmsg();
	}
	p->nodetype = t_int;
    }
    if (t != nil) {
	p->nodetype = t;
    }
}

/*
 * Create a node for a name.  The symbol for the name has already
 * been chosen, either implicitly with "which" or explicitly from
 * the dot routine.
 */

private Symbol namenode(p)
Node p;
{
    register Symbol r, s;
    register Node np;

    s = p->value.sym;
    if (s->class == REF) {
	np = new(Node);
	np->op = p->op;
	np->nodetype = s;
	np->value.sym = s;
	p->op = O_INDIR;
	p->value.arg[0] = np;
    }
/*
 * Old way
 *
    if (s->class == CONST or s->class == VAR or s->class == FVAR) {
	r = s->type;
    } else {
	r = s;
    }
 *
 */
    return s;
}

/*
 * Convert a tree to a type via a conversion operator;
 * if this isn't possible generate an error.
 *
 * Note the tree is call by address, hence the #define below.
 */

private convert(tp, typeto, op)
Node *tp;
Symbol typeto;
Operator op;
{
    Node tree;
    Symbol s, t;

    tree = *tp;
    s = rtype(tree->nodetype);
    t = rtype(typeto);
    if (compatible(t, t_real) and compatible(s, t_int)) {
	tree = build(op, tree);
    } else if (not compatible(s, t)) {
	beginerrmsg();
	fprintf(stderr, "expected integer or real, found \"");
	prtree(stderr, tree);
	fprintf(stderr, "\"");
	enderrmsg();
    } else if (op != O_NOP and s != t) {
	tree = build(op, tree);
    }
    *tp = tree;
}

/*
 * Construct a node for the dot operator.
 *
 * If the left operand is not a record, but rather a procedure
 * or function, then we interpret the "." as referencing an
 * "invisible" variable; i.e. a variable within a dynamically
 * active block but not within the static scope of the current procedure.
 */

public Node dot(record, fieldname)
Node record;
Name fieldname;
{
    register Node p;
    register Symbol s, t;

    if (isblock(record->nodetype)) {
	find(s, fieldname) where
	    s->block == record->nodetype and
	    s->class != FIELD and s->class != TAG
	endfind(s);
	if (s == nil) {
	    beginerrmsg();
	    fprintf(stderr, "\"%s\" is not defined in ", ident(fieldname));
	    printname(stderr, record->nodetype);
	    enderrmsg();
	}
	p = new(Node);
	p->op = O_SYM;
	p->value.sym = s;
	p->nodetype = namenode(p);
    } else {
	p = record;
	t = rtype(p->nodetype);
	if (t->class == PTR) {
	    s = findfield(fieldname, t->type);
	} else {
	    s = findfield(fieldname, t);
	}
	if (s == nil) {
	    beginerrmsg();
	    fprintf(stderr, "\"%s\" is not a field in ", ident(fieldname));
	    prtree(stderr, record);
	    enderrmsg();
	}
	if (t->class == PTR and not isreg(record->nodetype)) {
	    p = build(O_INDIR, record);
	}
	p = build(O_DOT, p, build(O_SYM, s));
    }
    return p;
}

/*
 * Return a tree corresponding to an array reference and do the
 * error checking.
 */

public Node subscript(a, slist)
Node a, slist;
{
    Symbol t;

    t = rtype(a->nodetype);
    if (t->language == nil) {
	error("unknown language");
    } else {
	return (Node) (*language_op(t->language, L_BUILDAREF))(a, slist);
    }
}

/*
 * Evaluate a subscript index.
 */

public int evalindex(s, i)
Symbol s;
long i;
{
    Symbol t;

    t = rtype(s);
    if (t->language == nil) {
	error("unknown language");
    } else {
	return ((*language_op(t->language, L_EVALAREF)) (s, i));
    }
}

/*
 * Check to see if a tree is boolean-valued, if not it's an error.
 */

public chkboolean(p)
register Node p;
{
    if (p->nodetype != t_boolean) {
	beginerrmsg();
	fprintf(stderr, "found ");
	prtree(stderr, p);
	fprintf(stderr, ", expected boolean expression");
	enderrmsg();
    }
}

/*
 * Check to make sure the given tree has a type of the given class.
 */

private chkclass(p, class)
Node p;
Symclass class;
{
    struct Symbol tmpsym;

    tmpsym.class = class;
    if (rtype(p->nodetype)->class != class) {
	beginerrmsg();
	fprintf(stderr, "\"");
	prtree(stderr, p);
	fprintf(stderr, "\" is not a %s", classname(&tmpsym));
	enderrmsg();
    }
}

/*
 * Construct a node for the type of a string.
 */

private Symbol mkstring(str)
String str;
{
    register char *p, *q;
    register Symbol s;
    integer len;

    p = str;
    q = str;
    while (*p != '\0') {
	if (*p == '\\') {
	    ++p;
	}
	*q = *p;
	++p;
	++q;
    }
    *q = '\0';
    len = p - str;
    if (len == 1) {
	s = t_char;
    } else {
	s = newSymbol(nil, 0, ARRAY, t_char, nil);
	s->language = primlang;
	s->chain = newSymbol(nil, 0, RANGE, t_int, nil);
	s->chain->language = s->language;
	s->chain->symvalue.rangev.lower = 1;
	s->chain->symvalue.rangev.upper = len + 1;
    }
    return s;
}

/*
 * Free up the space allocated for a string type.
 */

public unmkstring(s)
Symbol s;
{
    dispose(s->chain);
}

/*
 * Figure out the "current" variable or function being referred to,
 * this is either the active one or the most visible from the
 * current scope.
 */

public Symbol which(n)
Name n;
{
    register Symbol s, p, t, f;

    find(s, n)
	where s->class != FIELD and s->class != TAG and s->class != MODULE
    endfind(s);
    if (s == nil) {
	s = lookup(n);
    }
    if (s == nil) {
	error("\"%s\" is not defined", ident(n));
    } else if (s == program or isbuiltin(s)) {
	t = s;
    } else {
       /* start with current function */
	p = curfunc;
	do {
	    find(t, n) where
		t->block == p and t->class != FIELD and
		t->class != TAG and t->class != MODULE
	    endfind(t);
	    p = p->block;
	} while (t == nil and p != nil);
	if (t == nil) {
	    t = s;
	}
    }
    return t;
}

/*
 * Find the symbol which is has the same name and scope as the
 * given symbol but is of the given field.  Return nil if there is none.
 */

public Symbol findfield(fieldname, record)
Name fieldname;
Symbol record;
{
    register Symbol t;

    t = rtype(record)->chain;
    while (t != nil and t->name != fieldname) {
	t = t->chain;
    }
    return t;
}

public Boolean getbound(s,off,type,valp)
Symbol s;
int off;
Rangetype type;
int *valp;
{
    Frame frp;
    Address addr;
    Symbol cur;

    if (not isactive(s->block)) {
	return(false);
    }
    cur = s->block;
    while (cur != nil and cur->class == MODULE) {  /* WHY*/
    		cur = cur->block;
    }
    if(cur == nil) {
		cur = whatblock(pc);
    }
    frp = findframe(cur);
    if (frp == nil) {
	return(false);
    }
    if(type == R_TEMP) addr = locals_base(frp) + off;
    else if (type == R_ARG) addr = args_base(frp) + off;
    else return(false);
    dread(valp,addr,sizeof(long));
    return(true);
}
