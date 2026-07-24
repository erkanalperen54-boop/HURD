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

%token	sySkip
%token	syRoutine
%token	sySimpleRoutine

%token	sySubsystem
%token	syKernelUser
%token	syKernelServer

%token	syMsgOption
%token	syMsgSeqno
%token	syWaitTime
%token	syNoWaitTime
%token	syServerPrefix
%token	syUserPrefix
%token	syServerDemux
%token	syRCSId

%token	syImport
%token	syUImport
%token	sySImport

%token	syIn
%token	syOut
%token	syInOut
%token	syRequestPort
%token	syReplyPort
%token	sySReplyPort
%token	syUReplyPort

%token	syType
%token	syArray
%token	syStruct
%token	syOf

%token	syInTran
%token	syOutTran
%token	syDestructor
%token	syCType
%token	syCUserType
%token	syCServerType

%token	syCString

%token	syColon
%token	sySemi
%token	syComma
%token	syPlus
%token	syMinus
%token	syStar
%token	syDiv
%token	syLParen
%token	syRParen
%token	syEqual
%token	syCaret
%token	syTilde
%token	syLAngle
%token	syRAngle
%token	syLBrack
%token	syRBrack
%token	syLCBrack
%token	syRCBrack
%token	syBar

%token	syError			/* lex error */

%token	<number>	syNumber
%token	<symtype>	sySymbolicType
%token	<identifier>	syIdentifier
%token	<string>	syString syQString
%token	<string>	syFileName
%token	<flag>		syIPCFlag

%token	syInTranPayload

%left	syPlus syMinus
%left	syStar syDiv


%type	<statement_kind> ImportIndicant
%type	<number> VarArrayHead ArrayHead StructHead IntExp
%type   <structured_type> StructList
%type	<type> NamedTypeSpec TransTypeSpec TypeSpec
%type	<type> CStringSpec
%type	<type> BasicTypeSpec PrevTypeSpec ArgumentType
%type	<symtype> PrimIPCType IPCType
%type	<routine> RoutineDecl Routine SimpleRoutine
%type	<direction> Direction
%type	<argument> Argument Arguments ArgumentList
%type	<flag> IPCFlags

%{

#include <stdio.h>

#include "cpu.h"
#include "error.h"
#include "lexxer.h"
#include "global.h"
#include "mig_string.h"
#include "type.h"
#include "routine.h"
#include "statement.h"
#include "utils.h"

static const char *import_name(statement_kind_t sk);

void
yyerror(const char *s)
{
    error("%s", s);
}
%}

%union
{
    u_int number;
    identifier_t identifier;
    const_string_t string;
    statement_kind_t statement_kind;
    ipc_type_t *type;
    struct
    {
	u_int innumber;		/* msgt_name value, when sending */
	const_string_t instr;
	u_int outnumber;	/* msgt_name value, when receiving */
	const_string_t outstr;
	u_int size;		/* 0 means there is no default size */
    } symtype;
    /* Holds information about a structure while parsing. */
    struct
    {
        /* The required alignment (in bytes) so far. */
        u_int type_alignment_in_bytes;
        /* The size of the struct in bytes so far. */
        u_int size_in_bytes;
    } structured_type;
    routine_t *routine;
    arg_kind_t direction;
    argument_t *argument;
    ipc_flags_t flag;
}

%%

Statements		:	/* empty */
			|	Statements Statement
			;

Statement		:	Subsystem sySemi
			|	WaitTime sySemi
			|	MsgOption sySemi
			|	ServerPrefix sySemi
			|	UserPrefix sySemi
			|	ServerDemux sySemi
			|	TypeDecl sySemi
			|	RoutineDecl sySemi
{
    statement_t *st = stAlloc();

    st->stKind = skRoutine;
    st->stRoutine = $1;
    rtCheckRoutine($1);
    if (BeVerbose)
	rtPrintRoutine($1);
}
			|	sySkip sySemi
				{ rtSkip(1); }
			|	sySkip syNumber sySemi
				{ rtSkip($2); }
			|	Import sySemi
			|	RCSDecl sySemi
			|	sySemi
			|	error sySemi
				{ yyerrok; }
			;

Subsystem		:	SubsystemStart SubsystemMods
				SubsystemName SubsystemBase
{
    if (BeVerbose)
    {
	printf("Subsystem %s: base = %u%s%s\n\n",
	       SubsystemName, SubsystemBase,
	       IsKernelUser ? ", KernelUser" : "",
	       IsKernelServer ? ", KernelServer" : "");
    }
    init_type();
}
			;

SubsystemStart		:	sySubsystem
{
    if (SubsystemName != strNULL)
    {
	warn("previous Subsystem decl (of %s) will be ignored", SubsystemName);
	IsKernelUser = false;
	IsKernelServer = false;
	strfree((string_t) SubsystemName);
    }
}
			;

SubsystemMods		:	/* empty */
			|	SubsystemMods SubsystemMod
			;

SubsystemMod		:	syKernelUser
{
    if (IsKernelUser)
	warn("duplicate KernelUser keyword");
    IsKernelUser = true;
}
			|	syKernelServer
{
    if (IsKernelServer)
	warn("duplicate KernelServer keyword");
    IsKernelServer = true;
}
			;

SubsystemName		:	syIdentifier	{ SubsystemName = $1; }
			;

SubsystemBase		:	syNumber	{ SubsystemBase = $1; }
			;

MsgOption		:	LookString syMsgOption syString
{
    if (streql($3, "MACH_MSG_OPTION_NONE"))
    {
	MsgOption = strNULL;
	if (BeVerbose)
	    printf("MsgOption: canceled\n\n");
    }
    else
    {
	MsgOption = $3;
	if (BeVerbose)
	    printf("MsgOption %s\n\n",$3);
    }
}
			;

WaitTime		:	LookString syWaitTime syString
{
    WaitTime = $3;
    if (BeVerbose)
	printf("WaitTime %s\n\n", WaitTime);
}
			|	syNoWaitTime
{
    WaitTime = strNULL;
    if (BeVerbose)
	printf("NoWaitTime\n\n");
}
			;

ServerPrefix		:	syServerPrefix syIdentifier
{
    ServerPrefix = $2;
    if (BeVerbose)
	printf("ServerPrefix %s\n\n", ServerPrefix);
}
			;

UserPrefix		:	syUserPrefix syIdentifier
{
    UserPrefix = $2;
    if (BeVerbose)
	printf("UserPrefix %s\n\n", UserPrefix);
}
			;

ServerDemux		:	syServerDemux syIdentifier
{
    ServerDemux = $2;
    if (BeVerbose)
	printf("ServerDemux %s\n\n", ServerDemux);
}
			;

Import			:	LookFileName ImportIndicant syFileName
{
    statement_t *st = stAlloc();
    st->stKind = $2;
    st->stFileName = $3;

    if (BeVerbose)
	printf("%s %s\n\n", import_name($2), $3);
}
			;

ImportIndicant		:	syImport	{ $$ = skImport; }
			|	syUImport	{ $$ = skUImport; }
			|	sySImport	{ $$ = skSImport; }
			;

RCSDecl			:	LookQString syRCSId syQString
{
    if (RCSId != strNULL)
	warn("previous RCS decl will be ignored");
    if (BeVerbose)
	printf("RCSId %s\n\n", $3);
    RCSId = $3;
}
			;

TypeDecl		:	syType NamedTypeSpec
{
    identifier_t name = $2->itName;

    if (itLookUp(name) != itNULL)
		error("overriding previous definition of %s", name);
    itInsert(name, $2);
}
			;

NamedTypeSpec		:	syIdentifier syEqual TransTypeSpec
				{ itTypeDecl($1, $$ = $3); }
			;

TransTypeSpec		:	TypeSpec
				{ $$ = itResetType($1); }
			|	TransTypeSpec syInTran syColon syIdentifier
				syIdentifier syLParen syIdentifier syRParen 
{
    $$ = $1;

    if (($$->itTransType != strNULL) && !streql($$->itTransType, $4))
	warn("conflicting translation types (%s, %s)",
	     $$->itTransType, $4);
    $$->itTransType = $4;

    if (($$->itInTrans != strNULL) && !streql($$->itInTrans, $5))
	warn("conflicting in-translation functions (%s, %s)",
	     $$->itInTrans, $5);
    $$->itInTrans = $5;

    if (($$->itServerType != strNULL) && !streql($$->itServerType, $7))
	warn("conflicting server types (%s, %s)",
	     $$->itServerType, $7);
    $$->itServerType = $7;
}
			|	TransTypeSpec syInTranPayload syColon
				syIdentifier syIdentifier
{
    $$ = $1;

    if (($$->itTransType != strNULL) && !streql($$->itTransType, $4))
	warn("conflicting translation types (%s, %s)",
	     $$->itTransType, $4);
    $$->itTransType = $4;

    if (($$->itInTransPayload != strNULL) && !streql($$->itInTransPayload, $5))
	warn("conflicting in-translation functions (%s, %s)",
	     $$->itInTransPayload, $5);
    $$->itInTransPayload = $5;
}
			|	TransTypeSpec syOutTran syColon syIdentifier
				syIdentifier syLParen syIdentifier syRParen
{
    $$ = $1;

    if (($$->itServerType != strNULL) && !streql($$->itServerType, $4))
	warn("conflicting server types (%s, %s)",
	     $$->itServerType, $4);
    $$->itServerType = $4;

    if (($$->itOutTrans != strNULL) && !streql($$->itOutTrans, $5))
	warn("conflicting out-translation functions (%s, %s)",
	     $$->itOutTrans, $5);
    $$->itOutTrans = $5;

    if (($$->itTransType != strNULL) && !streql($$->itTransType, $7))
	warn("conflicting translation types (%s, %s)",
	     $$->itTransType, $7);
    $$->itTransType = $7;
}
			|	TransTypeSpec syDestructor syColon syIdentifier
				syLParen syIdentifier syRParen
{
    $$ = $1;

    if (($$->itDestructor != strNULL) && !streql($$->itDestructor, $4))
	warn("conflicting destructor functions (%s, %s)",
	     $$->itDestructor, $4);
    $$->itDestructor = $4;

    if (($$->itTransType != strNULL) && !streql($$->itTransType, $6))
	warn("conflicting translation types (%s, %s)",
	     $$->itTransType, $6);
    $$->itTransType = $6;
}
			|	TransTypeSpec syCType syColon syIdentifier
{
    $$ = $1;

    if (($$->itUserType != strNULL) && !streql($$->itUserType, $4))
	warn("conflicting user types (%s, %s)",
	     $$->itUserType, $4);
    $$->itUserType = $4;

    if (($$->itServerType != strNULL) && !streql($$->itServerType, $4))
	warn("conflicting server types (%s, %s)",
	     $$->itServerType, $4);
    $$->itServerType = $4;
}
			|	TransTypeSpec syCUserType syColon syIdentifier
{
    $$ = $1;

    if (($$->itUserType != strNULL) && !streql($$->itUserType, $4))
	warn("conflicting user types (%s, %s)",
	     $$->itUserType, $4);
    $$->itUserType = $4;
}
			|	TransTypeSpec syCServerType
				syColon syIdentifier
{
    $$ = $1;

    if (($$->itServerType != strNULL) && !streql($$->itServerType, $4))
	warn("conflicting server types (%s, %s)",
	     $$->itServerType, $4);
    $$->itServerType = $4;
}
			;

TypeSpec		:	BasicTypeSpec
				{ $$ = $1; }
			|	PrevTypeSpec
				{ $$ = $1; }
			|	VarArrayHead TypeSpec
				{ $$ = itVarArrayDecl($1, $2); }
			|	ArrayHead TypeSpec
				{ $$ = itArrayDecl($1, $2); }
			|	syCaret TypeSpec
				{ $$ = itPtrDecl($2); }
			|	StructHead TypeSpec
				{ $$ = itStructArrayDecl($1, $2); }
                        |       syStruct syLCBrack StructList syRCBrack
				{ $$ = itStructDecl($3.size_in_bytes, $3.type_alignment_in_bytes); }
			|	CStringSpec
				{ $$ = $1; }
			;

StructList   		: 	syIdentifier syIdentifier sySemi
{
    ipc_type_t *t = itPrevDecl($1);
    if (!t) {
        error("Type %s not found\n", $1);
    }
    if (!t->itInLine) {
        error("Type %s must be inline\n", $2);
    }

    $$.type_alignment_in_bytes = t->itAlignment;
    $$.size_in_bytes = t->itTypeSize;
}
			|	StructList syIdentifier syIdentifier sySemi
{
    ipc_type_t *t = itPrevDecl($2);
    if (!t) {
        error("Type %s not found\n", $2);
    }
    if (!t->itInLine) {
        error("Type %s must be inline\n", $2);
    }
    $$.type_alignment_in_bytes = MAX(t->itAlignment, $1.type_alignment_in_bytes);
    int padding_bytes = 0;
    if ($1.size_in_bytes % t->itAlignment)
        padding_bytes = t->itAlignment - ($1.size_in_bytes % t->itAlignment);
    $$.size_in_bytes = $1.size_in_bytes + padding_bytes + t->itTypeSize;
}
                        ;

BasicTypeSpec		:	IPCType
{
    $$ = itShortDecl($1.innumber, $1.instr,
		     $1.outnumber, $1.outstr,
		     $1.size);
}
			|	syLParen IPCType syComma IntExp
				IPCFlags syRParen
{
    $$ = itLongDecl($2.innumber, $2.instr,
		    $2.outnumber, $2.outstr,
		    $2.size, $4, $5);
}
			;

IPCFlags		:	/* empty */
				{ $$ = flNone; }
			|	IPCFlags syComma syIPCFlag
{
    if ($1 & $3)
	warn("redundant IPC flag ignored");
    else
	$$ = $1 | $3;
}
			|	IPCFlags syComma syIPCFlag syLBrack syRBrack
{
    if ($3 != flDealloc)
	warn("only Dealloc is variable");
    else
	$$ = $1 | flMaybeDealloc;
}
			;

PrimIPCType		:	syNumber
{
    $$.innumber = $$.outnumber = $1;
    $$.instr = $$.outstr = strNULL;
    $$.size = 0;
}
			|	sySymbolicType
				{ $$ = $1; }
			;

IPCType			:	PrimIPCType
				{ $$ = $1; }
			|	PrimIPCType syBar PrimIPCType
{
    if ($1.size != $3.size)
    {
	if ($1.size == 0)
	    $$.size = $3.size;
	else if ($3.size == 0)
	    $$.size = $1.size;
	else
	{
	    error("sizes in IPCTypes (%s %s %d, %s %s %d) aren't equal",
		  $1.instr, $1.outstr, $1.size, $3.instr, $3.outstr, $3.size);
	    $$.size = 0;
	}
    }
    else
	$$.size = $1.size;
    $$.innumber = $1.innumber;
    $$.instr = $1.instr;
    $$.outnumber = $3.outnumber;
    $$.outstr = $3.outstr;
}
			;

PrevTypeSpec		:	syIdentifier
				{ $$ = itPrevDecl($1); }
			;

VarArrayHead		:	syArray syLBrack syRBrack syOf
				{ $$ = 0; }
			|	syArray syLBrack syStar syRBrack syOf
				{ $$ = 0; }
			|	syArray syLBrack syStar syColon IntExp 
				syRBrack syOf
				{ $$ = $5; }
			;

ArrayHead		:	syArray syLBrack IntExp syRBrack syOf
				{ $$ = $3; }
			;

StructHead		:	syStruct syLBrack IntExp syRBrack syOf
				{ $$ = $3; }
			;

CStringSpec		:	syCString syLBrack IntExp syRBrack
				{ $$ = itCStringDecl($3, false); }
			|	syCString syLBrack syStar syColon
				IntExp syRBrack
				{ $$ = itCStringDecl($5, true); }
			;

IntExp			: 	IntExp	syPlus	IntExp
				{ $$ = $1 + $3;	}
			| 	IntExp	syMinus	IntExp
				{ $$ = $1 - $3;	}
			|	IntExp	syStar	IntExp
				{ $$ = $1 * $3;	}
			| 	IntExp	syDiv	IntExp
				{ $$ = $1 / $3;	}
			|	syNumber
				{ $$ = $1;	}
			|	syLParen IntExp syRParen
				{ $$ = $2;	}
			;

 
RoutineDecl		:	Routine			{ $$ = $1; }
			|	SimpleRoutine		{ $$ = $1; }
			;

Routine			:	syRoutine syIdentifier Arguments
				{ $$ = rtMakeRoutine($2, $3); }
			;

SimpleRoutine		:	sySimpleRoutine syIdentifier Arguments
				{ $$ = rtMakeSimpleRoutine($2, $3); }
			;

Arguments		:	syLParen syRParen
				{ $$ = argNULL; }
			|	syLParen ArgumentList syRParen
				{ $$ = $2; }

			;

ArgumentList		:	Argument
				{ $$ = $1; }
			|	Argument sySemi ArgumentList
{
    $$ = $1;
    $$->argNext = $3;
}
			;

Argument		:	Direction syIdentifier syColon ArgumentType IPCFlags
{
    $$ = argAlloc();
    $$->argKind = $1;
    $$->argName = $2;
    $$->argType = $4;
    $$->argFlags = $5;
}
			;

Direction		:	/* empty */	{ $$ = akNone; }
			|	syIn		{ $$ = akIn; }
			|	syOut		{ $$ = akOut; }
			|	syInOut		{ $$ = akInOut; }
			|	syRequestPort	{ $$ = akRequestPort; }
			|	syReplyPort	{ $$ = akReplyPort; }
			|	sySReplyPort	{ $$ = akSReplyPort; }
			|	syUReplyPort	{ $$ = akUReplyPort; }
			|	syWaitTime	{ $$ = akWaitTime; }
			|	syMsgOption	{ $$ = akMsgOption; }
			|	syMsgSeqno	{ $$ = akMsgSeqno; }
			;

ArgumentType		:	syIdentifier
{
    $$ = itLookUp($1);
    if ($$ == itNULL)
	error("type '%s' not defined", $1);
}
			|	NamedTypeSpec
				{ $$ = $1; }
			;

LookString		:	/* empty */
				{ LookString(); }
			;

LookFileName		:	/* empty */
				{ LookFileName(); }
			;

LookQString		:	/* empty */
				{ LookQString(); }
			;

%%

static const char *
import_name(statement_kind_t sk)
{
    switch (sk)
    {
      case skImport:
	return "Import";
      case skSImport:
	return "SImport";
      case skUImport:
	return "UImport";
      default:
	fatal("import_name(%d): not import statement", (int) sk);
	/*NOTREACHED*/
    }
}
