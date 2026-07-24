/* pager-bulk.c Default (dummy) implementation of bulk page write.

   Copyright (C) 2025 Free Software Foundation, Inc.
   Written by Milos Nikic.

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   The GNU Hurd is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with the GNU Hurd; if not, see <https://www.gnu.org/licenses/>.  */

#include <libpager/pager.h>
#include "priv.h"

/* Default dummy implementation of pager_write_pages. */
__attribute__((weak)) error_t
pager_write_pages (struct user_pager_info *upi,
		   vm_offset_t offset,
		   vm_address_t data, vm_size_t length, vm_size_t *written)
{
  (void) upi;
  (void) offset;
  (void) data;
  (void) length;
  if (written)
    *written = 0;
  return EOPNOTSUPP;
}
