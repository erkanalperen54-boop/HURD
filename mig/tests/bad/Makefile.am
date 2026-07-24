# Copyright (C) 2016 Free Software Foundation, Inc.
#
# This file is part of GNU MIG.
#
# GNU MIG is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License as
# published by the Free Software Foundation; either version 2, or (at
# your option) any later version.
#
# GNU MIG is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with GNU MIG.  If not, see <http://www.gnu.org/licenses/>.
#

TESTS = invalid_subsystem.defs no_request_port.defs no_subsystem.defs
EXTRA_DIST = $(TESTS) run_bad_test.sh
DEFS_LOG_COMPILER = sh $(srcdir)/run_bad_test.sh

include $(srcdir)/../Makeconf.am
