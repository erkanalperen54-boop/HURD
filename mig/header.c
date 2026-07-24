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

#include "write.h"
#include "utils.h"
#include "global.h"
#include "error.h"

static void
WriteIncludes(FILE *file)
{
    fprintf(file, "#include <mach/kern_return.h>\n");
    fprintf(file, "#include <mach/port.h>\n");
    fprintf(file, "#include <mach/message.h>\n");
    fprintf(file, "\n");
}

static void
WriteDefines(FILE *file)
{
    (void) file;	/* Avoid the warning.  */
}

static void
WriteMigExternal(FILE *file)
{
    fprintf(file, "#ifdef\tmig_external\n");
    fprintf(file, "mig_external\n");
    fprintf(file, "#else\n");
    fprintf(file, "extern\n");
    fprintf(file, "#endif\n");
}

static void
WriteProlog(FILE *file, const char *protect)
{
    if (protect != strNULL) {
	fprintf(file, "#ifndef\t_%s\n", protect);
	fprintf(file, "#define\t_%s\n", protect);
	fprintf(file, "\n");
    }

    fprintf(file, "/* Module %s */\n", SubsystemName);
    fprintf(file, "\n");

    WriteIncludes(file);
    WriteDefines(file);
}

static void
WriteEpilog(FILE *file, const char *protect)
{
    if (protect != strNULL) {
	fprintf(file, "\n");
	fprintf(file, "#endif\t/* not defined(_%s) */\n", protect);
    }
}

static void
WriteUserRoutine(FILE *file, const routine_t *rt)
{
    char *defName = strupper(rt->rtUserName);
    fprintf(file, "\n");
    fprintf(file, "/* %s %s */\n", rtRoutineKindToStr(rt->rtKind), rt->rtName);
    fprintf(file, "#define MIG_RPC_HAVE_%s\n", defName);
    fprintf(file, "#define MIG_RPC_REQUEST_ID_%s %d\n",
            defName, rt->rtNumber + SubsystemBase);
    fprintf(file, "#define MIG_RPC_REPLY_ID_%s %d\n",
            defName, rt->rtNumber + SubsystemBase + 100);
    WriteMigExternal(file);
    fprintf(file, "%s %s\n", ReturnTypeStr(rt), rt->rtUserName);
    fprintf(file, "(\n");
    WriteList(file, rt->rtArgs, WriteUserVarDecl, akbUserArg, ",\n", "\n");
    fprintf(file, ");\n");
    strfree(defName);
}

void
WriteUserHeader(FILE *file, const statement_t *stats)
{
    const statement_t *stat;
    const char *protect = strconcat(SubsystemName, "_user_");

    WriteProlog(file, protect);
    for (stat = stats; stat != stNULL; stat = stat->stNext)
	switch (stat->stKind)
	{
	  case skRoutine:
	    WriteUserRoutine(file, stat->stRoutine);
	    break;
	  case skImport:
	  case skUImport:
	    WriteImport(file, stat->stFileName);
	    break;
	  case skSImport:
	    break;
	  default:
	    fatal("WriteHeader(): bad statement_kind_t (%d)",
		  (int) stat->stKind);
	}
    WriteEpilog(file, protect);
}

void
WriteRoutineList(FILE *file, const statement_t *stats)
{
  const statement_t *stat;
  for (stat = stats; stat != stNULL; stat = stat->stNext)
    if (stat->stKind == skRoutine)
      {
	const routine_t *rt = stat->stRoutine;

	fprintf (file, "%s %u %s %u %u %u\n",
		 SubsystemName, SubsystemBase,
		 rt->rtName, rt->rtNumber,
		 SubsystemBase + rt->rtNumber,
		 rt->rtOneWay ? 0 : SubsystemBase + rt->rtNumber + 100);
      }
}

static void
WriteServerRoutine(FILE *file, const routine_t *rt)
{
    char *defName = strupper(rt->rtServerName);
    fprintf(file, "\n");
    fprintf(file, "/* %s %s */\n", rtRoutineKindToStr(rt->rtKind), rt->rtName);
    fprintf(file, "#define MIG_RPC_HAVE_%s\n", defName);
    fprintf(file, "#define MIG_RPC_REQUEST_ID_%s %d\n",
            defName, rt->rtNumber + SubsystemBase);
    fprintf(file, "#define MIG_RPC_REPLY_ID_%s %d\n",
            defName, rt->rtNumber + SubsystemBase + 100);
    WriteMigExternal(file);
    fprintf(file, "%s %s\n", ReturnTypeStr(rt), rt->rtServerName);
    fprintf(file, "(\n");
    WriteList(file, rt->rtArgs, WriteServerVarDecl,
	      akbServerArg, ",\n", "\n");
    fprintf(file, ");\n");
    strfree(defName);
}

void
WriteServerHeader(FILE *file, const statement_t *stats)
{
    const statement_t *stat;
    const char *protect = strconcat(SubsystemName, "_server_");

    WriteProlog(file, protect);
    fprintf(file, "#include <mach/mig_errors.h>\n"); /* For mig_routine_t. */

    for (stat = stats; stat != stNULL; stat = stat->stNext)
	switch (stat->stKind)
	{
	  case skRoutine:
	    WriteServerRoutine(file, stat->stRoutine);
	    break;
	  case skImport:
	  case skSImport:
	    WriteImport(file, stat->stFileName);
	    break;
	  case skUImport:
	    break;
	  default:
	    fatal("WriteServerHeader(): bad statement_kind_t (%d)",
		  (int) stat->stKind);
	}
    fprintf(file, "\n");

    /*
     * Include the x_server_routine function so it can be inlined.
     */
    fprintf(file, "extern mig_routine_t %s_routines[];\n", ServerDemux);
    WriteSubsystemServerRoutine(file, "static __inline");

    WriteEpilog(file, protect);
}

static void
WriteInternalRedefine(FILE *file, const routine_t *rt)
{
    fprintf(file, "#define %s %s_external\n", rt->rtUserName, rt->rtUserName);
}

void
WriteInternalHeader(FILE *file, const statement_t *stats)
{
    const statement_t *stat;

    for (stat = stats; stat != stNULL; stat = stat->stNext)
	switch (stat->stKind)
	{
	  case skRoutine:
	    WriteInternalRedefine(file, stat->stRoutine);
	    break;
	  case skImport:
	  case skUImport:
	  case skSImport:
	    break;
	  default:
	    fatal("WriteInternalHeader(): bad statement_kind_t (%d)",
		  (int) stat->stKind);
	}
}
