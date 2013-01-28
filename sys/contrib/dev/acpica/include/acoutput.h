/******************************************************************************
 *
 * Name: acoutput.h -- debug output
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2013, Intel Corp.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 */

#ifndef __ACOUTPUT_H__
#define __ACOUTPUT_H__

/*
 * Debug levels and component IDs. These are used to control the
 * granularity of the output of the ACPI_DEBUG_PRINT macro -- on a
 * per-component basis and a per-exception-type basis.
 */

/* Component IDs are used in the global "DebugLayer" */

#define ACPI_UTILITIES              0x00000001
#define ACPI_HARDWARE               0x00000002
#define ACPI_EVENTS                 0x00000004
#define ACPI_TABLES                 0x00000008
#define ACPI_NAMESPACE              0x00000010
#define ACPI_PARSER                 0x00000020
#define ACPI_DISPATCHER             0x00000040
#define ACPI_EXECUTER               0x00000080
#define ACPI_RESOURCES              0x00000100
#define ACPI_CA_DEBUGGER            0x00000200
#define ACPI_OS_SERVICES            0x00000400
#define ACPI_CA_DISASSEMBLER        0x00000800

/* Component IDs for ACPI tools and utilities */

#define ACPI_COMPILER               0x00001000
#define ACPI_TOOLS                  0x00002000
#define ACPI_EXAMPLE                0x00004000
#define ACPI_DRIVER                 0x00008000
#define DT_COMPILER                 0x00010000
#define ASL_PREPROCESSOR            0x00020000

#define ACPI_ALL_COMPONENTS         0x0001FFFF
#define ACPI_COMPONENT_DEFAULT      (ACPI_ALL_COMPONENTS)

/* Component IDs reserved for ACPI drivers */

#define ACPI_ALL_DRIVERS            0xFFFF0000


/*
 * Raw debug output levels, do not use these in the ACPI_DEBUG_PRINT macros
 */
#define ACPI_LV_INIT                0x00000001
#define ACPI_LV_DEBUG_OBJECT        0x00000002
#define ACPI_LV_INFO                0x00000004
#define ACPI_LV_REPAIR              0x00000008
#define ACPI_LV_ALL_EXCEPTIONS      0x0000000F

/* Trace verbosity level 1 [Standard Trace Level] */

#define ACPI_LV_INIT_NAMES          0x00000020
#define ACPI_LV_PARSE               0x00000040
#define ACPI_LV_LOAD                0x00000080
#define ACPI_LV_DISPATCH            0x00000100
#define ACPI_LV_EXEC                0x00000200
#define ACPI_LV_NAMES               0x00000400
#define ACPI_LV_OPREGION            0x00000800
#define ACPI_LV_BFIELD              0x00001000
#define ACPI_LV_TABLES              0x00002000
#define ACPI_LV_VALUES              0x00004000
#define ACPI_LV_OBJECTS             0x00008000
#define ACPI_LV_RESOURCES           0x00010000
#define ACPI_LV_USER_REQUESTS       0x00020000
#define ACPI_LV_PACKAGE             0x00040000
#define ACPI_LV_VERBOSITY1          0x0007FF40 | ACPI_LV_ALL_EXCEPTIONS

/* Trace verbosity level 2 [Function tracing and memory allocation] */

#define ACPI_LV_ALLOCATIONS         0x00100000
#define ACPI_LV_FUNCTIONS           0x00200000
#define ACPI_LV_OPTIMIZATIONS       0x00400000
#define ACPI_LV_VERBOSITY2          0x00700000 | ACPI_LV_VERBOSITY1
#define ACPI_LV_ALL                 ACPI_LV_VERBOSITY2

/* Trace verbosity level 3 [Threading, I/O, and Interrupts] */

#define ACPI_LV_MUTEX               0x01000000
#define ACPI_LV_THREADS             0x02000000
#define ACPI_LV_IO                  0x04000000
#define ACPI_LV_INTERRUPTS          0x08000000
#define ACPI_LV_VERBOSITY3          0x0F000000 | ACPI_LV_VERBOSITY2

/* Exceptionally verbose output -- also used in the global "DebugLevel"  */

#define ACPI_LV_AML_DISASSEMBLE     0x10000000
#define ACPI_LV_VERBOSE_INFO        0x20000000
#define ACPI_LV_FULL_TABLES         0x40000000
#define ACPI_LV_EVENTS              0x80000000
#define ACPI_LV_VERBOSE             0xF0000000


/*
 * Debug level macros that are used in the DEBUG_PRINT macros
 */
#define ACPI_DEBUG_LEVEL(dl)        (UINT32) dl,ACPI_DEBUG_PARAMETERS

/*
 * Exception level -- used in the global "DebugLevel"
 *
 * Note: For errors, use the ACPI_ERROR or ACPI_EXCEPTION interfaces.
 * For warnings, use ACPI_WARNING.
 */
#define ACPI_DB_INIT                ACPI_DEBUG_LEVEL (ACPI_LV_INIT)
#define ACPI_DB_DEBUG_OBJECT        ACPI_DEBUG_LEVEL (ACPI_LV_DEBUG_OBJECT)
#define ACPI_DB_INFO                ACPI_DEBUG_LEVEL (ACPI_LV_INFO)
#define ACPI_DB_REPAIR              ACPI_DEBUG_LEVEL (ACPI_LV_REPAIR)
#define ACPI_DB_ALL_EXCEPTIONS      ACPI_DEBUG_LEVEL (ACPI_LV_ALL_EXCEPTIONS)

/* Trace level -- also used in the global "DebugLevel" */

#define ACPI_DB_INIT_NAMES          ACPI_DEBUG_LEVEL (ACPI_LV_INIT_NAMES)
#define ACPI_DB_THREADS             ACPI_DEBUG_LEVEL (ACPI_LV_THREADS)
#define ACPI_DB_PARSE               ACPI_DEBUG_LEVEL (ACPI_LV_PARSE)
#define ACPI_DB_DISPATCH            ACPI_DEBUG_LEVEL (ACPI_LV_DISPATCH)
#define ACPI_DB_LOAD                ACPI_DEBUG_LEVEL (ACPI_LV_LOAD)
#define ACPI_DB_EXEC                ACPI_DEBUG_LEVEL (ACPI_LV_EXEC)
#define ACPI_DB_NAMES               ACPI_DEBUG_LEVEL (ACPI_LV_NAMES)
#define ACPI_DB_OPREGION            ACPI_DEBUG_LEVEL (ACPI_LV_OPREGION)
#define ACPI_DB_BFIELD              ACPI_DEBUG_LEVEL (ACPI_LV_BFIELD)
#define ACPI_DB_TABLES              ACPI_DEBUG_LEVEL (ACPI_LV_TABLES)
#define ACPI_DB_FUNCTIONS           ACPI_DEBUG_LEVEL (ACPI_LV_FUNCTIONS)
#define ACPI_DB_OPTIMIZATIONS       ACPI_DEBUG_LEVEL (ACPI_LV_OPTIMIZATIONS)
#define ACPI_DB_VALUES              ACPI_DEBUG_LEVEL (ACPI_LV_VALUES)
#define ACPI_DB_OBJECTS             ACPI_DEBUG_LEVEL (ACPI_LV_OBJECTS)
#define ACPI_DB_ALLOCATIONS         ACPI_DEBUG_LEVEL (ACPI_LV_ALLOCATIONS)
#define ACPI_DB_RESOURCES           ACPI_DEBUG_LEVEL (ACPI_LV_RESOURCES)
#define ACPI_DB_IO                  ACPI_DEBUG_LEVEL (ACPI_LV_IO)
#define ACPI_DB_INTERRUPTS          ACPI_DEBUG_LEVEL (ACPI_LV_INTERRUPTS)
#define ACPI_DB_USER_REQUESTS       ACPI_DEBUG_LEVEL (ACPI_LV_USER_REQUESTS)
#define ACPI_DB_PACKAGE             ACPI_DEBUG_LEVEL (ACPI_LV_PACKAGE)
#define ACPI_DB_MUTEX               ACPI_DEBUG_LEVEL (ACPI_LV_MUTEX)
#define ACPI_DB_EVENTS              ACPI_DEBUG_LEVEL (ACPI_LV_EVENTS)

#define ACPI_DB_ALL                 ACPI_DEBUG_LEVEL (ACPI_LV_ALL)

/* Defaults for DebugLevel, debug and normal */

#define ACPI_DEBUG_DEFAULT          (ACPI_LV_INIT | ACPI_LV_DEBUG_OBJECT | ACPI_LV_REPAIR)
#define ACPI_NORMAL_DEFAULT         (ACPI_LV_INIT | ACPI_LV_DEBUG_OBJECT | ACPI_LV_REPAIR)
#define ACPI_DEBUG_ALL              (ACPI_LV_AML_DISASSEMBLE | ACPI_LV_ALL_EXCEPTIONS | ACPI_LV_ALL)


#if defined (ACPI_DEBUG_OUTPUT) || !defined (ACPI_NO_ERROR_MESSAGES)
/*
 * The module name is used primarily for error and debug messages.
 * The __FILE__ macro is not very useful for this, because it
 * usually includes the entire pathname to the module making the
 * debug output difficult to read.
 */
#define ACPI_MODULE_NAME(Name)          static const char ACPI_UNUSED_VAR _AcpiModuleName[] = Name;
#else
/*
 * For the no-debug and no-error-msg cases, we must at least define
 * a null module name.
 */
#define ACPI_MODULE_NAME(Name)
#define _AcpiModuleName ""
#endif

/*
 * Ascii error messages can be configured out
 */
#ifndef ACPI_NO_ERROR_MESSAGES
#define AE_INFO                         _AcpiModuleName, __LINE__

/*
 * Error reporting. Callers module and line number are inserted by AE_INFO,
 * the plist contains a set of parens to allow variable-length lists.
 * These macros are used for both the debug and non-debug versions of the code.
 */
#define ACPI_INFO(plist)                AcpiInfo plist
#define ACPI_WARNING(plist)             AcpiWarning plist
#define ACPI_EXCEPTION(plist)           AcpiException plist
#define ACPI_ERROR(plist)               AcpiError plist
#define ACPI_BIOS_WARNING(plist)        AcpiBiosWarning plist
#define ACPI_BIOS_ERROR(plist)          AcpiBiosError plist
#define ACPI_DEBUG_OBJECT(obj,l,i)      AcpiExDoDebugObject(obj,l,i)

#else

/* No error messages */

#define ACPI_INFO(plist)
#define ACPI_WARNING(plist)
#define ACPI_EXCEPTION(plist)
#define ACPI_ERROR(plist)
#define ACPI_BIOS_WARNING(plist)
#define ACPI_BIOS_ERROR(plist)
#define ACPI_DEBUG_OBJECT(obj,l,i)

#endif /* ACPI_NO_ERROR_MESSAGES */


/*
 * Debug macros that are conditionally compiled
 */
#ifdef ACPI_DEBUG_OUTPUT

/*
 * If ACPI_GET_FUNCTION_NAME was not defined in the compiler-dependent header,
 * define it now. This is the case where there the compiler does not support
 * a __FUNCTION__ macro or equivalent.
 */
#ifndef ACPI_GET_FUNCTION_NAME
#define ACPI_GET_FUNCTION_NAME          _AcpiFunctionName

/*
 * The Name parameter should be the procedure name as a quoted string.
 * The function name is also used by the function exit macros below.
 * Note: (const char) is used to be compatible with the debug interfaces
 * and macros such as __FUNCTION__.
 */
#define ACPI_FUNCTION_NAME(Name)        static const char _AcpiFunctionName[] = #Name;

#else
/* Compiler supports __FUNCTION__ (or equivalent) -- Ignore this macro */

#define ACPI_FUNCTION_NAME(Name)
#endif /* ACPI_GET_FUNCTION_NAME */

/*
 * Common parameters used for debug output functions:
 * line number, function name, module(file) name, component ID
 */
#define ACPI_DEBUG_PARAMETERS \
    __LINE__, ACPI_GET_FUNCTION_NAME, _AcpiModuleName, _COMPONENT

/* Check if debug output is currently dynamically enabled */

#define ACPI_IS_DEBUG_ENABLED(Level, Component) \
    ((Level & AcpiDbgLevel) && (Component & AcpiDbgLayer))

/*
 * Master debug print macros
 * Print message if and only if:
 *    1) Debug print for the current component is enabled
 *    2) Debug error level or trace level for the print statement is enabled
 *
 * November 2012: Moved the runtime check for whether to actually emit the
 * debug message outside of the print function itself. This improves overall
 * performance at a relatively small code cost. Implementation involves the
 * use of variadic macros supported by C99.
 *
 * Note: the ACPI_DO_WHILE0 macro is used to prevent some compilers from
 * complaining about these constructs. On other compilers the do...while
 * adds some extra code, so this feature is optional.
 */
#ifdef ACPI_USE_DO_WHILE_0
#define ACPI_DO_WHILE0(a)               do a while(0)
#else
#define ACPI_DO_WHILE0(a)               a
#endif

/* DEBUG_PRINT functions */

#define ACPI_DEBUG_PRINT(plist)         ACPI_ACTUAL_DEBUG plist
#define ACPI_DEBUG_PRINT_RAW(plist)     ACPI_ACTUAL_DEBUG_RAW plist

/* Helper macros for DEBUG_PRINT */

#define ACPI_DO_DEBUG_PRINT(Function, Level, Line, Filename, Modulename, Component, ...) \
    ACPI_DO_WHILE0 ({ \
        if (ACPI_IS_DEBUG_ENABLED (Level, Component)) \
        { \
            Function (Level, Line, Filename, Modulename, Component, __VA_ARGS__); \
        } \
    })

#define ACPI_ACTUAL_DEBUG(Level, Line, Filename, Modulename, Component, ...) \
    ACPI_DO_DEBUG_PRINT (AcpiDebugPrint, Level, Line, \
        Filename, Modulename, Component, __VA_ARGS__)

#define ACPI_ACTUAL_DEBUG_RAW(Level, Line, Filename, Modulename, Component, ...) \
    ACPI_DO_DEBUG_PRINT (AcpiDebugPrintRaw, Level, Line, \
        Filename, Modulename, Component, __VA_ARGS__)


/*
 * Function entry tracing
 *
 * The name of the function is emitted as a local variable that is
 * intended to be used by both the entry trace and the exit trace.
 */

/* Helper macro */

#define ACPI_TRACE_ENTRY(Name, Function, Type, Param) \
    ACPI_FUNCTION_NAME (Name) \
    Function (ACPI_DEBUG_PARAMETERS, (Type) (Param))

/* The actual entry trace macros */

#define ACPI_FUNCTION_TRACE(Name) \
    ACPI_FUNCTION_NAME(Name) \
    AcpiUtTrace (ACPI_DEBUG_PARAMETERS)

#define ACPI_FUNCTION_TRACE_PTR(Name, Pointer) \
    ACPI_TRACE_ENTRY (Name, AcpiUtTracePtr, void *, Pointer)

#define ACPI_FUNCTION_TRACE_U32(Name, Value) \
    ACPI_TRACE_ENTRY (Name, AcpiUtTraceU32, UINT32, Value)

#define ACPI_FUNCTION_TRACE_STR(Name, String) \
    ACPI_TRACE_ENTRY (Name, AcpiUtTraceStr, char *, String)

#define ACPI_FUNCTION_ENTRY() \
    AcpiUtTrackStackPtr()


/*
 * Function exit tracing
 *
 * These macros include a return statement. This is usually considered
 * bad form, but having a separate exit macro before the actual return
 * is very ugly and difficult to maintain.
 *
 * One of the FUNCTION_TRACE macros above must be used in conjunction
 * with these macros so that "_AcpiFunctionName" is defined.
 *
 * There are two versions of most of the return macros. The default version is
 * safer, since it avoids side-effects by guaranteeing that the argument will
 * not be evaluated twice.
 *
 * A less-safe version of the macros is provided for optional use if the
 * compiler uses excessive CPU stack (for example, this may happen in the
 * debug case if code optimzation is disabled.)
 */

/* Exit trace helper macro */

#ifndef ACPI_SIMPLE_RETURN_MACROS

#define ACPI_TRACE_EXIT(Function, Type, Param) \
    ACPI_DO_WHILE0 ({ \
        register Type _Param = (Type) (Param); \
        Function (ACPI_DEBUG_PARAMETERS, _Param); \
        return (_Param); \
    })

#else /* Use original less-safe macros */

#define ACPI_TRACE_EXIT(Function, Type, Param) \
    ACPI_DO_WHILE0 ({ \
        Function (ACPI_DEBUG_PARAMETERS, (Type) (Param)); \
        return (Param); \
    })

#endif /* ACPI_SIMPLE_RETURN_MACROS */

/* The actual exit macros */

#define return_VOID \
    ACPI_DO_WHILE0 ({ \
        AcpiUtExit (ACPI_DEBUG_PARAMETERS); \
        return; \
    })

#define return_ACPI_STATUS(Status) \
    ACPI_TRACE_EXIT (AcpiUtStatusExit, ACPI_STATUS, Status)

#define return_PTR(Pointer) \
    ACPI_TRACE_EXIT (AcpiUtPtrExit, void *, Pointer)

#define return_VALUE(Value) \
    ACPI_TRACE_EXIT (AcpiUtValueExit, UINT64, Value)


/* Conditional execution */

#define ACPI_DEBUG_EXEC(a)              a
#define ACPI_DEBUG_ONLY_MEMBERS(a)      a;
#define _VERBOSE_STRUCTURES


/* Various object display routines for debug */

#define ACPI_DUMP_STACK_ENTRY(a)        AcpiExDumpOperand((a), 0)
#define ACPI_DUMP_OPERANDS(a, b ,c)     AcpiExDumpOperands(a, b, c)
#define ACPI_DUMP_ENTRY(a, b)           AcpiNsDumpEntry (a, b)
#define ACPI_DUMP_PATHNAME(a, b, c, d)  AcpiNsDumpPathname(a, b, c, d)
#define ACPI_DUMP_BUFFER(a, b)          AcpiUtDebugDumpBuffer((UINT8 *) a, b, DB_BYTE_DISPLAY, _COMPONENT)

#else /* ACPI_DEBUG_OUTPUT */
/*
 * This is the non-debug case -- make everything go away,
 * leaving no executable debug code!
 */
#define ACPI_FUNCTION_NAME(a)
#define ACPI_DEBUG_PRINT(pl)
#define ACPI_DEBUG_PRINT_RAW(pl)
#define ACPI_DEBUG_EXEC(a)
#define ACPI_DEBUG_ONLY_MEMBERS(a)
#define ACPI_FUNCTION_TRACE(a)
#define ACPI_FUNCTION_TRACE_PTR(a, b)
#define ACPI_FUNCTION_TRACE_U32(a, b)
#define ACPI_FUNCTION_TRACE_STR(a, b)
#define ACPI_FUNCTION_EXIT
#define ACPI_FUNCTION_STATUS_EXIT(s)
#define ACPI_FUNCTION_VALUE_EXIT(s)
#define ACPI_FUNCTION_ENTRY()
#define ACPI_DUMP_STACK_ENTRY(a)
#define ACPI_DUMP_OPERANDS(a, b, c)
#define ACPI_DUMP_ENTRY(a, b)
#define ACPI_DUMP_TABLES(a, b)
#define ACPI_DUMP_PATHNAME(a, b, c, d)
#define ACPI_DUMP_BUFFER(a, b)
#define ACPI_DEBUG_PRINT(pl)
#define ACPI_DEBUG_PRINT_RAW(pl)
#define ACPI_IS_DEBUG_ENABLED(Level, Component) 0

/* Return macros must have a return statement at the minimum */

#define return_VOID                     return
#define return_ACPI_STATUS(s)           return(s)
#define return_VALUE(s)                 return(s)
#define return_PTR(s)                   return(s)

#endif /* ACPI_DEBUG_OUTPUT */


#endif /* __ACOUTPUT_H__ */
