/*

    wm1.h

    Copyright (C) 1996 Angel Ortega <angel@triptico.com>

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

    http://www.triptico.com

*/


/* comandos de la ventana principal */

#define WM1_LSTBANK	100
#define WM1_LSTM1	101
#define WM1_OPEN	200
#define WM1_RIGHT	201
#define WM1_LEFT	202
#define WM1_SAVE	203
#define WM1_RECVSYSEX	300
#define WM1_SENDSYSEX	301
#define WM1_MESSAGE	400
#define WM1_ALLBANK	500
#define WM1_ALLM1	501
#define WM1_EDITPROG	600
#define WM1_SWAPMEMCARD 601

/* comandos internos de la ventana principal */

#define WM1I_PROGRAM_MODE 150
#define WM1I_GET_M1_PROGS 151


/* comandos de la ventana de edición */

#define WM1E_PROG_NAME	100

#define WM1E_OK 	500
#define WM1E_CANCEL	501


/* comandos de la ventana de configuración */

#define WM1C_LSTIN	100
#define WM1C_LSTOUT	101

#define WM1C_OK 	500
#define WM1C_CANCEL	501
