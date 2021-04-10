/* NBD client library in userspace
 * Copyright (C) 2013-2021 Red Hat Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/* Check for a requirement or skip the test. */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>

#include "requires.h"

void
requires (const char *cmd)
{
  printf ("requires %s\n", cmd);
  fflush (stdout);
  if (system (cmd) != 0) {
    printf ("Test skipped because prerequisite is missing or not working.\n");
    exit (77);
  }
}
