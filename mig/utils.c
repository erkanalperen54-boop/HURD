/*
 * Mach Operating System
 * Copyright (c) 1991,1990 Carnegie Mellon University
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie Mellon
 * the rights to redistribute these changes.
 */

#include <stdarg.h>

#include "write.h"
#include "utils.h"
#include "global.h"
#include "cpu.h"

void
WriteImport(FILE *file, const_string_t filename)
{
    fprintf(file, "#include %s\n", filename);
}

void
WriteRCSDecl(FILE *file, identifier_t name, const_string_t rcs)
{
    fprintf(file, "#if\tUseExternRCSId\n");
    fprintf(file, "char %s_rcsid[] = %s;\n", name, rcs);
    fprintf(file, "#else\t/* UseExternRCSId */\n");
    fprintf(file, "static char rcsid[] = %s;\n", rcs);
    fprintf(file, "#endif\t/* UseExternRCSId */\n");
    fprintf(file, "\n");
}

void
WriteBogusDefines(FILE *file)
{
    fprintf(file, "#ifndef\tmig_internal\n");
    fprintf(file, "#define\tmig_internal\tstatic\n");
    fprintf(file, "#endif\n");
    fprintf(file, "\n");

    fprintf(file, "#ifndef\tmig_external\n");
    fprintf(file, "#define mig_external\n");
    fprintf(file, "#endif\n");
    fprintf(file, "\n");

    fprintf(file, "#ifndef\tmig_unlikely\n");
    fprintf(file, "#define\tmig_unlikely(X)\t__builtin_expect (!! (X), 0)\n");
    fprintf(file, "#endif\n");
    fprintf(file, "\n");

    fprintf(file, "#ifndef\tTypeCheck\n");
    fprintf(file, "#define\tTypeCheck 1\n");
    fprintf(file, "#endif\n");
    fprintf(file, "\n");

    fprintf(file, "#ifndef\tUseExternRCSId\n");
    fprintf(file, "#define\tUseExternRCSId\t\t1\n");
    fprintf(file, "#endif\n");
    fprintf(file, "\n");

    fprintf(file, "#define BAD_TYPECHECK(type, check) mig_unlikely (({\\\n");
    fprintf(file,
	    "  union { mach_msg_type_t t; uint%d_t w; } _t, _c;\\\n", desired_complex_alignof * 8);
    fprintf(file,
	    "  _t.t = *(type); _c.t = *(check);_t.w != _c.w; }))\n");
}

void
WriteList(FILE *file, const argument_t *args, write_list_fn_t *func, u_int mask,
	  const char *between, const char *after)
{
    const argument_t *arg;
    bool sawone = false;

    for (arg = args; arg != argNULL; arg = arg->argNext)
	if (akCheckAll(arg->argKind, mask))
	{
	    if (sawone)
		fprintf(file, "%s", between);
	    sawone = true;

	    (*func)(file, arg);
	}

    if (sawone)
	fprintf(file, "%s", after);
}

static bool
WriteReverseListPrim(FILE *file, const argument_t *arg,
		     write_list_fn_t *func, u_int mask, const char *between)
{
    bool sawone = false;

    if (arg != argNULL)
    {
	sawone = WriteReverseListPrim(file, arg->argNext, func, mask, between);

	if (akCheckAll(arg->argKind, mask))
	{
	    if (sawone)
		fprintf(file, "%s", between);
	    sawone = true;

	    (*func)(file, arg);
	}
    }

    return sawone;
}

void
WriteReverseList(FILE *file, const argument_t *args, write_list_fn_t *func,
		 u_int mask, const char *between, const char *after)
{
    bool sawone;

    sawone = WriteReverseListPrim(file, args, func, mask, between);

    if (sawone)
	fprintf(file, "%s", after);
}

void
WriteNameDecl(FILE *file, const argument_t *arg)
{
    fprintf(file, "%s", arg->argVarName);
}

/* Returns whether parameter should be qualified with const because we will only
   send the pointed data, not receive it. */
static bool
UserVarConst(const argument_t *arg)
{
    return (arg->argKind & (akbSend|akbReturn)) == akbSend
	    && !arg->argType->itStruct;
}

static const char *
UserVarQualifier(const argument_t *arg)
{
    if (!UserVarConst(arg))
	return "";

    const ipc_type_t *it = arg->argType;

    if (it->itIndefinite ||
	it->itInName == MACH_MSG_TYPE_STRING_C ||
	(it->itVarArray && !strcmp(it->itElement->itUserType, "char")) ||
	!strcmp(it->itUserType, "string_t"))
        /* This is a pointer, so we have to use the const_foo type to
	   make const qualify the data, not the pointer.

	   Or this is a pointer to a variable array. For now we only support arrays of char
	   but we can remove that condition if we define const typedefs for all types that
	   require it.

	   Or this is a string_t, which should use const_string_t to avoid
	   forcing the caller to respect the definite string size */
	return "const_";
    else
	return "const ";
}

void
WriteUserVarDecl(FILE *file, const argument_t *arg)
{
    const ipc_type_t *it = arg->argType;

    if (it->itInLine && it->itVarArray && !it->itIndefinite &&
	!UserVarConst(arg) &&
	!strcmp(it->itElement->itUserType, "char"))
    {
	/* For variable arrays like "array[*:128] of char" we prefer to use "char *param"
	 * as the argument since it is more standard than using "char param[128]".
	 */
	fprintf(file, "\tchar *%s /* max of %d elements */", arg->argVarName, it->itNumber);
    } else {
	const char *qualif = UserVarQualifier(arg);
	const char *ref = arg->argByReferenceUser ? "*" : "";
	fprintf(file, "\t%s%s %s%s", qualif, it->itUserType, ref, arg->argVarName);
    }
}

/* Returns whether parameter should be qualified with const because we will only
   receive the pointed data, not modify it. */
static bool
ServerVarConst(const argument_t *arg)
{
    return (arg->argKind & (akbSend|akbReturn)) == akbSend
	    && !arg->argType->itStruct;
}

const char *
ServerVarQualifier(const argument_t *arg)
{
    if (!ServerVarConst(arg))
	return "";

    if (arg->argType->itIndefinite ||
	arg->argType->itInName == MACH_MSG_TYPE_STRING_C ||
	!strcmp(arg->argType->itTransType, "string_t"))
        /* This is a pointer, so we have to use the const_foo type to
	   make const qualify the data, not the pointer.

	   Or this is a string_t, which should use const_string_t to avoid
	   forcing the caller to respect the definite string size */
	return "const_";
    else
	return "const ";
}

void
WriteServerVarDecl(FILE *file, const argument_t *arg)
{
    const char *qualif = ServerVarQualifier(arg);
    const char *ref = arg->argByReferenceServer ? "*" : "";

    fprintf(file, "\t%s%s %s%s",
	    qualif, arg->argType->itTransType, ref, arg->argVarName);
}

void
WriteTypeDeclInServer(FILE *file, const argument_t *arg)
{
    WriteStaticDecl(file, arg->argType,
		    arg->argType->itIndefinite ? d_NO : arg->argDeallocate,
		    arg->argLongForm, /*is_server=*/true, true, arg->argTTName);
}

void
WriteTypeDeclOutServer(FILE *file, const argument_t *arg)
{
    WriteStaticDecl(file, arg->argType,
		    arg->argType->itIndefinite ? d_NO : arg->argDeallocate,
		    arg->argLongForm, /*is_server=*/true, false, arg->argTTName);
}

void
WriteTypeDeclInUser(FILE *file, const argument_t *arg)
{
    WriteStaticDecl(file, arg->argType,
		    arg->argType->itIndefinite ? d_NO : arg->argDeallocate,
		    arg->argLongForm, /*is_server=*/false, true, arg->argTTName);
}

void
WriteTypeDeclOutUser(FILE *file, const argument_t *arg)
{
    WriteStaticDecl(file, arg->argType,
		    arg->argType->itIndefinite ? d_NO : arg->argDeallocate,
		    arg->argLongForm, /*is_server=*/false, false, arg->argTTName);
}

void
WriteCheckDecl(FILE *file, const argument_t *arg)
{
    const ipc_type_t *it = arg->argType;

    /* We'll only be called for short-form types.
       Note we use itOutNameStr instead of itInNameStr, because
       this declaration will be used to check received types. */

    fprintf(file, "\tconst mach_msg_type_t %sCheck = {\n",
	    arg->argVarName);
    fprintf(file, "\t\t.msgt_name =\t\t(unsigned char) %s,\n", it->itOutNameStr);
    fprintf(file, "\t\t.msgt_size =\t\t%d,\n", it->itSize);
    fprintf(file, "\t\t.msgt_number =\t\t%d,\n", it->itNumber);
    fprintf(file, "\t\t.msgt_inline =\t\t%s,\n",
	    strbool(it->itInLine));
    fprintf(file, "\t\t.msgt_longform =\t\tFALSE,\n");
    fprintf(file, "\t\t.msgt_deallocate =\t\t%s,\n",
	    strbool(!it->itInLine));
    fprintf(file, "\t\t.msgt_unused =\t\t0\n");
    fprintf(file, "\t};\n");
}

const char *
ReturnTypeStr(const routine_t *rt)
{
    if (rt->rtReturn == argNULL)
	return "void";
    else
	return rt->rtReturn->argType->itUserType;
}

const char *
FetchUserType(const ipc_type_t *it)
{
    return it->itUserType;
}

const char *
FetchServerType(const ipc_type_t *it)
{
    return it->itServerType;
}

void
WriteFieldDeclPrim(FILE *file, const argument_t *arg,
		   const char *(*tfunc)(const ipc_type_t *))
{
    const ipc_type_t *it = arg->argType;

    fprintf(file, "\t\tmach_msg_type_%st %s;\n",
	    arg->argLongForm ? "long_" : "", arg->argTTName);

    /* Pad mach_msg_type_t/mach_msg_type_long_t in case we need alignment by more than its size. */
    if (!arg->argLongForm && sizeof_mach_msg_type_t % complex_alignof) {
        fprintf(file, "\t\tchar %s_pad[%zd];\n",
		arg->argTTName, complex_alignof - sizeof_mach_msg_type_t % complex_alignof);
    } else if (arg->argLongForm && sizeof_mach_msg_type_long_t % complex_alignof) {
	fprintf(file, "\t\tchar %s_pad[%zd];\n", arg->argTTName,
		complex_alignof - sizeof_mach_msg_type_long_t % complex_alignof);
    }

    if (it->itInLine && it->itVarArray)
    {
	ipc_type_t *btype = it->itElement;
	identifier_t original_type_name = (*tfunc)(btype);
	identifier_t inlined_type_name = btype->itUserlandPort ?
	    "mach_port_name_inlined_t" : original_type_name;

	/*
	 *	Build our own declaration for a varying array:
	 *	use the element type and maximum size specified.
	 *	Note arg->argCount->argMultiplier == btype->itNumber.
	 */
	fprintf(file, "\t\tunion {\n");
	fprintf(file, "\t\t\t%s %s[%d];\n",
			inlined_type_name,
			arg->argMsgField,
			it->itNumber/btype->itNumber);
	fprintf(file, "\t\t\t%s%s *%s%s;\n",
			tfunc == FetchUserType && UserVarConst(arg)
				? "const " : "",
			original_type_name,
			arg->argMsgField,
			OOLPostfix);
	fprintf(file, "\t\t};");
    }
    else
    {
        identifier_t original_type_name = (*tfunc)(it);
        identifier_t final_type_name = it->itUserlandPort && it->itInLine ?
	    "mach_port_name_inlined_t" : original_type_name;

        fprintf(file, "\t\t%s %s;",
		final_type_name,
                arg->argMsgField);
    }

    if (it->itPadSize != 0)
	fprintf(file, "\n\t\tchar %s[%d];", arg->argPadName, it->itPadSize);
}

void
WriteStructDecl(FILE *file, const argument_t *args, write_list_fn_t *func,
		u_int mask, const char *name)
{
    fprintf(file, "\ttypedef struct {\n");
    fprintf(file, "\t\tmach_msg_header_t Head;\n");
    WriteList(file, args, func, mask, "\n", "\n");
    fprintf(file, "\t} %s;\n", name);
    fprintf(file, "\n");
}

static void
WriteStaticLongDecl(FILE *file, const ipc_type_t *it,
		    dealloc_t dealloc, bool inname, identifier_t name)
{
    const_string_t msgt_name = inname ? it->itInNameStr : it->itOutNameStr;
    fprintf(file, "\tconst mach_msg_type_long_t %s = {\n", name);
    fprintf(file, "\t\t.msgtl_header = {\n");
    if (IS_64BIT_ABI) {
	/* For the 64 bit ABI we don't really have mach_msg_type_long_t
	 * so we fill mach_msg_type_long_t just like mach_msg_type_t.
	 */
	fprintf(file, "\t\t\t.msgt_name =\t\t(unsigned char) %s,\n", msgt_name);
	/* In case we are passing out of line ports, we always send as a contiguous array of port names
	 * rather than mach_port_name_inlined_t. */
	const u_int true_size = (it->itUserlandPort && !it->itInLine && it->itNumber == 0) ?
	    port_name_size_in_bits : it->itSize;
	fprintf(file, "\t\t\t.msgt_size =\t\t%d,\n", true_size);
	fprintf(file, "\t\t\t.msgt_number =\t\t%d,\n", it->itNumber);
    } else {
	fprintf(file, "\t\t\t.msgt_name =\t\t0,\n");
	fprintf(file, "\t\t\t.msgt_size =\t\t0,\n");
	fprintf(file, "\t\t\t.msgt_number =\t\t0,\n");
    }
    fprintf(file, "\t\t\t.msgt_inline =\t\t%s,\n",
	    strbool(it->itInLine));
    fprintf(file, "\t\t\t.msgt_longform =\t\tTRUE,\n");
    fprintf(file, "\t\t\t.msgt_deallocate =\t\t%s,\n",
	    strdealloc(dealloc));
    fprintf(file, "\t\t\t.msgt_unused =\t\t0\n");
    fprintf(file, "\t\t},\n");
    if (!IS_64BIT_ABI) {
	fprintf(file, "\t\t.msgtl_name =\t(unsigned short) %s,\n", msgt_name);
	fprintf(file, "\t\t.msgtl_size =\t%d,\n", it->itSize);
	fprintf(file, "\t\t.msgtl_number =\t%d,\n", it->itNumber);
    }
    fprintf(file, "\t};\n");
}

static void
WriteStaticShortDecl(FILE *file, const ipc_type_t *it,
		     dealloc_t dealloc, bool is_server, bool inname,
		     identifier_t name)
{
    fprintf(file, "\tconst mach_msg_type_t %s = {\n", name);
    fprintf(file, "\t\t.msgt_name =\t\t(unsigned char) %s,\n",
	    inname ? it->itInNameStr : it->itOutNameStr);
    fprintf(file, "\t\t.msgt_size =\t\t%d,\n", it->itSize);
    fprintf(file, "\t\t.msgt_number =\t\t%d,\n", it->itNumber);
    fprintf(file, "\t\t.msgt_inline =\t\t%s,\n",
	    strbool(it->itInLine));
    fprintf(file, "\t\t.msgt_longform =\t\tFALSE,\n");
    fprintf(file, "\t\t.msgt_deallocate =\t\t%s,\n",
	    strdealloc(dealloc));
    fprintf(file, "\t\t.msgt_unused =\t\t0\n");
    fprintf(file, "\t};\n");
    if (it->itInLine && !it->itVarArray) {
        identifier_t type = is_server ? FetchServerType(it) : FetchUserType(it);
        identifier_t actual_type = it->itUserlandPort ? "mach_port_name_inlined_t" : type;
        const u_int size_bytes = it->itSize >> 3;
        fprintf(file, "\t_Static_assert(sizeof(%s) == %d * %d, \"expected %s to be size %d * %d\");\n",
                      actual_type, size_bytes, it->itNumber,
                      actual_type, size_bytes, it->itNumber);
    }
}

void
WriteStaticDecl(FILE *file, const ipc_type_t *it, dealloc_t dealloc,
		bool longform, bool is_server, bool inname,
		identifier_t name)
{
    if (longform)
	WriteStaticLongDecl(file, it, dealloc, inname, name);
    else
	WriteStaticShortDecl(file, it, dealloc, is_server, inname, name);
}

/*
 * Like vfprintf, but omits a leading comment in the format string
 * and skips the items that would be printed by it.  Only %s, %d,
 * and %f are recognized.
 */
static void
SkipVFPrintf(FILE *file, const char *fmt, va_list pvar)
{
    if (*fmt == 0)
	return;	/* degenerate case */

    if (fmt[0] == '/' && fmt[1] == '*') {
	/* Format string begins with C comment.  Scan format
	   string until end-comment delimiter, skipping the
	   items in pvar that the enclosed format items would
	   print. */

	int c;

	fmt += 2;
	for (;;) {
	    c = *fmt++;
	    if (c == 0)
		return;	/* nothing to format */
	    if (c == '*') {
		if (*fmt == '/') {
		    break;
		}
	    }
	    else if (c == '%') {
		/* Field to skip */
		c = *fmt++;
		switch (c) {
		    case 's':
			(void) va_arg(pvar, char *);
			break;
		    case 'd':
			(void) va_arg(pvar, int);
			break;
		    case 'f':
			(void) va_arg(pvar, double);
			break;
		    case '\0':
			return; /* error - fmt ends with '%' */
		    default:
			break;
		}
	    }
	}
	/* End of comment.  To be pretty, skip
	   the space that follows. */
	fmt++;
	if (*fmt == ' ')
	    fmt++;
    }

    /* Now format the string. */
    (void) vfprintf(file, fmt, pvar);
}

#define do_skip_vfprintf(file, fmt, lastarg) do {			      \
    va_list ap;								      \
    va_start(ap, lastarg);						      \
    SkipVFPrintf(file, fmt, ap);					      \
    va_end (ap);							      \
} while (0)

void
WriteCopyType(FILE *file, const ipc_type_t *it, const char *left,
	      const char *right, ...)
{
    if (it->itStruct)
    {
	fprintf(file, "\t");
	do_skip_vfprintf(file, left, right);
	fprintf(file, " = ");
	do_skip_vfprintf(file, right, right);
	fprintf(file, ";\n");
    }
    else if (it->itString)
    {
	fprintf(file, "\t(void) %smig_strncpy(", SubrPrefix);
	do_skip_vfprintf(file, left, right);
	fprintf(file, ", ");
	do_skip_vfprintf(file, right, right);
	fprintf(file, ", %d);\n", it->itTypeSize);
    }
    else
    {
	fprintf(file, "\tmemcpy(");
	do_skip_vfprintf(file, left, right);
	fprintf(file, ", ");
	do_skip_vfprintf(file, right, right);
	fprintf(file, ", %d);\n", it->itTypeSize);
    }
}

void
WritePackMsgType(FILE *file, const ipc_type_t *it, dealloc_t dealloc,
		 bool longform, bool inname, const char *left,
		 const char *right, ...)
{
    fprintf(file, "\t");
    do_skip_vfprintf(file, left, right);
    fprintf(file, " = ");
    do_skip_vfprintf(file, right, right);
    fprintf(file, ";\n");
}
