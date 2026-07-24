/*
 * Mach Operating System
 * Copyright (c) 1992,1991,1990 Carnegie Mellon University
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS
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
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

#include <assert.h>

#include "write.h"
#include "error.h"
#include "utils.h"
#include "global.h"
#include "mig_string.h"
#include "cpu.h"

/*************************************************************
 *	Writes the standard includes. The subsystem specific
 *	includes  are in <SubsystemName>.h and writen by
 *	header:WriteHeader. Called by WriteProlog.
 *************************************************************/
static void
WriteIncludes(FILE *file)
{
    fprintf(file, "#ifndef _GNU_SOURCE\n");
    fprintf(file, "#define _GNU_SOURCE 1\n");
    fprintf(file, "#endif\n\n");

    if (IsKernelServer)
    {
	/*
	 *	We want to get the user-side definitions of types
	 *	like task_t, ipc_space_t, etc. in mach/mach_types.h.
	 */

	fprintf(file, "#undef\tKERNEL\n");

	if (InternalHeaderFileName != strNULL)
	{
	    char *cp;

	    /* Strip any leading path from InternalHeaderFileName. */
	    cp = strrchr(InternalHeaderFileName, '/');
	    if (cp == 0)
		cp = InternalHeaderFileName;
	    else
		cp++;	/* skip '/' */
	    fprintf(file, "#include \"%s\"\n", cp);
	}
    }

    if (UserHeaderFileName != strNULL)
    {
	char *cp;

	/* Strip any leading path from UserHeaderFileName. */
	cp = strrchr(UserHeaderFileName, '/');
	if (cp == 0)
	    cp = UserHeaderFileName;
	else
	    cp++;	/* skip '/' */
	fprintf(file, "#include \"%s\"\n", cp);
    }

    fprintf(file, "#define EXPORT_BOOLEAN\n");
    fprintf(file, "#include <mach/boolean.h>\n");
    fprintf(file, "#include <mach/kern_return.h>\n");
    fprintf(file, "#include <mach/message.h>\n");
    fprintf(file, "#include <mach/notify.h>\n");
    fprintf(file, "#include <mach/mach_types.h>\n");
    fprintf(file, "#include <mach/mig_errors.h>\n");
    fprintf(file, "#include <mach/mig_support.h>\n");
    if (IsKernelUser)
	fprintf(file, "#include <kern/ipc_mig.h>\n");
    fprintf(file, "#include <stdint.h>\n");
    fprintf(file, "\n");
}

static void
WriteGlobalDecls(FILE *file)
{
    if (RCSId != strNULL)
	WriteRCSDecl(file, strconcat(SubsystemName, "_user"), RCSId);

    fprintf(file, "#define msgh_request_port\tmsgh_remote_port\n");
    fprintf(file, "#define msgh_reply_port\t\tmsgh_local_port\n");
    fprintf(file, "\n");
}

/*************************************************************
 *	Writes the standard #includes, #defines, and
 *	RCS declaration. Called by WriteUser.
 *************************************************************/
static void
WriteProlog(FILE *file)
{
    WriteIncludes(file);
    WriteBogusDefines(file);
    WriteGlobalDecls(file);
}

/*ARGSUSED*/
static void
WriteEpilog(FILE *file)
{
}

static const_string_t
WriteHeaderPortType(const argument_t *arg)
{
    if (arg->argType->itInName == MACH_MSG_TYPE_POLYMORPHIC)
	return arg->argPoly->argVarName;
    else
	return arg->argType->itInNameStr;
}

static void
WriteRequestHead(FILE *file, const routine_t *rt)
{
    if (rt->rtMaxRequestPos > 0)
	fprintf(file, "\tInP = &Mess.In;\n");

    if (rt->rtSimpleFixedRequest) {
	fprintf(file, "\tInP->Head.msgh_bits =");
	if (!rt->rtSimpleSendRequest)
	    fprintf(file, " MACH_MSGH_BITS_COMPLEX|");
	fprintf(file, "\n");
	fprintf(file, "\t\tMACH_MSGH_BITS(%s, %s);\n",
		WriteHeaderPortType(rt->rtRequestPort),
		WriteHeaderPortType(rt->rtUReplyPort));
    } else {
	fprintf(file, "\tInP->Head.msgh_bits = msgh_simple ?\n");
	fprintf(file, "\t\tMACH_MSGH_BITS(%s, %s) :\n",
		WriteHeaderPortType(rt->rtRequestPort),
		WriteHeaderPortType(rt->rtUReplyPort));
	fprintf(file, "\t\t(MACH_MSGH_BITS_COMPLEX|\n");
	fprintf(file, "\t\t MACH_MSGH_BITS(%s, %s));\n",
		WriteHeaderPortType(rt->rtRequestPort),
		WriteHeaderPortType(rt->rtUReplyPort));
    }

    fprintf(file, "\t/* msgh_size filled below */\n");

    /*
     *	KernelUser stubs need to cast the request and reply ports
     *	from ipc_port_t to mach_port_t.
     */

    if (IsKernelUser)
	fprintf(file, "\tInP->%s = (mach_port_t) %s;\n",
		rt->rtRequestPort->argMsgField,
		rt->rtRequestPort->argVarName);
    else
	fprintf(file, "\tInP->%s = %s;\n",
		rt->rtRequestPort->argMsgField,
		rt->rtRequestPort->argVarName);

    if (akCheck(rt->rtUReplyPort->argKind, akbUserArg)) {
	if (IsKernelUser)
	    fprintf(file, "\tInP->%s = (mach_port_t) %s;\n",
		    rt->rtUReplyPort->argMsgField,
		    rt->rtUReplyPort->argVarName);
	else
	    fprintf(file, "\tInP->%s = %s;\n",
		    rt->rtUReplyPort->argMsgField,
		    rt->rtUReplyPort->argVarName);
    } else if (rt->rtOneWay || IsKernelUser)
	fprintf(file, "\tInP->%s = MACH_PORT_NULL;\n",
		rt->rtUReplyPort->argMsgField);
    else
	fprintf(file, "\tInP->%s = %smig_get_reply_port();\n",
		rt->rtUReplyPort->argMsgField, SubrPrefix);

    fprintf(file, "\tInP->Head.msgh_seqno = 0;\n");
    fprintf(file, "\tInP->Head.msgh_id = %d;\n", rt->rtNumber + SubsystemBase);
}

/*************************************************************
 *  Writes declarations for the message types, variables
 *  and return  variable if needed. Called by WriteRoutine.
 *************************************************************/
static void
WriteVarDecls(FILE *file, const routine_t *rt)
{
    fprintf(file, "\tunion {\n");
    fprintf(file, "\t\tRequest In;\n");
    if (!rt->rtOneWay)
	fprintf(file, "\t\tReply Out;\n");
    fprintf(file, "\t} Mess;\n");
    fprintf(file, "\n");

    fprintf(file, "\tRequest *InP = &Mess.In;\n");
    if (!rt->rtOneWay)
	fprintf(file, "\tReply *OutP = &Mess.Out;\n");
    fprintf(file, "\n");

    if (!rt->rtOneWay)
	fprintf(file, "\tmach_msg_return_t msg_result;\n");

    if (!rt->rtSimpleFixedRequest)
	fprintf(file, "\tboolean_t msgh_simple = %s;\n",
		strbool(rt->rtSimpleSendRequest));
    else if (!rt->rtOneWay &&
	     !(rt->rtSimpleCheckReply && rt->rtSimpleReceiveReply)) {
	fprintf(file, "#if\tTypeCheck\n");
	fprintf(file, "\tboolean_t msgh_simple;\n");
	fprintf(file, "#endif\t/* TypeCheck */\n");
    }

    if (rt->rtNumRequestVar > 0)
	fprintf(file, "\tunsigned int msgh_size;\n");
    else if (!rt->rtOneWay && !rt->rtNoReplyArgs)
    {
	fprintf(file, "#if\tTypeCheck\n");
	fprintf(file, "\tunsigned int msgh_size;\n");
	fprintf(file, "#endif\t/* TypeCheck */\n");
    }

    /* if either request or reply is variable, we need msgh_size_delta */
    if ((rt->rtMaxRequestPos > 0) ||
	(rt->rtMaxReplyPos > 0))
	fprintf(file, "\tunsigned int msgh_size_delta;\n");

    fprintf(file, "\n");
}

/*************************************************************
 *  Writes code to call the user provided error procedure
 *  when a MIG error occurs. Called by WriteMsgSend,
 *  WriteMsgCheckReceive, WriteCheckIdentity,
 *  WriteRetCodeCheck, WriteTypeCheck, WritePackArgValue.
 *************************************************************/
static void
WriteMsgError(FILE *file, const routine_t *rt, const char *error_msg)
{
    if (rt->rtReturn != rt->rtRetCode)
    {
	fprintf(file, "\t\t{ (%s); ", error_msg);
	if (rt->rtNumReplyVar > 0)
	    fprintf(file, "OutP = &Mess.Out; ");
	fprintf(file, "return OutP->%s; }\n", rt->rtReturn->argMsgField);
    }
    else
	fprintf(file, "\t\treturn %s;\n", error_msg);
}

/*************************************************************
 *   Writes the send call when there is to be no subsequent
 *   receive. Called by WriteRoutine for SimpleRoutines.
 *************************************************************/
static void
WriteMsgSend(FILE *file, const routine_t *rt)
{
    const char *MsgResult = "return";
    char SendSize[24];

    if (rt->rtNumRequestVar == 0)
    {
        sprintf(SendSize, "%d", rt->rtRequestSize);
        fprintf(file, "\t_Static_assert(sizeof(Request) == %s, \"Request expected to be %s bytes\");\n", SendSize, SendSize);
    }
    else
	strcpy(SendSize, "msgh_size");

    fprintf(file, "\tInP->Head.msgh_size = %s;\n\n", SendSize);

    if (IsKernelUser)
    {
	fprintf(file, "\t%s %smach_msg_send_from_kernel(",
		MsgResult, SubrPrefix);
	fprintf(file, "&InP->Head, %s);\n", SendSize);
    }
    else
    {
	fprintf(file, "\t%s %smach_msg(&InP->Head, MACH_SEND_MSG|%s, %s, 0,",
		MsgResult,
		SubrPrefix,
		rt->rtMsgOption->argVarName,
		SendSize);
	fprintf(file,
		" MACH_PORT_NULL, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);\n"
		);
    }
}

/*************************************************************
 *  Writes to code to check for error returns from receive.
 *  Called by WriteMsgSendReceive and WriteMsgRPC
 *************************************************************/
static void
WriteMsgCheckReceive(FILE *file, const routine_t *rt, const char *success)
{
    fprintf(file, "\tif (msg_result != %s) {\n", success);
    if (!akCheck(rt->rtUReplyPort->argKind, akbUserArg) && !IsKernelUser)
    {
        /* If we aren't using a user-supplied reply port,
	   then deallocate the reply port on any message transmission
	   errors. */
	fprintf(file, "\t\t%smig_dealloc_reply_port(%s);\n",
		SubrPrefix, "InP->Head.msgh_reply_port");
    }
    WriteMsgError(file, rt, "msg_result");
    fprintf(file, "\t}\n");

    /*
     * If not using a user supplied reply port, tell the port
     * allocator we're done with the port.
     */
    if (!akCheck(rt->rtUReplyPort->argKind, akbUserArg) && !IsKernelUser)
    {
	fprintf(file, "\t%smig_put_reply_port(InP->Head.msgh_reply_port);\n",
		SubrPrefix);
    }
}

/*************************************************************
 *  Writes the rpc call and the code to check for errors.
 *  This is the default code to be generated. Called by WriteRoutine
 *  for all routine types except SimpleRoutine.
 *************************************************************/
static void
WriteMsgRPC(FILE *file, const routine_t *rt)
{
    char SendSize[24];

    if (rt->rtNumRequestVar == 0)
    {
        sprintf(SendSize, "%d", rt->rtRequestSize);
        fprintf(file, "\t_Static_assert(sizeof(Request) == %s, \"Request expected to be %s bytes\");\n", SendSize, SendSize);
    } else
	strcpy(SendSize, "msgh_size");

    fprintf(file, "\tInP->Head.msgh_size = %s;\n\n", SendSize);

    if (IsKernelUser)
	fprintf(file, "\tmsg_result = %smach_msg_rpc_from_kernel(&InP->Head, %s, sizeof(Reply));\n",
		SubrPrefix,
		SendSize);
    else
	fprintf(file, "\tmsg_result = %smach_msg(&InP->Head, MACH_SEND_MSG|MACH_RCV_MSG|%s%s, %s, sizeof(Reply), InP->Head.msgh_reply_port, %s, MACH_PORT_NULL);\n",
	    SubrPrefix,
	    rt->rtMsgOption->argVarName,
	    rt->rtWaitTime != argNULL ? "|MACH_RCV_TIMEOUT" : "",
	    SendSize,
	    rt->rtWaitTime != argNULL? rt->rtWaitTime->argVarName : "MACH_MSG_TIMEOUT_NONE");
    WriteMsgCheckReceive(file, rt, "MACH_MSG_SUCCESS");
    fprintf(file, "\n");
}

/*************************************************************
 *   Sets the correct value of the dealloc flag and calls
 *   Utils:WritePackMsgType to fill in the ipc msg type word(s)
 *   in the request message. Called by WriteRoutine for each
 *   argument that is to be sent in the request message.
 *************************************************************/
static void
WritePackArgType(FILE *file, const argument_t *arg)
{
    WritePackMsgType(file, arg->argType,
		     arg->argType->itIndefinite ? d_NO : arg->argDeallocate,
		     arg->argLongForm, true,
		     "InP->%s", "%s", arg->argTTName);
    fprintf(file, "\n");
}

/*************************************************************
 *  Writes code to copy an argument into the request message.
 *  Called by WriteRoutine for each argument that is to placed
 *  in the request message.
 *************************************************************/
static void
WritePackArgValue(FILE *file, const argument_t *arg)
{
    const ipc_type_t *it = arg->argType;
    const char *ref = arg->argByReferenceUser ? "*" : "";

    if (it->itInLine && it->itVarArray) {

	if (it->itString) {
	    /*
	     *	Copy variable-size C string with mig_strncpy.
	     *	Save the string length (+ 1 for trailing 0)
	     *	in the argument`s count field.
	     */
	    fprintf(file,
		"\tInP->%s = %smig_strncpy(InP->%s, %s, %d);\n",
		arg->argCount->argMsgField,
		SubrPrefix,
		arg->argMsgField,
		arg->argVarName,
		it->itNumber);
	    fprintf(file,
		"\tif (InP->%s < %d) InP->%s += 1;\n",
		arg->argCount->argMsgField,
		it->itNumber,
	        arg->argCount->argMsgField);
	}
	else {

	    /*
	     *	Copy in variable-size inline array with memcpy,
	     *	after checking that number of elements doesn`t
	     *	exceed declared maximum.
	     */
	    const argument_t *count = arg->argCount;
	    const char *countRef = count->argByReferenceUser ? "*" :"";
	    const ipc_type_t *btype = it->itElement;
	    const bool is_64bit_port = IS_64BIT_ABI && btype->itUserlandPort;

	    /* Note btype->itNumber == count->argMultiplier */

	    fprintf(file, "\tif (%s%s > %d) {\n",
		countRef, count->argVarName,
		it->itNumber/btype->itNumber);
	    if (it->itIndefinite) {
		fprintf(file, "\t\tInP->%s%s.msgt_inline = FALSE;\n",
			arg->argTTName,
			arg->argLongForm ? ".msgtl_header" : "");
		if (is_64bit_port) {
			/* Update size of the type to be the same as a port name since
			 * we are passing port names out of line. */
			fprintf(file, "\t\t/* We are passing mach_port_name_t out of line. */\n");
			fprintf(file, "\t\tInP->%s%s.msgt_size = %d;\n",
				arg->argTTName,
				arg->argLongForm ? ".msgtl_header" : "",
				port_name_size_in_bits);
		}
		if (arg->argDeallocate == d_YES)
		    fprintf(file, "\t\tInP->%s%s.msgt_deallocate = TRUE;\n",
			    arg->argTTName,
			    arg->argLongForm ? ".msgtl_header" : "");
		else if (arg->argDeallocate == d_MAYBE)
		    fprintf(file, "\t\tInP->%s%s.msgt_deallocate = %s%s;\n",
			    arg->argTTName,
			    arg->argLongForm ? ".msgtl_header" : "",
			    arg->argDealloc->argByReferenceUser ? "*" : "",
			    arg->argDealloc->argVarName);
		fprintf(file, "\t\tInP->%s%s = %s%s;\n",
			arg->argMsgField,
			OOLPostfix,
			ref, arg->argVarName);
		if (!arg->argRoutine->rtSimpleFixedRequest)
		    fprintf(file, "\t\tmsgh_simple = FALSE;\n");
	    }
	    else
		WriteMsgError(file, arg->argRoutine, "MIG_ARRAY_TOO_LARGE");

	    fprintf(file, "\t}\n\telse if (%s%s) {\n", countRef, count->argVarName);

	    if (is_64bit_port) {
		    /* When copying inlined ports, the 64bit ABI uses 8 bytes to store port names,
		     * hence we cannot use memcpy directly.
		     */
		    fprintf(file, "\t\t/* Transform mach_port_name_t into mach_port_name_inlined_t. */\n");
		    fprintf(file, "\t\tmach_port_name_inlined_t *inlined_%s = (mach_port_name_inlined_t *)InP->%s;\n",
				    arg->argMsgField, arg->argMsgField);
		    fprintf(file, "\t\tmach_msg_type_number_t i;\n");
		    fprintf(file, "\t\tfor (i = 0; i < %s%s; i++) {\n", countRef, count->argVarName);
		    fprintf(file, "\t\t\t/* Clear the whole message with zeros. */\n");
		    fprintf(file, "\t\t\tinlined_%s[i].kernel_port_do_not_use = 0;\n",
				    arg->argMsgField);
		    fprintf(file, "\t\t\tinlined_%s[i].name = (%s%s)[i];\n", arg->argMsgField, ref, arg->argMsgField);
		    fprintf(file, "\t\t}\n");
	    } else {
		    fprintf(file, "\t\tmemcpy(InP->%s, %s%s, ", arg->argMsgField, ref, arg->argVarName);
		    if (btype->itTypeSize > 1)
			    fprintf(file, "%d * ", btype->itTypeSize);
		    fprintf(file, "%s%s);\n", countRef, count->argVarName);
	    }
	    fprintf(file, "\t}\n");
	}
    }
    else if (arg->argMultiplier > 1) {
	WriteCopyType(file, it, "InP->%s", "/* %s */ %d * %s%s",
		      arg->argMsgField, arg->argMultiplier,
		      ref, arg->argVarName);
    } else {
	bool is_inlined_port = it->itUserlandPort && it->itInLine;
	WriteCopyType(file, it, "InP->%s%s", "/* %s%s */ %s%s",
		      arg->argMsgField, is_inlined_port ? ".name" : "", ref, arg->argVarName);
    }
    fprintf(file, "\n");
}

static void
WriteAdjustMsgSimple(FILE *file, const argument_t *arg)
{
    if (!arg->argRoutine->rtSimpleFixedRequest)
    {
	const char *ref = arg->argByReferenceUser ? "*" : "";

	fprintf(file, "\tif (MACH_MSG_TYPE_PORT_ANY(%s%s))\n",
		ref, arg->argVarName);
	fprintf(file, "\t\tmsgh_simple = FALSE;\n");
	fprintf(file, "\n");
    }
}

/*
 * Calculate the size of a variable-length message field.
 */
static void
WriteArgSize(FILE *file, const argument_t *arg)
{
    const ipc_type_t *ptype = arg->argType;
    int bsize = ptype->itElement->itTypeSize;
    const argument_t *count = arg->argCount;

    if (ptype->itIndefinite) {
	/*
	 *	Check descriptor.  If out-of-line, use standard size.
	 */
	fprintf(file, "(InP->%s%s.msgt_inline) ? ",
		arg->argTTName, arg->argLongForm ? ".msgtl_header" : "");
    }
    if (bsize % complex_alignof != 0)
	fprintf(file, "(");

    if (bsize > 1)
	fprintf(file, "%d * ", bsize);

    if (ptype->itString)
	/* get count from descriptor in message */
	fprintf(file, "InP->%s", count->argMsgField);
    else
	/* get count from argument */
	fprintf(file, "%s%s",
		count->argByReferenceUser ? "*" : "",
		count->argVarName);

    /*
     * If the base type size is not a multiple of complex_alignof,
     * we have to round up.
     */
    if (bsize % complex_alignof != 0)
	fprintf(file, " + %zd) & ~%zdU", complex_alignof - 1, complex_alignof - 1);

    if (ptype->itIndefinite) {
	fprintf(file, " : sizeof(%s *)",
		FetchUserType(ptype->itElement));
    }
}

/*
 * Adjust message size and advance request pointer.
 * Called after packing a variable-length argument that
 * has more arguments following.
 */
static void
WriteAdjustMsgSize(FILE *file, const argument_t *arg)
{
    const ipc_type_t *ptype = arg->argType;

    /* There are more In arguments.  We need to adjust msgh_size
       and advance InP, so we save the size of the current field
       in msgh_size_delta. */

    fprintf(file, "\tmsgh_size_delta = ");
    WriteArgSize(file, arg);
    fprintf(file, ";\n");

    if (arg->argRequestPos == 0)
	/* First variable-length argument.  The previous msgh_size value
	   is the minimum request size. */

	fprintf(file, "\tmsgh_size = %d + msgh_size_delta;\n",
		arg->argRoutine->rtRequestSize);
    else
	fprintf(file, "\tmsgh_size += msgh_size_delta;\n");

    fprintf(file,
	"\tInP = (Request *) ((char *) InP + msgh_size_delta - %d);\n",
	ptype->itTypeSize + ptype->itPadSize);
}

/*
 * Calculate the size of the message.  Called after the
 * last argument has been packed.
 */
static void
WriteFinishMsgSize(FILE *file, const argument_t *arg)
{
    /* No more In arguments.  If this is the only variable In
       argument, the previous msgh_size value is the minimum
       request size. */

    if (arg->argRequestPos == 0) {
	fprintf(file, "\tmsgh_size = %d + (",
			arg->argRoutine->rtRequestSize);
	WriteArgSize(file, arg);
	fprintf(file, ");\n");
    }
    else {
        fprintf(file, "\tmsgh_size += ");
	WriteArgSize(file, arg);
        fprintf(file, ";\n");
    }
}

static void
WriteInitializeCount(FILE *file, const argument_t *arg)
{
    const ipc_type_t *ptype = arg->argCInOut->argParent->argType;
    const ipc_type_t *btype = ptype->itElement;

    fprintf(file, "\tif (%s%s < %d)\n",
	    arg->argByReferenceUser ? "*" : "",
	    arg->argVarName,
	    ptype->itNumber/btype->itNumber);
    fprintf(file, "\t\tInP->%s = %s%s;\n",
	    arg->argMsgField,
	    arg->argByReferenceUser ? "*" : "",
	    arg->argVarName);
    fprintf(file, "\telse\n");
    fprintf(file, "\t\tInP->%s = %d;\n",
	    arg->argMsgField, ptype->itNumber/btype->itNumber);
    fprintf(file, "\n");
}

/*
 * Called for every argument.  Responsible for packing that
 * argument into the request message.
 */
static void
WritePackArg(FILE *file, const argument_t *arg)
{
    if (akCheck(arg->argKind, akbRequest))
	WritePackArgType(file, arg);

    if ((akIdent(arg->argKind) == akePoly) &&
	akCheckAll(arg->argKind, akbSendSnd|akbUserArg))
	WriteAdjustMsgSimple(file, arg);

    if ((akIdent(arg->argKind) == akeCountInOut) &&
	akCheck(arg->argKind, akbSendSnd))
	WriteInitializeCount(file, arg);
    else if (akCheckAll(arg->argKind, akbSendSnd|akbSendBody))
	WritePackArgValue(file, arg);
}

/*
 * Generate code to fill in all of the request arguments and their
 * message types.
 */
static void
WriteRequestArgs(FILE *file, const routine_t *rt)
{
    const argument_t *arg;
    const argument_t *lastVarArg;

    lastVarArg = argNULL;
    for (arg = rt->rtArgs; arg != argNULL; arg = arg->argNext)
    {
	/*
	 * Adjust message size and advance message pointer if
	 * the last request argument was variable-length and the
	 * request position will change.
	 */
	if (lastVarArg != argNULL &&
	    lastVarArg->argRequestPos < arg->argRequestPos)
	{
	    WriteAdjustMsgSize(file, lastVarArg);
	    lastVarArg = argNULL;
	}

	/*
	 * Copy the argument
	 */
	WritePackArg(file, arg);

	/*
	 * Remember whether this was variable-length.
	 */
	if (akCheckAll(arg->argKind, akbSendSnd|akbSendBody|akbVariable))
	    lastVarArg = arg;
    }

    /*
     * Finish the message size.
     */
    if (lastVarArg != argNULL)
	WriteFinishMsgSize(file, lastVarArg);
}

/*************************************************************
 *  Writes code to check that the return msgh_id is correct and that
 *  the size of the return message is correct. Called by
 *  WriteRoutine.
 *************************************************************/
static void
WriteCheckIdentity(FILE *file, const routine_t *rt)
{
    fprintf(file, "\tif (mig_unlikely (OutP->Head.msgh_id != %d)) {\n",
	    rt->rtNumber + SubsystemBase + 100);
    fprintf(file, "\t\tif (OutP->Head.msgh_id == MACH_NOTIFY_SEND_ONCE)\n\t");
    WriteMsgError(file, rt, "MIG_SERVER_DIED");
    fprintf(file, "\t\telse {\n");
    fprintf(file, "\t\t\t%smig_dealloc_reply_port(%s);\n\t",
          SubrPrefix,"InP->Head.msgh_reply_port");
    WriteMsgError(file, rt, "MIG_REPLY_MISMATCH");
    fprintf(file, "\t\t}\n\t}\n");
    fprintf(file, "\n");
    fprintf(file, "#if\tTypeCheck\n");

    if (rt->rtSimpleCheckReply && rt->rtSimpleReceiveReply)
    {
	/* Expecting a simple message.  We can factor out the check for
	   a simple message, since the error reply message is also simple.
	   */

	if (!rt->rtNoReplyArgs)
	    fprintf(file, "\tmsgh_size = OutP->Head.msgh_size;\n\n");

	fprintf(file,
	    "\tif (mig_unlikely ("
	    "(OutP->Head.msgh_bits & MACH_MSGH_BITS_COMPLEX) ||\n");
	if (rt->rtNoReplyArgs)
	    fprintf(file, "\t    (OutP->Head.msgh_size != %d)))\n",
			rt->rtReplySize);
	else {
	    fprintf(file, "\t    ((msgh_size %s %d) &&\n",
		(rt->rtNumReplyVar > 0) ? "<" : "!=",
		rt->rtReplySize);
	    fprintf(file, "\t     ((msgh_size != sizeof(mig_reply_header_t)) ||\n");
	    fprintf(file, "\t      (OutP->RetCode == KERN_SUCCESS)))))\n");
	}
    }
    else {
	/* Expecting a complex message, or may vary at run time. */

	fprintf(file, "\tmsgh_size = OutP->Head.msgh_size;\n");
	fprintf(file, "\tmsgh_simple = !(OutP->Head.msgh_bits & MACH_MSGH_BITS_COMPLEX);\n");
	fprintf(file, "\n");

	fprintf(file, "\tif (mig_unlikely (((msgh_size %s %d)",
		(rt->rtNumReplyVar > 0) ? "<" : "!=",
		rt->rtReplySize);

	if (rt->rtSimpleCheckReply)
	    /* if rtSimpleReceiveReply was true, then we would have
	       executed the code above.  So we know that the message
	       is complex. */
	    fprintf(file, " || msgh_simple");
	fprintf(file, ") &&\n");

	fprintf(file, "\t    ((msgh_size != sizeof(mig_reply_header_t)) ||\n");
	fprintf(file, "\t     !msgh_simple ||\n");
	fprintf(file, "\t     (OutP->RetCode == KERN_SUCCESS))))\n");
    }
    WriteMsgError(file, rt, "MIG_TYPE_ERROR");
    fprintf(file, "#endif\t/* TypeCheck */\n");
    fprintf(file, "\n");
}

/*************************************************************
 *  Write code to generate error handling code if the RetCode
 *  argument of a Routine is not KERN_SUCCESS.
 *************************************************************/
static void
WriteRetCodeCheck(FILE *file, const routine_t *rt)
{
    fprintf(file, "\tif (OutP->RetCode != KERN_SUCCESS)\n");
    WriteMsgError(file, rt, "OutP->RetCode");
    fprintf(file, "\n");
}

/*************************************************************
 *  Writes code to check that the type of each of the arguments
 *  in the reply message is what is expected. Called by
 *  WriteRoutine for each argument in the reply message.
 *************************************************************/
static void
WriteTypeCheck(FILE *file, const argument_t *arg)
{
    const ipc_type_t *it = arg->argType;
    const routine_t *rt = arg->argRoutine;

    fprintf(file, "#if\tTypeCheck\n");
    if (akCheck(arg->argKind, akbReplyQC))
    {
	fprintf(file, "\tif (BAD_TYPECHECK (&OutP->%s, &%sCheck))\n",
		arg->argTTName, arg->argVarName);
    }
    else
    {
	fprintf(file, "\tif (mig_unlikely (");
	if (!it->itIndefinite) {
	    fprintf(file, "(OutP->%s%s.msgt_inline != %s) ||\n\t    ",
		arg->argTTName,
		arg->argLongForm ? ".msgtl_header" : "",
		strbool(it->itInLine));
	}
	fprintf(file, "(OutP->%s%s.msgt_longform != %s) ||\n",
		arg->argTTName,
		arg->argLongForm ? ".msgtl_header" : "",
		strbool(arg->argLongForm));
	if (it->itOutName == MACH_MSG_TYPE_POLYMORPHIC)
	{
	    if (!rt->rtSimpleCheckReply)
		fprintf(file, "\t    (MACH_MSG_TYPE_PORT_ANY(OutP->%s.msgt%s_name) && msgh_simple) ||\n",
			arg->argTTName,
			arg->argLongForm ? "l" : "");
	}
	else
	    fprintf(file, "\t    (OutP->%s.msgt%s_name != %s) ||\n",
		    arg->argTTName,
		    arg->argLongForm ? "l" : "",
		    it->itOutNameStr);
	if (!it->itVarArray)
	    fprintf(file, "\t    (OutP->%s.msgt%s_number != %d) ||\n",
		    arg->argTTName,
		    arg->argLongForm ? "l" : "",
		    it->itNumber);
	if (IS_64BIT_ABI && it->itUserlandPort && arg->argLongForm) {
	    /* 64 bit inlined ports are 8 bytes long but we use mach_port_name_t when passing them out of line. */
	    fprintf(file, "\t    (OutP->%s.msgtl_size != %d && OutP->%s.msgtl_header.msgt_inline == TRUE) || \n",
		    arg->argTTName,
		    it->itSize,
		    arg->argTTName);
	    fprintf(file, "\t    (OutP->%s.msgtl_size != %d && OutP->%s.msgtl_header.msgt_inline == FALSE)",
		    arg->argTTName,
		    port_name_size_in_bits,
		    arg->argTTName);
	} else {
	    fprintf(file, "\t    (OutP->%s.msgt%s_size != %d)",
		    arg->argTTName,
		    arg->argLongForm ? "l" : "",
		    it->itSize);
	}
	fprintf(file, "))\n");
    }
    WriteMsgError(file, rt, "MIG_TYPE_ERROR");
    fprintf(file, "#endif\t/* TypeCheck */\n");
    fprintf(file, "\n");
}

static void
WriteCheckArgSize(FILE *file, const argument_t *arg)
{
    const ipc_type_t *ptype = arg->argType;
    const ipc_type_t *btype = ptype->itElement;
    const argument_t *count = arg->argCount;
    int multiplier = btype->itTypeSize / btype->itNumber;

    if (ptype->itIndefinite) {
	/*
	 * Check descriptor.  If out-of-line, use standard size.
	 */
	fprintf(file, "(OutP->%s%s.msgt_inline) ? ",
		arg->argTTName, arg->argLongForm ? ".msgtl_header" : "");
    }

    if (btype->itTypeSize % complex_alignof != 0)
	fprintf(file, "(");

    if (multiplier > 1)
	fprintf(file, "%d * ", multiplier);

    fprintf(file, "OutP->%s", count->argMsgField);

    /* If the base type size of the data field isn`t a multiple of complex_alignof,
       we have to round up. */
    if (btype->itTypeSize % complex_alignof != 0)
	fprintf(file, " + %zd) & ~%zdU", complex_alignof - 1, complex_alignof - 1);

    if (ptype->itIndefinite)
	fprintf(file, " : sizeof(%s *)", FetchUserType(btype));
}

static void
WriteCheckMsgSize(FILE *file, const argument_t *arg)
{
    const routine_t *rt = arg->argRoutine;

    /* If there aren't any more Out args after this, then
       we can use the msgh_size_delta value directly in
       the TypeCheck conditional. */

    if (arg->argReplyPos == rt->rtMaxReplyPos)
    {
	fprintf(file, "#if\tTypeCheck\n");
	fprintf(file, "\tif (mig_unlikely (msgh_size != %d + (",
		rt->rtReplySize);
	WriteCheckArgSize(file, arg);
	fprintf(file, ")))\n");

	WriteMsgError(file, rt, "MIG_TYPE_ERROR");
	fprintf(file, "#endif\t/* TypeCheck */\n");
    }
    else
    {
	/* If there aren't any more variable-sized arguments after this,
	   then we must check for exact msg-size and we don't need
	   to update msgh_size. */

	bool LastVarArg = arg->argReplyPos+1 == rt->rtNumReplyVar;

	/* calculate the actual size in bytes of the data field.  note
	   that this quantity must be a multiple of complex_alignof.  hence, if
	   the base type size isn't a multiple of complex_alignof, we have to
	   round up.  note also that btype->itNumber must
	   divide btype->itTypeSize (see itCalculateSizeInfo). */

	fprintf(file, "\tmsgh_size_delta = ");
	WriteCheckArgSize(file, arg);
	fprintf(file, ";\n");
	fprintf(file, "#if\tTypeCheck\n");

	/* Don't decrement msgh_size until we've checked that
	   it won't underflow. */

	if (LastVarArg)
	    fprintf(file,
		"\tif (mig_unlikely (msgh_size != %d + msgh_size_delta))\n",
		rt->rtReplySize);
	else
	    fprintf(file,
		"\tif (mig_unlikely (msgh_size < %d + msgh_size_delta))\n",
		rt->rtReplySize);
	WriteMsgError(file, rt, "MIG_TYPE_ERROR");

	if (!LastVarArg)
	    fprintf(file, "\tmsgh_size -= msgh_size_delta;\n");

	fprintf(file, "#endif\t/* TypeCheck */\n");
    }
    fprintf(file, "\n");
}

static void
WriteExtractArgValueThroughCopy(FILE *file, const argument_t *arg, const argument_t *count,
	const ipc_type_t *btype, const char *ref, const bool is_64bit_port)
{
    if (is_64bit_port) {
	/* When copying inlined ports, the 64bit ABI uses 8 bytes to store port names,
	 * hence we cannot use memcpy.
	 */
	fprintf(file, "\t\t/* Transform mach_port_name_inlined_t into mach_port_name_t. */\n");
	fprintf(file, "\t\tmach_port_name_inlined_t *inlined_%s = (mach_port_name_inlined_t *)OutP->%s;\n",
		arg->argMsgField, arg->argMsgField);
	fprintf(file, "\t\tmach_msg_type_number_t i;\n");
	fprintf(file, "\t\tfor (i = 0; i < OutP->%s; i++) {\n", count->argMsgField);
	fprintf(file, "\t\t\t(%s%s)[i] = inlined_%s[i].name;\n",
		ref, arg->argVarName, arg->argMsgField);
	fprintf(file, "\t\t}\n");
    } else {
	fprintf(file, "\t\tmemcpy(%s%s, OutP->%s, ", ref, arg->argVarName,
		arg->argMsgField);
	if (btype->itTypeSize != btype->itNumber)
	    fprintf(file, "%d * ", btype->itTypeSize/btype->itNumber);
	fprintf(file, "OutP->%s);\n", count->argMsgField);
    }
}

/*************************************************************
 *  Write code to copy an argument from the reply message
 *  to the parameter. Called by WriteRoutine for each argument
 *  in the reply message.
 *************************************************************/
static void
WriteExtractArgValue(FILE *file, const argument_t *arg)
{
    const ipc_type_t	*argType = arg->argType;
    const char *ref = arg->argByReferenceUser ? "*" : "";

    if (argType->itInLine && argType->itVarArray) {

	if (argType->itString) {
	    /*
	     *	Copy out variable-size C string with mig_strncpy.
	     */
	    fprintf(file, "\t(void) %smig_strncpy(%s%s, OutP->%s, %d);\n",
		SubrPrefix,
		ref,
		arg->argVarName,
		arg->argMsgField,
		argType->itNumber);
	}
	else if (argType->itIndefinite) {
	    /*
	     * If data was returned out-of-line,
	     *    change user`s pointer to point to it.
	     * If data was returned in-line but doesn`t fit,
	     *    allocate a new buffer, copy the data to it,
	     *	  and change user`s pointer to point to it.
	     * If data was returned in-line and fits,
	     *    copy to buffer.
	     */
	    const argument_t *count = arg->argCount;
	    const char *countRef = count->argByReferenceUser ? "*" : "";
	    const ipc_type_t *btype = argType->itElement;
	    const bool is_64bit_port = IS_64BIT_ABI && btype->itUserlandPort;

	    fprintf(file, "\tif (!OutP->%s%s.msgt_inline)\n",
		    arg->argTTName,
		    arg->argLongForm ? ".msgtl_header" : "");
	    fprintf(file, "\t\t%s%s = OutP->%s%s;\n",
		    ref, arg->argVarName,
		    arg->argMsgField,
		    OOLPostfix);
	    fprintf(file, "\telse if (OutP->%s", count->argMsgField);
	    if (btype->itNumber > 1)
		fprintf(file, " / %d", btype->itNumber);
	    fprintf(file, " > %s%s) {\n", countRef, count->argVarName);
	    fprintf(file, "\t\t%smig_allocate((vm_offset_t *)%s, ",
		    SubrPrefix, arg->argVarName);	/* no ref! */
	    if (is_64bit_port)
		fprintf(file, "%d * ", port_name_size);
	    else if (btype->itTypeSize != btype->itNumber)
		fprintf(file, "%d * ", btype->itTypeSize/btype->itNumber);
	    fprintf(file, "OutP->%s);\n", count->argMsgField);
	    WriteExtractArgValueThroughCopy(file, arg, count, btype, ref, is_64bit_port);
	    fprintf(file, "\t}\n");
	    fprintf(file, "\telse if (OutP->%s) {\n", count->argMsgField);

	    WriteExtractArgValueThroughCopy(file, arg, count, btype, ref, is_64bit_port);
	    fprintf(file, "\t}\n");
	}
	else {

	    /*
	     *	Copy out variable-size inline array with memcpy,
	     *	after checking that number of elements doesn`t
	     *	exceed user`s maximum.
	     */
	    const argument_t *count = arg->argCount;
	    const char *countRef = count->argByReferenceUser ? "*" :"";
	    const ipc_type_t *btype = argType->itElement;

	    /* Note count->argMultiplier == btype->itNumber */

	    fprintf(file, "\tif (OutP->%s", count->argMsgField);
	    if (btype->itNumber > 1)
		fprintf(file, " / %d", btype->itNumber);
	    fprintf(file, " > %s%s) {\n",
		countRef, count->argVarName);

	    /*
	     * If number of elements is too many for user receiving area,
	     * fill user`s area as much as possible.  Return the correct
	     * number of elements.
	     */
	    fprintf(file, "\t\tif (%s%s)\n", countRef, count->argVarName);
	    fprintf(file, "\t\t\tmemcpy(%s%s, OutP->%s, ", ref, arg->argVarName,
		    arg->argMsgField);
	    if (btype->itTypeSize > 1)
		fprintf(file, "%d * ", btype->itTypeSize);
	    fprintf(file, "%s%s);\n",
		countRef, count->argVarName);

	    fprintf(file, "\t\t%s%s = OutP->%s",
		     countRef, count->argVarName, count->argMsgField);
	    if (btype->itNumber > 1)
		fprintf(file, " / %d", btype->itNumber);
	    fprintf(file, ";\n");
	    WriteMsgError(file,arg->argRoutine, "MIG_ARRAY_TOO_LARGE");

	    fprintf(file, "\t}\n\telse if (OutP->%s) {\n", count->argMsgField);

	    fprintf(file, "\t\tmemcpy(%s%s, OutP->%s, ", ref, arg->argVarName,
		    arg->argMsgField);
	    if (btype->itTypeSize != btype->itNumber)
		fprintf(file, "%d * ", btype->itTypeSize/btype->itNumber);
	    fprintf(file, "OutP->%s);\n", count->argMsgField);
	    fprintf(file, "\t}\n");
	}
    }
    else if (arg->argMultiplier > 1) {
	WriteCopyType(file, argType,
		      "%s%s", "/* %s%s */ OutP->%s / %d",
		      ref, arg->argVarName, arg->argMsgField,
		      arg->argMultiplier);
    } else {
	bool is_inlined_port = argType->itUserlandPort && argType->itInLine;
	WriteCopyType(file, argType,
		      "%s%s", "/* %s%s */ OutP->%s%s",
		      ref, arg->argVarName, arg->argMsgField, is_inlined_port ? ".name" : "");
    }
    fprintf(file, "\n");
}

static void
WriteExtractArg(FILE *file, const argument_t *arg)
{
    const routine_t *rt = arg->argRoutine;

    if (akCheck(arg->argKind, akbReply))
	WriteTypeCheck(file, arg);

    if (akCheckAll(arg->argKind, akbVariable|akbReply))
	WriteCheckMsgSize(file, arg);

    /* Now that the RetCode is type-checked, check its value.
       Must abort immediately if it isn't KERN_SUCCESS, because
       in that case the reply message is truncated. */

    if (arg == rt->rtRetCode)
	WriteRetCodeCheck(file, rt);

    if (akCheckAll(arg->argKind, akbReturnRcv))
	WriteExtractArgValue(file, arg);
}

static void
WriteAdjustReplyMsgPtr(FILE *file, const argument_t *arg)
{
    const ipc_type_t *ptype = arg->argType;

    fprintf(file,
	"\tOutP = (Reply *) ((char *) OutP + msgh_size_delta - %d);\n\n",
	ptype->itTypeSize + ptype->itPadSize);
}

static void
WriteReplyArgs(FILE *file, const routine_t *rt)
{
    const argument_t *arg;
    const argument_t *lastVarArg;

    lastVarArg = argNULL;
    for (arg = rt->rtArgs; arg != argNULL; arg = arg->argNext) {

	/*
	 * Advance message pointer if the last reply argument was
	 * variable-length and the reply position will change.
	 */
	if (lastVarArg != argNULL &&
	    lastVarArg->argReplyPos < arg->argReplyPos)
	{
	    WriteAdjustReplyMsgPtr(file, lastVarArg);
	    lastVarArg = argNULL;
	}

	/*
	 * Copy the argument
	 */
	WriteExtractArg(file, arg);

	/*
	 * Remember whether this was variable-length.
	 */
	if (akCheckAll(arg->argKind, akbReturnRcv|akbVariable))
	    lastVarArg = arg;
    }
}

/*************************************************************
 *  Writes code to return the return value. Called by WriteRoutine
 *  for routines and functions.
 *************************************************************/
static void
WriteReturnValue(FILE *file, const routine_t *rt)
{
    if (rt->rtReturn == rt->rtRetCode)
	/* If returning RetCode, we have already checked that it is
	   KERN_SUCCESS */
	fprintf(file, "\treturn KERN_SUCCESS;\n");

    else
    {
	if (rt->rtNumReplyVar > 0)
	    fprintf(file, "\tOutP = &Mess.Out;\n");

	fprintf(file, "\treturn OutP->%s;\n", rt->rtReturn->argMsgField);
    }
}

/*************************************************************
 *  Writes the elements of the message type declaration: the
 *  msg_type structure, the argument itself and any padding
 *  that is required to make the argument a multiple of complex_alignof bytes.
 *  Called by WriteRoutine for all the arguments in the request
 *  message first and then the reply message.
 *************************************************************/
static void
WriteFieldDecl(FILE *file, const argument_t *arg)
{
    WriteFieldDeclPrim(file, arg, FetchUserType);
}

static void
WriteStubDecl(FILE *file, const routine_t *rt)
{
    fprintf(file, "\n");
    fprintf(file, "/* %s %s */\n", rtRoutineKindToStr(rt->rtKind), rt->rtName);
    fprintf(file, "mig_external %s %s\n", ReturnTypeStr(rt), rt->rtUserName);
    fprintf(file, "(\n");
    WriteList(file, rt->rtArgs, WriteUserVarDecl, akbUserArg, ",\n", "\n");
    fprintf(file, ")\n");
    fprintf(file, "{\n");
}

/*************************************************************
 *  Writes all the code comprising a routine body. Called by
 *  WriteUser for each routine.
 *************************************************************/
static void
WriteRoutine(FILE *file, const routine_t *rt)
{
    /* write the stub's declaration */

    WriteStubDecl(file, rt);

    /* typedef of structure for Request and Reply messages */

    WriteStructDecl(file, rt->rtArgs, WriteFieldDecl, akbRequest, "Request");
    if (!rt->rtOneWay)
	WriteStructDecl(file, rt->rtArgs, WriteFieldDecl, akbReply, "Reply");

    /* declarations for local vars: Union of Request and Reply messages,
       InP, OutP and return value */

    WriteVarDecls(file, rt);

    /* declarations and initializations of the mach_msg_type_t variables
       for each argument */

    WriteList(file, rt->rtArgs, WriteTypeDeclInUser, akbRequest, "\n", "\n");
    if (!rt->rtOneWay)
	WriteList(file, rt->rtArgs, WriteCheckDecl, akbReplyQC, "\n", "\n");

    /* fill in all the request message types and then arguments */

    WriteRequestArgs(file, rt);

    /* fill in request message head */

    WriteRequestHead(file, rt);
    fprintf(file, "\n");

    /* Write the send/receive or rpc call */

    if (rt->rtOneWay)
	WriteMsgSend(file, rt);
    else
    {
	WriteMsgRPC(file, rt);

	/* Check the values that are returned in the reply message */

	WriteCheckIdentity(file, rt);

	/* If the reply message has no Out parameters or return values
	   other than the return code, we can type-check it and
	   return it directly. */

	if (rt->rtNoReplyArgs)
	{
	    WriteTypeCheck(file, rt->rtRetCode);

	    fprintf(file, "\treturn OutP->RetCode;\n");
	}
	else {
	    WriteReplyArgs(file, rt);

	    /* return the return value */
            WriteReturnValue(file, rt);
	}
    }

    fprintf(file, "}\n");
}

/*************************************************************
 *  Writes out the xxxUser.c file. Called by mig.c
 *************************************************************/
void
WriteUser(FILE *file, const statement_t *stats)
{
    const statement_t *stat;

    WriteProlog(file);
    for (stat = stats; stat != stNULL; stat = stat->stNext)
	switch (stat->stKind)
	{
	  case skRoutine:
	    WriteRoutine(file, stat->stRoutine);
	    break;
	  case skImport:
	  case skUImport:
	    WriteImport(file, stat->stFileName);
	    break;
	  case skSImport:
	    break;
	  default:
	    fatal("WriteUser(): bad statement_kind_t (%d)",
		  (int) stat->stKind);
	}
    WriteEpilog(file);
}

/*************************************************************
 *  Writes out individual .c user files for each routine.  Called by mig.c
 *************************************************************/
void
WriteUserIndividual(const statement_t *stats)
{
    const statement_t *stat;

    for (stat = stats; stat != stNULL; stat = stat->stNext)
	switch (stat->stKind)
	{
	  case skRoutine:
	    {
		FILE *file;
		char *filename;

		filename = strconcat(UserFilePrefix,
				     strconcat(stat->stRoutine->rtName, ".c"));
		file = fopen(filename, "w");
		if (file == NULL)
		    fatal("fopen(%s): %s", filename,
			  unix_error_string(errno));
		WriteProlog(file);

		{
		  /* Write all the imports.  */
		  const statement_t *s;
		  for (s = stats; s != stNULL; s = s->stNext)
		    switch (s->stKind)
		      {
		      case skImport:
		      case skUImport:
			WriteImport(file, s->stFileName);
			break;
		      default:
			break;
		      }
		}

		WriteRoutine(file, stat->stRoutine);
		WriteEpilog(file);
		if (ferror(file) || fclose(file))
		    fatal("fclose(%s): %s", filename,
			  unix_error_string(errno));
		strfree(filename);
	    }
	    break;
	  case skImport:
	  case skUImport:
	    break;
	  case skSImport:
	    break;
	  default:
	    fatal("WriteUserIndividual(): bad statement_kind_t (%d)",
		  (int) stat->stKind);
	}
}
