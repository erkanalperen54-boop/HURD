/*
 * Copyright (c) 2026 Free Software Foundation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef _MACHINE_LOCK_H_
#define _MACHINE_LOCK_H_

/*
 *	No machine-specific lock primitives on aarch64 yet.
 *
 *	kern/lock.h unconditionally includes this header, but only
 *	references its definitions when NCPUS > 1.  Today the aarch64
 *	port is built with NCPUS = 1, so an empty header is sufficient.
 *	A future SMP-capable aarch64 port should fill this with the
 *	low-level _simple_lock_data_t and _simple_lock / _simple_unlock
 *	primitives, mirroring i386/i386/lock.h.
 */

#endif /* _MACHINE_LOCK_H_ */
