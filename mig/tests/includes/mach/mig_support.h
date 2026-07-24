/* This file allows the testsuite to compile under Linux
 * which doesn't have mach/mig_support.h */

#ifndef	_MACH_MIG_SUPPORT_H

#define	_MACH_MIG_SUPPORT_H 1

#include <mach/std_types.h>
#include <string.h>

mach_port_t mig_get_reply_port (void);
void mig_dealloc_reply_port (mach_port_t);
void mig_put_reply_port (mach_port_t);
void mig_allocate (vm_address_t *__addr_p, vm_size_t __size);
void mig_deallocate (vm_address_t __addr, vm_size_t __size);

#endif
