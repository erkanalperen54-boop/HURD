%option nounput
%option noyywrap

%k 10000
%n 5000
%a 20000
%e 10000
%p 25000

Ident		([A-Za-z_][A-Za-z_0-9]*)
Number		([0-9]+)
String		([-/._$A-Za-z0-9]+)
QString		(\"[^"\n]*\")
AString		(\<[^>\n]*\>)
FileName	({QString}|{AString})

%{
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

#include <string.h>

#include "mig_string.h"
#include "type.h"
#include "statement.h"
#include "global.h"
#include "parser.h"
#include "lexxer.h"
#include "cpu.h"

/* We do not need the input function.  */
#define YY_NO_INPUT

#ifdef	LDEBUG
#define RETURN(sym)							\
{									\
    printf("yylex: returning '%s' (%d)\n", #sym, (sym));		\
    return (sym);							\
}
#else	/* LDEBUG */
#define RETURN(sym)	return (sym)
#endif	/* LDEBUG */

#define	TPRETURN(intype, outtype, tsize)				\
{									\
    yylval.symtype.innumber = (intype);					\
    yylval.symtype.instr = #intype;					\
    yylval.symtype.outnumber = (outtype);				\
    yylval.symtype.outstr = #outtype;					\
    yylval.symtype.size = (tsize);					\
    RETURN(sySymbolicType);						\
}

#define	TRETURN(type, tsize)						\
{									\
    yylval.symtype.innumber = (type);					\
    yylval.symtype.instr = #type;					\
    yylval.symtype.outnumber = (type);					\
    yylval.symtype.outstr = #type;					\
    yylval.symtype.size = (tsize);					\
    RETURN(sySymbolicType);						\
}

#define	FRETURN(val)							\
{									\
    yylval.flag = (val);						\
    RETURN(syIPCFlag);							\
}

extern YYSTYPE yylval;		/* added by rm */

extern void yyerror(const char *);

int lineno;
char *inname;

#ifdef	YY_START
static int oldYYBegin;
#define SAVE_BEGIN oldYYBegin = YY_START
#define RSTR_BEGIN BEGIN oldYYBegin
#else
static struct yysvf *oldYYBegin;
#define SAVE_BEGIN oldYYBegin = yybgin
#define RSTR_BEGIN yybgin = oldYYBegin;
#endif

static void doSharp(char *body); /* process body of # directives */
%}

%Start	Normal String FileName QString SkipToEOL

%%

<Normal>(?i:routine)		RETURN(syRoutine);
<Normal>(?i:simpleroutine)	RETURN(sySimpleRoutine);
<Normal>(?i:subsystem)		RETURN(sySubsystem);
<Normal>(?i:msgoption)		RETURN(syMsgOption);
<Normal>(?i:msgseqno)		RETURN(syMsgSeqno);
<Normal>(?i:waittime)		RETURN(syWaitTime);
<Normal>(?i:nowaittime)		RETURN(syNoWaitTime);
<Normal>(?i:in)			RETURN(syIn);
<Normal>(?i:out)		RETURN(syOut);
<Normal>(?i:inout)		RETURN(syInOut);
<Normal>(?i:requestport)	RETURN(syRequestPort);
<Normal>(?i:replyport)		RETURN(syReplyPort);
<Normal>(?i:ureplyport)		RETURN(syUReplyPort);
<Normal>(?i:sreplyport)		RETURN(sySReplyPort);
<Normal>(?i:array)		RETURN(syArray);
<Normal>(?i:of)			RETURN(syOf);
<Normal>(?i:serverprefix)       RETURN(syServerPrefix);
<Normal>(?i:userprefix)		RETURN(syUserPrefix);
<Normal>(?i:serverdemux)	RETURN(syServerDemux);
<Normal>(?i:rcsid)		RETURN(syRCSId);
<Normal>(?i:import)		RETURN(syImport);
<Normal>(?i:uimport)		RETURN(syUImport);
<Normal>(?i:simport)		RETURN(sySImport);
<Normal>(?i:type)		RETURN(syType);
<Normal>(?i:kernelserver)	RETURN(syKernelServer);
<Normal>(?i:kerneluser)		RETURN(syKernelUser);
<Normal>(?i:skip)		RETURN(sySkip);
<Normal>(?i:struct)		RETURN(syStruct);
<Normal>(?i:intran)		RETURN(syInTran);
<Normal>(?i:intranpayload)      RETURN(syInTranPayload);
<Normal>(?i:outtran)		RETURN(syOutTran);
<Normal>(?i:destructor)		RETURN(syDestructor);
<Normal>(?i:ctype)		RETURN(syCType);
<Normal>(?i:cusertype)		RETURN(syCUserType);
<Normal>(?i:cservertype)	RETURN(syCServerType);
<Normal>(?i:c_string)		RETURN(syCString);

<Normal>(?i:islong)		FRETURN(flLong);
<Normal>(?i:isnotlong)		FRETURN(flNotLong);
<Normal>(?i:dealloc)		FRETURN(flDealloc);
<Normal>(?i:notdealloc)         FRETURN(flNotDealloc);
<Normal>(?i:servercopy)         FRETURN(flServerCopy);
<Normal>(?i:countinout)		FRETURN(flCountInOut);
<Normal>(?i:retcode)		FRETURN(flNone);

<Normal>(?i:polymorphic)	TRETURN(MACH_MSG_TYPE_POLYMORPHIC,port_size_in_bits);

<Normal>"MACH_MSG_TYPE_UNSTRUCTURED"	TRETURN(MACH_MSG_TYPE_UNSTRUCTURED,0);
<Normal>"MACH_MSG_TYPE_BIT"		TRETURN(MACH_MSG_TYPE_BIT,1);
<Normal>"MACH_MSG_TYPE_BOOLEAN"		TRETURN(MACH_MSG_TYPE_BOOLEAN,32);
<Normal>"MACH_MSG_TYPE_INTEGER_16"	TRETURN(MACH_MSG_TYPE_INTEGER_16,16);
<Normal>"MACH_MSG_TYPE_INTEGER_32"	TRETURN(MACH_MSG_TYPE_INTEGER_32,32);
<Normal>"MACH_MSG_TYPE_INTEGER_64"	TRETURN(MACH_MSG_TYPE_INTEGER_64,64);
<Normal>"MACH_MSG_TYPE_CHAR"		TRETURN(MACH_MSG_TYPE_CHAR,8);
<Normal>"MACH_MSG_TYPE_BYTE"		TRETURN(MACH_MSG_TYPE_BYTE,8);
<Normal>"MACH_MSG_TYPE_INTEGER_8"	TRETURN(MACH_MSG_TYPE_INTEGER_8,8);
<Normal>"MACH_MSG_TYPE_REAL"		TRETURN(MACH_MSG_TYPE_REAL,0);
<Normal>"MACH_MSG_TYPE_STRING"		TRETURN(MACH_MSG_TYPE_STRING,0);
<Normal>"MACH_MSG_TYPE_STRING_C"	TRETURN(MACH_MSG_TYPE_STRING_C,0);

<Normal>"MACH_MSG_TYPE_MOVE_RECEIVE"	TPRETURN(MACH_MSG_TYPE_MOVE_RECEIVE,MACH_MSG_TYPE_PORT_RECEIVE,port_size_in_bits);
<Normal>"MACH_MSG_TYPE_COPY_SEND"	TPRETURN(MACH_MSG_TYPE_COPY_SEND,MACH_MSG_TYPE_PORT_SEND,port_size_in_bits);
<Normal>"MACH_MSG_TYPE_MAKE_SEND"	TPRETURN(MACH_MSG_TYPE_MAKE_SEND,MACH_MSG_TYPE_PORT_SEND,port_size_in_bits);
<Normal>"MACH_MSG_TYPE_MOVE_SEND"	TPRETURN(MACH_MSG_TYPE_MOVE_SEND,MACH_MSG_TYPE_PORT_SEND,port_size_in_bits);
<Normal>"MACH_MSG_TYPE_MAKE_SEND_ONCE"	TPRETURN(MACH_MSG_TYPE_MAKE_SEND_ONCE,MACH_MSG_TYPE_PORT_SEND_ONCE,port_size_in_bits);
<Normal>"MACH_MSG_TYPE_MOVE_SEND_ONCE"	TPRETURN(MACH_MSG_TYPE_MOVE_SEND_ONCE,MACH_MSG_TYPE_PORT_SEND_ONCE,port_size_in_bits);

<Normal>"MACH_MSG_TYPE_PORT_NAME"	TRETURN(MACH_MSG_TYPE_PORT_NAME,port_name_size_in_bits);
<Normal>"MACH_MSG_TYPE_PORT_RECEIVE"	TPRETURN(MACH_MSG_TYPE_POLYMORPHIC,MACH_MSG_TYPE_PORT_RECEIVE,port_size_in_bits);
<Normal>"MACH_MSG_TYPE_PORT_SEND"	TPRETURN(MACH_MSG_TYPE_POLYMORPHIC,MACH_MSG_TYPE_PORT_SEND,port_size_in_bits);
<Normal>"MACH_MSG_TYPE_PORT_SEND_ONCE"	TPRETURN(MACH_MSG_TYPE_POLYMORPHIC,MACH_MSG_TYPE_PORT_SEND_ONCE,port_size_in_bits);
<Normal>"MACH_MSG_TYPE_POLYMORPHIC"	TRETURN(MACH_MSG_TYPE_POLYMORPHIC,0);

<Normal>":"		RETURN(syColon);
<Normal>";"		RETURN(sySemi);
<Normal>","		RETURN(syComma);
<Normal>"+"		RETURN(syPlus);
<Normal>"-"		RETURN(syMinus);
<Normal>"*"		RETURN(syStar);
<Normal>"/"		RETURN(syDiv);
<Normal>"("		RETURN(syLParen);
<Normal>")"		RETURN(syRParen);
<Normal>"="		RETURN(syEqual);
<Normal>"^"		RETURN(syCaret);
<Normal>"~"		RETURN(syTilde);
<Normal>"<"		RETURN(syLAngle);
<Normal>">"		RETURN(syRAngle);
<Normal>"["		RETURN(syLBrack);
<Normal>"]"		RETURN(syRBrack);
<Normal>"{"		RETURN(syLCBrack);
<Normal>"}"		RETURN(syRCBrack);
<Normal>"|"		RETURN(syBar);

<Normal>{Ident}		{ yylval.identifier = strmake(yytext);
			  RETURN(syIdentifier); }
<Normal>{Number}	{ yylval.number = atoi(yytext); RETURN(syNumber); }

<String>{String}	{ yylval.string = strmake(yytext);
			  BEGIN Normal; RETURN(syString); }
<FileName>{FileName}	{ yylval.string = strmake(yytext);
			  BEGIN Normal; RETURN(syFileName); }
<QString>{QString}	{ yylval.string = strmake(yytext);
			  BEGIN Normal; RETURN(syQString); }

^\#[ \t]*{Number}[ \t]*\"[^"]*\"	{ doSharp(yytext+1);
					  SAVE_BEGIN;
					  BEGIN SkipToEOL; }
^\#\ *{Number}				{ doSharp(yytext+1);
					  SAVE_BEGIN;
					  BEGIN SkipToEOL; }
^\#define[ \t] 				{ /* GCC's pre-processor might emit those.  */
					  SAVE_BEGIN;
					  BEGIN SkipToEOL; }
^\#					{ yyerror("illegal # directive");
					  SAVE_BEGIN;
					  BEGIN SkipToEOL; }

<SkipToEOL>\n		RSTR_BEGIN;
<SkipToEOL>.		;

[ \t]			;
\n			{ lineno++; }
.			{ BEGIN Normal; RETURN(syError); }

%%

extern void
LookNormal(void)
{
    if (inname == 0)
    {
	inname = strmake("(unknown)");
	lineno = 0;
    }

    BEGIN Normal;
}

extern void
LookString(void)
{
    BEGIN String;
}

extern void
LookQString(void)
{
    BEGIN QString;
}

extern void
LookFileName(void)
{
    BEGIN FileName;
}

static void
doSharp(char *body)
{
    const char *startName;

    lineno = atoi(body);
    startName = strchr(body, '"');
    if (startName != NULL)
    {
	*strrchr(body, '"') = '\0';
	strfree(inname);
	inname = strmake(startName+1);
    }
}
