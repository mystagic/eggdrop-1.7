/*
 * cmdt.h
 *   stuff for builtin commands
 *
 * $Id: cmdt.h,v 1.1 2004/08/25 01:02:02 wcc Exp $
 */
/*
 * Copyright (C) 1997 Robey Pointer
 * Copyright (C) 1999, 2000, 2001, 2002, 2003, 2004 Eggheads Development Team
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#ifndef _EGG_CMDT_H
#define _EGG_CMDT_H

#define CMD_LEAVE    (Function)(-1)
typedef struct {
  char *name;
  char *flags;
  Function func;
  char *funcname;
} cmd_t;

typedef struct {
  char *name;
  Function func;
} botcmd_t;

#endif /* _EGG_CMDT_H */
