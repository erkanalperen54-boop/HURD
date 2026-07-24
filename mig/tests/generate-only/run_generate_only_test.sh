#!/bin/sh
#
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

. $SRCDIR/tests/test_lib.sh

file=$1
module="$(basename $file .defs)"
if ! run_mig $file $module; then
  failure "Could not generate stubs for $module"
  exit 1
fi
