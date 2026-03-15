/*

    wm1edit.c

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


#include <windows.h>
#include <string.h>

#include "wm1.h"


typedef unsigned char byte;


/* un programa */
#define M1_PROG_SIZE 143

/* estructura de un programa */
struct
{
	byte name[10];			/* nombre del programa */
	byte data[M1_PROG_SIZE-10];	/* resto de los datos */
} Prg;



/**********************
	Funciones
***********************/


void PrgToWindow(HWND hwnd)
/* vuelca el programa en edición sobre los controles */
{
	int n;
	char tmp[100];

	for(n=0;n<sizeof(Prg.name);n++)
		tmp[n]=Prg.name[n];
	tmp[n]='\0';

	SetDlgItemText(hwnd, WM1E_PROG_NAME,tmp);
}


void WindowToPrg(HWND hwnd)
/* vuelca los controles de edición sobre el programa */
{
	int n;
	char tmp[100];

	GetDlgItemText(hwnd, WM1E_PROG_NAME, tmp, sizeof(tmp));

	for(n=0;n<sizeof(Prg.name);n++)
		Prg.name[n]=tmp[n];
}


#pragma argsused

BOOL FAR PASCAL Wm1EditProc(HWND hwnd, WORD message, WORD wparam, LONG lparam)
/* procedimiento de la ventana de edición */
{
	switch(message)
	{
	case WM_INITDIALOG:

		PrgToWindow(hwnd);

		return(TRUE);

	case WM_COMMAND:

		switch(wparam)
		{
		case WM1E_OK:

			WindowToPrg(hwnd);

			EndDialog(hwnd, 1);
			break;

		case WM1E_CANCEL:

			EndDialog(hwnd, 0);
			break;
		}

		return(TRUE);
	}

	return(FALSE);
}


int WM1Edit(HANDLE hinst, char * progdata)
/* edita un programa */
{
	/* copia el programa sobre el buffer de trabajo */
	memcpy((char *) &Prg, progdata, M1_PROG_SIZE);

	/* ejecuta la ventana */
	if(DialogBox(hinst,"WM1EDIT",NULL,(DLGPROC) Wm1EditProc))
	{
		/* si se pulsó OK, se vuelca sobre el programa */
		memcpy(progdata, (char *) &Prg, M1_PROG_SIZE);
		return(1);
	}

	return(0);
}
