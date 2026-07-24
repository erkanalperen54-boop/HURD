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

#ifndef	_GLOBAL_H
#define	_GLOBAL_H

#include <stdbool.h>
#include <sys/types.h>

#include "mig_string.h"

extern bool DefaultFiles;	/* default output file names if no arguments */
extern bool BeQuiet;	/* no warning messages */
extern bool BeVerbose;	/* summarize types, routines */
extern bool GenSymTab;

extern bool IsKernelUser;
extern bool IsKernelServer;

extern const_string_t RCSId;

extern const_string_t SubsystemName;
extern u_int SubsystemBase;

extern const_string_t MsgOption;
extern const_string_t WaitTime;
extern const_string_t ServerPrefix;
extern const_string_t UserPrefix;
extern const_string_t ServerDemux;
extern const_string_t SubrPrefix;
extern const_string_t RoutinePrefix;
extern const_string_t OOLPostfix;

extern int yylineno;
extern string_t yyinname;

extern void init_global(void);

extern string_t UserFilePrefix;
extern string_t UserHeaderFileName;
extern string_t ServerHeaderFileName;
extern string_t InternalHeaderFileName;
extern string_t UserFileName;
extern string_t ServerFileName;

extern size_t port_size;
extern size_t port_size_in_bits;
extern size_t complex_alignof;

#define IS_64BIT_ABI (desired_complex_alignof == 8)

extern void more_global(void);

#ifndef NULL
#define NULL 0
#endif

#endif	/* _GLOBAL_H */
