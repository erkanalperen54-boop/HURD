#
#   Copyright (C) 2019, 2023 Free Software Foundation, Inc.
#
#   This program is free software; you can redistribute it and/or
#   modify it under the terms of the GNU General Public License as
#   published by the Free Software Foundation; either version 2, or (at
#   your option) any later version.
#
#   This program is distributed in the hope that it will be useful, but
#   WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
#   General Public License for more details.
#
#   You should have received a copy of the GNU General Public License
#   along with this program; if not, write to the Free Software
#   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

RUMPLIBS=rump rumpuser rumpdev rumpdev_miiphy rumpdev_pci rumpvfs rumpdev_pci_if_wm rumpnet rumpnet_net rumpnet_netinet rumpnet_local rumpdev_bpf
# If we have a configured tree, include the configuration so that we
# can conditionally build translators.
ifneq (,$(wildcard ../config.make))
 include ../config.make
endif

ifeq ($(HAVE_LIBRUMP_VFSNOFIFO),yes)
RUMPLIBS += rumpvfs_nofifofs
endif

dir := rumpnet
makemode := server

SRCS = main.c net-rump.c
LCLHDRS =
target = rumpnet
OBJS = $(SRCS:.c=.o)
CFLAGS = -Wno-unused-function -Wno-unused-variable
HURDLIBS = machdev ports trivfs shouldbeinlibc iohelp ihash fshelp irqhelp
LDLIBS += -lpthread -lpciaccess -ldl -lz
rumpnet-LDLIBS += -Wl,--no-as-needed $(RUMPLIBS:%=-l%) -Wl,--as-needed
rumpnet.static-LDLIBS += -Wl,--whole-archive $(RUMPLIBS:%=-l%_pic) -Wl,--no-whole-archive

include ../Makeconf
