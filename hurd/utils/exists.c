/* Check that a file exists
   Copyright (C) 2025 Free Software Foundation, Inc.

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   The GNU Hurd is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <error.h>
#include <hurd.h>

/* This version passes O_NOTRANS to file_name_lookup, contrary to /bin/test
   which passes only AT_NO_AUTOMOUNT which we don't currently support.  */

/* TODO: rather implement AT_NO_AUTOMOUNT, see glibc's bc8879f4f5f3
   ("hurd: Stop mapping AT_NO_AUTOMOUNT to O_NOTRANS") */

int
main (int argc, char **argv)
{
  file_t f;

  if (argc < 2)
    error (1, 0, "Usage: exists /path/to/file");

  f = file_name_lookup (argv[1], O_NOTRANS, 0);

  if (f == MACH_PORT_NULL)
    return EXIT_FAILURE;

  mach_port_deallocate (mach_task_self (), f);
  return EXIT_SUCCESS;
}
