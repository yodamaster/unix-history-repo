/* debug.h -- Describe generic debugging information.
   Copyright 1995, 1996 Free Software Foundation, Inc.
   Written by Ian Lance Taylor <ian@cygnus.com>.

   This file is part of GNU Binutils.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA.  */

#ifndef DEBUG_H
#define DEBUG_H

/* This header file describes a generic debugging information format.
   We may eventually have readers which convert different formats into
   this generic format, and writers which write it out.  The initial
   impetus for this was writing a convertor from stabs to HP IEEE-695
   debugging format.  */

/* Different kinds of types.  */

enum debug_type_kind
{
  /* Not used.  */
  DEBUG_KIND_ILLEGAL,
  /* Indirect via a pointer.  */
  DEBUG_KIND_INDIRECT,
  /* Void.  */
  DEBUG_KIND_VOID,
  /* Integer.  */
  DEBUG_KIND_INT,
  /* Floating point.  */
  DEBUG_KIND_FLOAT,
  /* Complex.  */
  DEBUG_KIND_COMPLEX,
  /* Boolean.  */
  DEBUG_KIND_BOOL,
  /* Struct.  */
  DEBUG_KIND_STRUCT,
  /* Union.  */
  DEBUG_KIND_UNION,
  /* Class.  */
  DEBUG_KIND_CLASS,
  /* Union class (can this really happen?).  */
  DEBUG_KIND_UNION_CLASS,
  /* Enumeration type.  */
  DEBUG_KIND_ENUM,
  /* Pointer.  */
  DEBUG_KIND_POINTER,
  /* Function.  */
  DEBUG_KIND_FUNCTION,
  /* Reference.  */
  DEBUG_KIND_REFERENCE,
  /* Range.  */
  DEBUG_KIND_RANGE,
  /* Array.  */
  DEBUG_KIND_ARRAY,
  /* Set.  */
  DEBUG_KIND_SET,
  /* Based pointer.  */
  DEBUG_KIND_OFFSET,
  /* Method.  */
  DEBUG_KIND_METHOD,
  /* Const qualified type.  */
  DEBUG_KIND_CONST,
  /* Volatile qualified type.  */
  DEBUG_KIND_VOLATILE,
  /* Named type.  */
  DEBUG_KIND_NAMED,
  /* Tagged type.  */
  DEBUG_KIND_TAGGED
};

/* Different kinds of variables.  */

enum debug_var_kind
{
  /* Not used.  */
  DEBUG_VAR_ILLEGAL,
  /* A global variable.  */
  DEBUG_GLOBAL,
  /* A static variable.  */
  DEBUG_STATIC,
  /* A local static variable.  */
  DEBUG_LOCAL_STATIC,
  /* A local variable.  */
  DEBUG_LOCAL,
  /* A register variable.  */
  DEBUG_REGISTER
};

/* Different kinds of function parameters.  */

enum debug_parm_kind
{
  /* Not used.  */
  DEBUG_PARM_ILLEGAL,
  /* A stack based parameter.  */
  DEBUG_PARM_STACK,
  /* A register parameter.  */
  DEBUG_PARM_REG,
  /* A stack based reference parameter.  */
  DEBUG_PARM_REFERENCE,
  /* A register reference parameter.  */
  DEBUG_PARM_REF_REG
};

/* Different kinds of visibility.  */

enum debug_visibility
{
  /* A public field (e.g., a field in a C struct).  */
  DEBUG_VISIBILITY_PUBLIC,
  /* A protected field.  */
  DEBUG_VISIBILITY_PROTECTED,
  /* A private field.  */
  DEBUG_VISIBILITY_PRIVATE,
  /* A field which should be ignored.  */
  DEBUG_VISIBILITY_IGNORE
};

/* A type.  */

typedef struct debug_type *debug_type;

#define DEBUG_TYPE_NULL ((debug_type) NULL)

/* A field in a struct or union.  */

typedef struct debug_field *debug_field;

#define DEBUG_FIELD_NULL ((debug_field) NULL)

/* A base class for an object.  */

typedef struct debug_baseclass *debug_baseclass;

#define DEBUG_BASECLASS_NULL ((debug_baseclass) NULL)

/* A method of an object.  */

typedef struct debug_method *debug_method;

#define DEBUG_METHOD_NULL ((debug_method) NULL)

/* The arguments to a method function of an object.  These indicate
   which method to run.  */

typedef struct debug_method_variant *debug_method_variant;

#define DEBUG_METHOD_VARIANT_NULL ((debug_method_variant) NULL)

/* This structure is passed to debug_write.  It holds function
   pointers that debug_write will call based on the accumulated
   debugging information.  */

struct debug_write_fns
{
  /* This is called at the start of each new compilation unit with the
     name of the main file in the new unit.  */
  boolean (*start_compilation_unit) PARAMS ((PTR, const char *));

  /* This is called at the start of each source file within a
     compilation unit, before outputting any global information for
     that file.  The argument is the name of the file.  */
  boolean (*start_source) PARAMS ((PTR, const char *));

  /* Each writer must keep a stack of types.  */

  /* Push an empty type onto the type stack.  This type can appear if
     there is a reference to a type which is never defined.  */
  boolean (*empty_type) PARAMS ((PTR));

  /* Push a void type onto the type stack.  */
  boolean (*void_type) PARAMS ((PTR));

  /* Push an integer type onto the type stack, given the size and
     whether it is unsigned.  */
  boolean (*int_type) PARAMS ((PTR, unsigned int, boolean));

  /* Push a floating type onto the type stack, given the size.  */
  boolean (*float_type) PARAMS ((PTR, unsigned int));

  /* Push a complex type onto the type stack, given the size.  */
  boolean (*complex_type) PARAMS ((PTR, unsigned int));

  /* Push a boolean type onto the type stack, given the size.  */
  boolean (*bool_type) PARAMS ((PTR, unsigned int));

  /* Push an enum type onto the type stack, given the tag, a NULL
     terminated array of names and the associated values.  If there is
     no tag, the tag argument will be NULL.  If this is an undefined
     enum, the names and values arguments will be NULL.  */
  boolean (*enum_type) PARAMS ((PTR, const char *, const char **,
				bfd_signed_vma *));

  /* Pop the top type on the type stack, and push a pointer to that
     type onto the type stack.  */
  boolean (*pointer_type) PARAMS ((PTR));

  /* Push a function type onto the type stack.  The second argument
     indicates the number of argument types that have been pushed onto
     the stack.  If the number of argument types is passed as -1, then
     the argument types of the function are unknown, and no types have
     been pushed onto the stack.  The third argument is true if the
     function takes a variable number of arguments.  The return type
     of the function is pushed onto the type stack below the argument
     types, if any.  */
  boolean (*function_type) PARAMS ((PTR, int, boolean));

  /* Pop the top type on the type stack, and push a reference to that
     type onto the type stack.  */
  boolean (*reference_type) PARAMS ((PTR));

  /* Pop the top type on the type stack, and push a range of that type
     with the given lower and upper bounds onto the type stack.  */
  boolean (*range_type) PARAMS ((PTR, bfd_signed_vma, bfd_signed_vma));

  /* Push an array type onto the type stack.  The top type on the type
     stack is the range, and the next type on the type stack is the
     element type.  These should be popped before the array type is
     pushed.  The arguments are the lower bound, the upper bound, and
     whether the array is a string.  */
  boolean (*array_type) PARAMS ((PTR, bfd_signed_vma, bfd_signed_vma,
				 boolean));

  /* Pop the top type on the type stack, and push a set of that type
     onto the type stack.  The argument indicates whether this set is
     a bitstring.  */
  boolean (*set_type) PARAMS ((PTR, boolean));

  /* Push an offset type onto the type stack.  The top type on the
     type stack is the target type, and the next type on the type
     stack is the base type.  These should be popped before the offset
     type is pushed.  */
  boolean (*offset_type) PARAMS ((PTR));

  /* Push a method type onto the type stack.  If the second argument
     is true, the top type on the stack is the class to which the
     method belongs; otherwise, the class must be determined by the
     class to which the method is attached.  The third argument is the
     number of argument types; these are pushed onto the type stack in
     reverse order (the first type popped is the last argument to the
     method).  A value of -1 for the third argument means that no
     argument information is available.  The fourth argument is true
     if the function takes a variable number of arguments.  The next
     type on the type stack below the domain and the argument types is
     the return type of the method.  All these types must be popped,
     and then the method type must be pushed.  */
  boolean (*method_type) PARAMS ((PTR, boolean, int, boolean));

  /* Pop the top type off the type stack, and push a const qualified
     version of that type onto the type stack.  */
  boolean (*const_type) PARAMS ((PTR));

  /* Pop the top type off the type stack, and push a volatile
     qualified version of that type onto the type stack.  */
  boolean (*volatile_type) PARAMS ((PTR));

  /* Start building a struct.  This is followed by calls to the
     struct_field function, and finished by a call to the
     end_struct_type function.  The second argument is the tag; this
     will be NULL if there isn't one.  If the second argument is NULL,
     the third argument is a constant identifying this struct for use
     with tag_type.  The fourth argument is true for a struct, false
     for a union.  The fifth argument is the size.  If this is an
     undefined struct or union, the size will be 0 and struct_field
     will not be called before end_struct_type is called.  */
  boolean (*start_struct_type) PARAMS ((PTR, const char *, unsigned int,
					boolean, unsigned int));

  /* Add a field to the struct type currently being built.  The type
     of the field should be popped off the type stack.  The arguments
     are the name, the bit position, the bit size (may be zero if the
     field is not packed), and the visibility.  */
  boolean (*struct_field) PARAMS ((PTR, const char *, bfd_vma, bfd_vma,
				   enum debug_visibility));

  /* Finish building a struct, and push it onto the type stack.  */
  boolean (*end_struct_type) PARAMS ((PTR));

  /* Start building a class.  This is followed by calls to several
     functions: struct_field, class_static_member, class_baseclass,
     class_start_method, class_method_variant,
     class_static_method_variant, and class_end_method.  The class is
     finished by a call to end_class_type.  The first five arguments
     are the same as for start_struct_type.  The sixth argument is
     true if there is a virtual function table; if there is, the
     seventh argument is true if the virtual function table can be
     found in the type itself, and is false if the type of the object
     holding the virtual function table should be popped from the type
     stack.  */
  boolean (*start_class_type) PARAMS ((PTR, const char *, unsigned int,
				       boolean, unsigned int, boolean,
				       boolean));

  /* Add a static member to the class currently being built.  The
     arguments are the field name, the physical name, and the
     visibility.  The type must be popped off the type stack.  */
  boolean (*class_static_member) PARAMS ((PTR, const char *, const char *,
					  enum debug_visibility));
  
  /* Add a baseclass to the class currently being built.  The type of
     the baseclass must be popped off the type stack.  The arguments
     are the bit position, whether the class is virtual, and the
     visibility.  */
  boolean (*class_baseclass) PARAMS ((PTR, bfd_vma, boolean,
				      enum debug_visibility));

  /* Start adding a method to the class currently being built.  This
     is followed by calls to class_method_variant and
     class_static_method_variant to describe different variants of the
     method which take different arguments.  The method is finished
     with a call to class_end_method.  The argument is the method
     name.  */
  boolean (*class_start_method) PARAMS ((PTR, const char *));

  /* Describe a variant to the class method currently being built.
     The type of the variant must be popped off the type stack.  The
     second argument is the physical name of the function.  The
     following arguments are the visibility, whether the variant is
     const, whether the variant is volatile, the offset in the virtual
     function table, and whether the context is on the type stack
     (below the variant type).  */
  boolean (*class_method_variant) PARAMS ((PTR, const char *,
					   enum debug_visibility,
					   boolean, boolean,
					   bfd_vma, boolean));

  /* Describe a static variant to the class method currently being
     built.  The arguments are the same as for class_method_variant,
     except that the last two arguments are omitted.  The type of the
     variant must be popped off the type stack.  */
  boolean (*class_static_method_variant) PARAMS ((PTR, const char *,
						  enum debug_visibility,
						  boolean, boolean));

  /* Finish describing a class method.  */
  boolean (*class_end_method) PARAMS ((PTR));

  /* Finish describing a class, and push it onto the type stack.  */
  boolean (*end_class_type) PARAMS ((PTR));

  /* Push a type on the stack which was given a name by an earlier
     call to typdef.  */
  boolean (*typedef_type) PARAMS ((PTR, const char *));

  /* Push a tagged type on the stack which was defined earlier.  If
     the second argument is not NULL, the type was defined by a call
     to tag.  If the second argument is NULL, the type was defined by
     a call to start_struct_type or start_class_type with a tag of
     NULL and the number of the third argument.  Either way, the
     fourth argument is the tag kind.  Note that this may be called
     for a struct (class) being defined, in between the call to
     start_struct_type (start_class_type) and the call to
     end_struct_type (end_class_type).  */
  boolean (*tag_type) PARAMS ((PTR, const char *, unsigned int,
			       enum debug_type_kind));

  /* Pop the type stack, and typedef it to the given name.  */
  boolean (*typdef) PARAMS ((PTR, const char *));

  /* Pop the type stack, and declare it as a tagged struct or union or
     enum or whatever.  The tag passed down here is redundant, since
     was also passed when enum_type, start_struct_type, or
     start_class_type was called.  */
  boolean (*tag) PARAMS ((PTR, const char *));

  /* This is called to record a named integer constant.  */
  boolean (*int_constant) PARAMS ((PTR, const char *, bfd_vma));

  /* This is called to record a named floating point constant.  */
  boolean (*float_constant) PARAMS ((PTR, const char *, double));

  /* This is called to record a typed integer constant.  The type is
     popped off the type stack.  */
  boolean (*typed_constant) PARAMS ((PTR, const char *, bfd_vma));

  /* This is called to record a variable.  The type is popped off the
     type stack.  */
  boolean (*variable) PARAMS ((PTR, const char *, enum debug_var_kind,
			       bfd_vma));

  /* Start writing out a function.  The return type must be popped off
     the stack.  The boolean is true if the function is global.  This
     is followed by calls to function_parameter, followed by block
     information.  */
  boolean (*start_function) PARAMS ((PTR, const char *, boolean));

  /* Record a function parameter for the current function.  The type
     must be popped off the stack.  */
  boolean (*function_parameter) PARAMS ((PTR, const char *,
					 enum debug_parm_kind, bfd_vma));

  /* Start writing out a block.  There is at least one top level block
     per function.  Blocks may be nested.  The argument is the
     starting address of the block.  */
  boolean (*start_block) PARAMS ((PTR, bfd_vma));

  /* Finish writing out a block.  The argument is the ending address
     of the block.  */
  boolean (*end_block) PARAMS ((PTR, bfd_vma));

  /* Finish writing out a function.  */
  boolean (*end_function) PARAMS ((PTR));

  /* Record line number information for the current compilation unit.  */
  boolean (*lineno) PARAMS ((PTR, const char *, unsigned long, bfd_vma));
};

/* Exported functions.  */

/* The first argument to most of these functions is a handle.  This
   handle is returned by the debug_init function.  The purpose of the
   handle is to permit the debugging routines to not use static
   variables, and hence to be reentrant.  This would be useful for a
   program which wanted to handle two executables simultaneously.  */

/* Return a debugging handle.  */

extern PTR debug_init PARAMS ((void));

/* Set the source filename.  This implicitly starts a new compilation
   unit.  */

extern boolean debug_set_filename PARAMS ((PTR, const char *));

/* Change source files to the given file name.  This is used for
   include files in a single compilation unit.  */

extern boolean debug_start_source PARAMS ((PTR, const char *));

/* Record a function definition.  This implicitly starts a function
   block.  The debug_type argument is the type of the return value.
   The boolean indicates whether the function is globally visible.
   The bfd_vma is the address of the start of the function.  Currently
   the parameter types are specified by calls to
   debug_record_parameter.  */

extern boolean debug_record_function
  PARAMS ((PTR, const char *, debug_type, boolean, bfd_vma));

/* Record a parameter for the current function.  */

extern boolean debug_record_parameter
  PARAMS ((PTR, const char *, debug_type, enum debug_parm_kind, bfd_vma));

/* End a function definition.  The argument is the address where the
   function ends.  */

extern boolean debug_end_function PARAMS ((PTR, bfd_vma));

/* Start a block in a function.  All local information will be
   recorded in this block, until the matching call to debug_end_block.
   debug_start_block and debug_end_block may be nested.  The argument
   is the address at which this block starts.  */

extern boolean debug_start_block PARAMS ((PTR, bfd_vma));

/* Finish a block in a function.  This matches the call to
   debug_start_block.  The argument is the address at which this block
   ends.  */

extern boolean debug_end_block PARAMS ((PTR, bfd_vma));

/* Associate a line number in the current source file with a given
   address.  */

extern boolean debug_record_line PARAMS ((PTR, unsigned long, bfd_vma));

/* Start a named common block.  This is a block of variables that may
   move in memory.  */

extern boolean debug_start_common_block PARAMS ((PTR, const char *));

/* End a named common block.  */

extern boolean debug_end_common_block PARAMS ((PTR, const char *));

/* Record a named integer constant.  */

extern boolean debug_record_int_const PARAMS ((PTR, const char *, bfd_vma));

/* Record a named floating point constant.  */

extern boolean debug_record_float_const PARAMS ((PTR, const char *, double));

/* Record a typed constant with an integral value.  */

extern boolean debug_record_typed_const
  PARAMS ((PTR, const char *, debug_type, bfd_vma));

/* Record a label.  */

extern boolean debug_record_label
  PARAMS ((PTR, const char *, debug_type, bfd_vma));

/* Record a variable.  */

extern boolean debug_record_variable
  PARAMS ((PTR, const char *, debug_type, enum debug_var_kind, bfd_vma));

/* Make an indirect type.  The first argument is a pointer to the
   location where the real type will be placed.  The second argument
   is the type tag, if there is one; this may be NULL; the only
   purpose of this argument is so that debug_get_type_name can return
   something useful.  This function may be used when a type is
   referenced before it is defined.  */

extern debug_type debug_make_indirect_type
  PARAMS ((PTR, debug_type *, const char *));

/* Make a void type.  */

extern debug_type debug_make_void_type PARAMS ((PTR));

/* Make an integer type of a given size.  The boolean argument is true
   if the integer is unsigned.  */

extern debug_type debug_make_int_type PARAMS ((PTR, unsigned int, boolean));

/* Make a floating point type of a given size.  FIXME: On some
   platforms, like an Alpha, you probably need to be able to specify
   the format.  */

extern debug_type debug_make_float_type PARAMS ((PTR, unsigned int));

/* Make a boolean type of a given size.  */

extern debug_type debug_make_bool_type PARAMS ((PTR, unsigned int));

/* Make a complex type of a given size.  */

extern debug_type debug_make_complex_type PARAMS ((PTR, unsigned int));

/* Make a structure type.  The second argument is true for a struct,
   false for a union.  The third argument is the size of the struct.
   The fourth argument is a NULL terminated array of fields.  */

extern debug_type debug_make_struct_type
  PARAMS ((PTR, boolean, bfd_vma, debug_field *));

/* Make an object type.  The first three arguments after the handle
   are the same as for debug_make_struct_type.  The next arguments are
   a NULL terminated array of base classes, a NULL terminated array of
   methods, the type of the object holding the virtual function table
   if it is not this object, and a boolean which is true if this
   object has its own virtual function table.  */

extern debug_type debug_make_object_type
  PARAMS ((PTR, boolean, bfd_vma, debug_field *, debug_baseclass *,
	   debug_method *, debug_type, boolean));

/* Make an enumeration type.  The arguments are a null terminated
   array of strings, and an array of corresponding values.  */

extern debug_type debug_make_enum_type
  PARAMS ((PTR, const char **, bfd_signed_vma *));

/* Make a pointer to a given type.  */

extern debug_type debug_make_pointer_type
  PARAMS ((PTR, debug_type));

/* Make a function type.  The second argument is the return type.  The
   third argument is a NULL terminated array of argument types.  The
   fourth argument is true if the function takes a variable number of
   arguments.  If the third argument is NULL, then the argument types
   are unknown.  */

extern debug_type debug_make_function_type
  PARAMS ((PTR, debug_type, debug_type *, boolean));

/* Make a reference to a given type.  */

extern debug_type debug_make_reference_type PARAMS ((PTR, debug_type));

/* Make a range of a given type from a lower to an upper bound.  */

extern debug_type debug_make_range_type
  PARAMS ((PTR, debug_type, bfd_signed_vma, bfd_signed_vma));

/* Make an array type.  The second argument is the type of an element
   of the array.  The third argument is the type of a range of the
   array.  The fourth and fifth argument are the lower and upper
   bounds, respectively (if the bounds are not known, lower should be
   0 and upper should be -1).  The sixth argument is true if this
   array is actually a string, as in C.  */

extern debug_type debug_make_array_type
  PARAMS ((PTR, debug_type, debug_type, bfd_signed_vma, bfd_signed_vma,
	   boolean));

/* Make a set of a given type.  For example, a Pascal set type.  The
   boolean argument is true if this set is actually a bitstring, as in
   CHILL.  */

extern debug_type debug_make_set_type PARAMS ((PTR, debug_type, boolean));

/* Make a type for a pointer which is relative to an object.  The
   second argument is the type of the object to which the pointer is
   relative.  The third argument is the type that the pointer points
   to.  */

extern debug_type debug_make_offset_type
  PARAMS ((PTR, debug_type, debug_type));

/* Make a type for a method function.  The second argument is the
   return type.  The third argument is the domain.  The fourth
   argument is a NULL terminated array of argument types.  The fifth
   argument is true if the function takes a variable number of
   arguments, in which case the array of argument types indicates the
   types of the first arguments.  The domain and the argument array
   may be NULL, in which case this is a stub method and that
   information is not available.  Stabs debugging uses this, and gets
   the argument types from the mangled name.  */

extern debug_type debug_make_method_type
  PARAMS ((PTR, debug_type, debug_type, debug_type *, boolean));

/* Make a const qualified version of a given type.  */

extern debug_type debug_make_const_type PARAMS ((PTR, debug_type));

/* Make a volatile qualified version of a given type.  */

extern debug_type debug_make_volatile_type PARAMS ((PTR, debug_type));

/* Make an undefined tagged type.  For example, a struct which has
   been mentioned, but not defined.  */

extern debug_type debug_make_undefined_tagged_type
  PARAMS ((PTR, const char *, enum debug_type_kind));

/* Make a base class for an object.  The second argument is the base
   class type.  The third argument is the bit position of this base
   class in the object.  The fourth argument is whether this is a
   virtual class.  The fifth argument is the visibility of the base
   class.  */

extern debug_baseclass debug_make_baseclass
  PARAMS ((PTR, debug_type, bfd_vma, boolean, enum debug_visibility));

/* Make a field for a struct.  The second argument is the name.  The
   third argument is the type of the field.  The fourth argument is
   the bit position of the field.  The fifth argument is the size of
   the field (it may be zero).  The sixth argument is the visibility
   of the field.  */

extern debug_field debug_make_field
  PARAMS ((PTR, const char *, debug_type, bfd_vma, bfd_vma,
	   enum debug_visibility));

/* Make a static member of an object.  The second argument is the
   name.  The third argument is the type of the member.  The fourth
   argument is the physical name of the member (i.e., the name as a
   global variable).  The fifth argument is the visibility of the
   member.  */

extern debug_field debug_make_static_member
  PARAMS ((PTR, const char *, debug_type, const char *,
	   enum debug_visibility));

/* Make a method.  The second argument is the name, and the third
   argument is a NULL terminated array of method variants.  Each
   method variant is a method with this name but with different
   argument types.  */

extern debug_method debug_make_method
  PARAMS ((PTR, const char *, debug_method_variant *));

/* Make a method variant.  The second argument is the physical name of
   the function.  The third argument is the type of the function,
   probably constructed by debug_make_method_type.  The fourth
   argument is the visibility.  The fifth argument is whether this is
   a const function.  The sixth argument is whether this is a volatile
   function.  The seventh argument is the index in the virtual
   function table, if any.  The eighth argument is the virtual
   function context.  */

extern debug_method_variant debug_make_method_variant
  PARAMS ((PTR, const char *, debug_type, enum debug_visibility, boolean,
	   boolean, bfd_vma, debug_type));

/* Make a static method argument.  The arguments are the same as for
   debug_make_method_variant, except that the last two are omitted
   since a static method can not also be virtual.  */

extern debug_method_variant debug_make_static_method_variant
  PARAMS ((PTR, const char *, debug_type, enum debug_visibility, boolean,
	   boolean));

/* Name a type.  This returns a new type with an attached name.  */

extern debug_type debug_name_type PARAMS ((PTR, const char *, debug_type));

/* Give a tag to a type, such as a struct or union.  This returns a
   new type with an attached tag.  */

extern debug_type debug_tag_type PARAMS ((PTR, const char *, debug_type));

/* Record the size of a given type.  */

extern boolean debug_record_type_size PARAMS ((PTR, debug_type, unsigned int));

/* Find a named type.  */

extern debug_type debug_find_named_type PARAMS ((PTR, const char *));

/* Find a tagged type.  */

extern debug_type debug_find_tagged_type
  PARAMS ((PTR, const char *, enum debug_type_kind));

/* Get the kind of a type.  */

extern enum debug_type_kind debug_get_type_kind PARAMS ((PTR, debug_type));

/* Get the name of a type.  */

extern const char *debug_get_type_name PARAMS ((PTR, debug_type));

/* Get the size of a type.  */

extern bfd_vma debug_get_type_size PARAMS ((PTR, debug_type));

/* Get the return type of a function or method type.  */

extern debug_type debug_get_return_type PARAMS ((PTR, debug_type));

/* Get the NULL terminated array of parameter types for a function or
   method type (actually, parameter types are not currently stored for
   function types).  This may be used to determine whether a method
   type is a stub method or not.  The last argument points to a
   boolean which is set to true if the function takes a variable
   number of arguments.  */

extern const debug_type *debug_get_parameter_types PARAMS ((PTR,
							    debug_type,
							    boolean *));

/* Get the target type of a pointer or reference or const or volatile
   type.  */

extern debug_type debug_get_target_type PARAMS ((PTR, debug_type));

/* Get the NULL terminated array of fields for a struct, union, or
   class.  */

extern const debug_field *debug_get_fields PARAMS ((PTR, debug_type));

/* Get the type of a field.  */

extern debug_type debug_get_field_type PARAMS ((PTR, debug_field));

/* Get the name of a field.  */

extern const char *debug_get_field_name PARAMS ((PTR, debug_field));

/* Get the bit position of a field within the containing structure.
   If the field is a static member, this will return (bfd_vma) -1.  */

extern bfd_vma debug_get_field_bitpos PARAMS ((PTR, debug_field));

/* Get the bit size of a field.  If the field is a static member, this
   will return (bfd_vma) -1.  */

extern bfd_vma debug_get_field_bitsize PARAMS ((PTR, debug_field));

/* Get the visibility of a field.  */

extern enum debug_visibility debug_get_field_visibility
  PARAMS ((PTR, debug_field));

/* Get the physical name of a field, if it is a static member.  If the
   field is not a static member, this will return NULL.  */

extern const char *debug_get_field_physname PARAMS ((PTR, debug_field));

/* Write out the recorded debugging information.  This takes a set of
   function pointers which are called to do the actual writing.  The
   first PTR is the debugging handle.  The second PTR is a handle
   which is passed to the functions.  */

extern boolean debug_write PARAMS ((PTR, const struct debug_write_fns *, PTR));

#endif /* DEBUG_H */
