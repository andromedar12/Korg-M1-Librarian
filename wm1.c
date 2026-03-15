/*

    wm1.c

    Copyright (C) 2002 Angel Ortega <angel@triptico.com>

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


#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#include <stdlib.h>

#include <windows.h>
#include <commdlg.h>
#include <mmsystem.h>

#include "wm1.h"
#include "wm1edit.h"


/*************************************
	Control MIDI, Windows
*************************************/

/* tamaño del buffer midi */
#define MIDIBUFFERSIZE 40000

/* constantes generales */
#define MIDI_IN 	1
#define MIDI_OUT	0

#define ON		1
#define OFF		0


/* estructura de control de un buffer */
struct midibuf
{
	int miditype;			/* MIDI_IN ó MIDI_OUT */
	HANDLE hmidi;			/* hmidiin ó hmidiout */
	HGLOBAL hglb_midibuf;		/* handle de esta estructura */
	HGLOBAL hglb_midihdr;		/* handle del midihdr */
	HGLOBAL hglb_midibuffer;	/* handle del buffer */
	LPMIDIHDR lpmidihdr;		/* midi header (bloqueado) */
	unsigned int size;		/* tamaño de midibuffer */
};

#define LPMIDI struct midibuf FAR *


/* descriptor de la aplicación */
char * szApp="WM1 0.92";

/* handle global de la instancia */
HANDLE hinst;

/* números de dispositivo */

int midiinid=-1;
int midioutid=-1;

/* handle del midi out */
HMIDIOUT hmidiout=NULL;

/* handle del midi in */
HMIDIIN hmidiin=NULL;

/* handle de la ventana principal */
HWND hwndmain;

/* handle del cuadro de mensaje */
HWND hwndmsg;

/* estado de recepción de sysex */
int RecvSysexStatus=0;

/* función de procesamiento de sysex */
int (* SysexProc)(LPMIDIHDR lpmidihdr)=NULL;

/* descriptor de fichero global */
FILE * fd=NULL;

/* tipo byte */
typedef unsigned char byte;

/* midibuf para el procesamiento 'delayed' */
LPMIDI mididelay=NULL;

/* comando a enviar 'delayed' */
WORD wmcommand=0;


/*****************
	M1
*****************/

/* nombre del banco en disco */
char bankfile[1024];

/* un programa */
#define M1_PROG_SIZE 143
#define M1_PROG_SIZE_7BIT 164

/* número de programas en un banco (fijo) */
#define M1_PROG_NUM 100


/* estructura de un programa */
struct m1prog
{
	byte progname[10];		/* nombre del programa */
	byte data[M1_PROG_SIZE-10];	/* resto de los datos */
};


/* nombres de los programas en el M1 */
struct
{
	byte progname[11];		/* nombre del programa */
} m1names[M1_PROG_NUM];


/* un banco */
struct
{
	struct m1prog prog[M1_PROG_NUM];    /* los 100 programas del M1 */
} m1bank;


/** cadenas sysex específicas del M1 **/

/* cadena genérica de cabecera de los sysex del M1 */
byte * M1STR_SYSEX_GEN="\xf0\x42\x30\x19";

/* cadena para recibir un programa */
byte * M1STR_GET_PRG="\xf0\x42\x30\x19\x10\xf7";

/* cadena de cabecera de un programa */
byte * M1STR_PRG_HDR="\xf0\x42\x30\x19\x40";

/* cadena para recibir todos los programas */
byte * M1STR_GET_ALL_PRG="\xf0\x42\x30\x19\x1c\x00\xf7";

/* cadena para poner al M1 en modo programa */
byte * M1STR_PROGRAM_MODE="\xf0\x42\x30\x19\x4e\x02\x10\xf7";


/* programa destino (para funciones callback) */
int prgdes;

/* swap entre memoria y tarjeta del M1 */
int mem_card=0;
char * mem_card_ptr;


/************************
	Funciones
************************/


/** rutinas generales **/


/* empaqueta un mensaje midi corto */

#define MAKEMIDISHORTMSG(cStatus, cChannel, cData1, cData2)	       \
    cStatus | cChannel | (((UINT)cData1) << 8) | (((DWORD)cData2) << 16)


void SetMessage(char * msg)
/* define el mensaje que aparece en el cuadro de mensaje */
{
	/* si no hay un mensaje, se pone el por defecto */
	if(msg==NULL)
		msg="(C) Angel Ortega 1996/1999\r\njohannes@mail.ddnet.es";

	SetWindowText(hwndmsg, msg);
}


void SetErrorMessage(int miditype, int error)
/* muestra el mensaje de error */
{
	char tmp[512];
	char * errortype;

	if(error==0)
		return;

	if(miditype==MIDI_IN)
		errortype="WM1 - MIDI IN Error";
	else
		errortype="WM1 - MIDI OUT Error";

	if(error<0)
	{
		switch(error)
		{
		case -1:
			strcpy(tmp,"Out of memory.");
			break;

		case -2:
			strcpy(tmp,"Unexpected I/O operation.");
			break;

		case 9:
		default:
			strcpy(tmp,"Internal error. Please exit the program.");
			break;
		}
	}
	else
	{
		if(miditype==MIDI_IN)
			midiInGetErrorText(error,tmp,sizeof(tmp)-1);
		else
			midiOutGetErrorText(error,tmp,sizeof(tmp)-1);
	}

	MessageBox(hwndmain,tmp,errortype,MB_OK|MB_ICONSTOP);
}


/** inicialización **/

int MidiOutInit(void)
/* abre el dispositivo MIDI OUT */
{
	int ret;

	/* abre el dispositivo de salida */
	ret=midiOutOpen(&hmidiout,midioutid,0,0,0);

	if(ret)
	{
		hmidiout=NULL;
		SetErrorMessage(MIDI_OUT, ret);
	}

	return(ret);
}


int MidiInInit(void)
/* abre el dispositivo MIDI OUT */
{
	int ret;

	/* abre el dispositivo de entrada */
	ret=midiInOpen(&hmidiin, midiinid, (DWORD) hwndmain, 0, CALLBACK_WINDOW);

	if(ret)
	{
		hmidiin=NULL;
		SetErrorMessage(MIDI_IN, ret);
	}

	return(ret);
}


LPMIDI MidiAllocBuffer(int miditype, unsigned int size)
/* aloca un buffer */
{
	HGLOBAL hglb;
	LPMIDI midi;

	if(miditype==MIDI_IN && hmidiin==NULL)
		return(NULL);

	if(miditype==MIDI_OUT && hmidiout==NULL)
		return(NULL);

	/* crea primero un bloque midibuf */
	hglb=GlobalAlloc(GMEM_MOVEABLE|GMEM_SHARE|GMEM_ZEROINIT,sizeof(struct midibuf));

	if((midi=(LPMIDI) GlobalLock(hglb))==NULL)
	{
		GlobalFree(hglb);

		SetErrorMessage(miditype, -1);

		return(NULL);
	}

	/* ya está; se guarda el tamaño y el handle */
	midi->hglb_midibuf=hglb;
	midi->size=size;

	/* se crea el midihdr */
	midi->hglb_midihdr=GlobalAlloc(GMEM_MOVEABLE|GMEM_SHARE|GMEM_ZEROINIT, sizeof(MIDIHDR));

	if((midi->lpmidihdr=(LPMIDIHDR) GlobalLock(midi->hglb_midihdr))==NULL)
	{
		GlobalFree(midi->hglb_midihdr);

		GlobalUnlock(hglb);
		GlobalFree(hglb);

		SetErrorMessage(miditype, -1);

		return(NULL);
	}

	/* se crea el buffer */
	midi->hglb_midibuffer=GlobalAlloc(GMEM_MOVEABLE|GMEM_SHARE|GMEM_ZEROINIT,size);

	if((midi->lpmidihdr->lpData=GlobalLock(midi->hglb_midibuffer))==NULL)
	{
		GlobalFree(midi->hglb_midibuffer);

		GlobalUnlock(midi->hglb_midihdr);
		GlobalFree(midi->hglb_midihdr);

		GlobalUnlock(hglb);
		GlobalFree(hglb);

		SetErrorMessage(miditype, -1);

		return(NULL);
	}

	midi->hmidi=miditype==MIDI_IN?hmidiin:hmidiout;
	midi->miditype=miditype;
	midi->lpmidihdr->dwBufferLength=size;

	return(midi);
}


/* devuelve el puntero de datos del buffer */
#define MIDIDATA(midi) (midi->lpmidihdr->lpData)

/* devuelve el tamaño del buffer */
#define MIDISIZE(midi) (midi->lpmidihdr->dwBufferLength)

/* define el tamaño del buffer */
#define MIDISETSIZE(midi,s) midi->lpmidihdr->dwBufferLength=s


void MidiFreeBuffer(LPMIDI midi)
/* libera el buffer */
{
	HGLOBAL hglb;

	/* si es midi in, se desprepara */
	if(midi->miditype==MIDI_IN)
		midiInUnprepareHeader(midi->hmidi, midi->lpmidihdr, sizeof(MIDIHDR));

	GlobalUnlock(midi->hglb_midibuffer);
	GlobalFree(midi->hglb_midibuffer);

	GlobalUnlock(midi->hglb_midihdr);
	GlobalFree(midi->hglb_midihdr);

	hglb=midi->hglb_midibuf;
	GlobalUnlock(hglb);
	GlobalFree(hglb);
}


void MidiError(LPMIDI midi, int error)
/* muestra el error */
{
	SetErrorMessage(midi->miditype, error);
}


int MidiAction(LPMIDI midi)
/* efectúa una acción midi. Si es midiin, prepara la cabecera y hace un addbuffer; si es
   out, envía el paquete */
{
	int ret=0;

	if(midi->miditype==MIDI_IN)
	{
		/* en midi in, se define el tamaño como el tope */
		midi->lpmidihdr->dwBufferLength=midi->size;

		/* prepara la cabecera */
		ret=midiInPrepareHeader(midi->hmidi, midi->lpmidihdr, sizeof(MIDIHDR));

		/* añade el buffer */
		if(!ret)
			ret=midiInAddBuffer(midi->hmidi, midi->lpmidihdr, sizeof(MIDIHDR));
	}
	else
	{
		/* prepara la cabecera */
		ret=midiOutPrepareHeader(midi->hmidi, midi->lpmidihdr, sizeof(MIDIHDR));

		/* envía el mensaje */
		if(!ret)
			ret=midiOutLongMsg(midi->hmidi, midi->lpmidihdr, sizeof(MIDIHDR));

		/* envía el paquete */
		if(!ret)
			ret=midiOutUnprepareHeader(midi->hmidi, midi->lpmidihdr, sizeof(MIDIHDR));
	}

	MidiError(midi, ret);

	return(ret);
}



int MidiActionDelayed(LPMIDI midi)
/* hace un action 'delayed' */
{
	if(mididelay!=NULL)
		return(-3);
	else
		mididelay=midi;

	return(0);
}


int MidiRecord(unsigned int size)
/* activa/desactiva (si size==0) la grabación de sysex */
{
	int ret=0;
	static LPMIDI midi1=NULL;
	static LPMIDI midi2=NULL;

	/* si se intenta poner en un modo en el que ya está, error */
	if(size!=0)
	{
		if(RecvSysexStatus==1)
			ret=-2;

		/* crea dos buffers */
		if(!ret && (midi1=MidiAllocBuffer(MIDI_IN, size))==NULL)
			ret=-1;

		if(!ret && (midi2=MidiAllocBuffer(MIDI_IN, size))==NULL)
		{
			MidiFreeBuffer(midi1);

			ret=-1;
		}

		if(!ret)
			ret=MidiAction(midi1);

		if(!ret)
			ret=MidiAction(midi2);

		/* comienza la grabación */
		if(!ret)
		{
			if((ret=midiInStart(hmidiin))!=0)
			{   
				MidiFreeBuffer(midi1);
				MidiFreeBuffer(midi2);
			}
			else
				RecvSysexStatus=1;
		}
	}
	else
	{
		if(RecvSysexStatus==0)
			ret=-2;

		/* deja de grabar */
		if(!ret)
			ret=midiInStop(hmidiin);

		/* resetea si es necesario */
		if(!ret)
		{
			if(midi1->lpmidihdr->dwFlags!=MHDR_DONE || midi2->lpmidihdr->dwFlags!=MHDR_DONE)
				ret=midiInReset(hmidiin);
		}

		MidiFreeBuffer(midi1);
		MidiFreeBuffer(midi2);

		midi1=midi2=NULL;

		RecvSysexStatus=0;
	}

	SetErrorMessage(MIDI_IN, ret);

	return(ret);
}


int MidiProcessSysex(LPMIDIHDR lpmidihdr)
/* procesa el sysex que ha llegado en un mensaje */
{
	int ret;

	/* si no estamos esperando nada, fuera */
	if(RecvSysexStatus==0)
	{
		SetErrorMessage(MIDI_IN, -9);
		return(0);
	}

	if(lpmidihdr->dwBytesRecorded)
	{
		/* si hay una función de procesamiento, la ejecuta */
		if(SysexProc!=NULL)
			ret=SysexProc(lpmidihdr);

		if(ret)
			MidiRecord(0);
	}

	return(0);
}


void MidiSendStr(char * str, int size)
/* envía una cadena por midi */
{
	LPMIDI midi;

	/* pide un buffer */
	if((midi=MidiAllocBuffer(MIDI_OUT, MIDIBUFFERSIZE))==NULL)
		return;

	/* copia el mensaje */
	memcpy(MIDIDATA(midi), str, size);
	MIDISETSIZE(midi,size);

	MidiAction(midi);

	MidiFreeBuffer(midi);
}


/** operaciones con bancos **/

void RefreshBank(void)
/* rellena la lista con el banco */
{
	HWND hwnd;
	char tmp[100];
	int n,m;

	sprintf(tmp,"%s - [%s]", szApp, bankfile);
	SetWindowText(hwndmain, tmp);

	hwnd=GetDlgItem(hwndmain, WM1_LSTBANK);

	/* vacía la lista */
	SendMessage(hwnd, LB_RESETCONTENT, 0, 0L);

	for(n=0;n<M1_PROG_NUM;n++)
	{
		sprintf(tmp,"%02d ",n);

		for(m=0;m<10;m++)
			tmp[m+3]=m1bank.prog[n].progname[m];
		tmp[m+3]='\0';

		SendMessage(hwnd, LB_ADDSTRING, 0, (DWORD) (LPSTR) tmp);
	}

	/* rellena la lista del M1 */
	hwnd=GetDlgItem(hwndmain, WM1_LSTM1);

	SendMessage(hwnd, LB_RESETCONTENT, 0, 0L);

	for(n=0;n<M1_PROG_NUM;n++)
	{
		sprintf(tmp,"%02d %s",n, m1names[n].progname);

		SendMessage(hwnd, LB_ADDSTRING, 0, (DWORD) (LPSTR) tmp);
	}

}


void NewBank(void)
/* rellena m1bank a ceros */
{
	int n;

	for(n=0;n<M1_PROG_NUM;n++)
	{
		memset(&m1bank.prog[n],0,sizeof(struct m1prog));

		memcpy(m1bank.prog[n].progname,"Empty.....",10);

		strcpy(m1names[n].progname,"Korg M1");
	}

	strcpy(bankfile,"<unnamed>");

	RefreshBank();
}


void LoadBank(char * bnk)
/* carga un banco */
{
	int i;

	/* intenta abrirlo */
	if((i=open(bnk,O_RDONLY|O_BINARY))==-1)
		return;
	else
		read(i,&m1bank,sizeof(m1bank));

	close(i);

	strcpy(bankfile, bnk);

	RefreshBank();
}


void SaveBank(char * bnk)
/* salva el banco al disco */
{
	int i;

	if((i=open(bnk, O_CREAT|O_TRUNC|O_WRONLY|O_BINARY,
		S_IREAD|S_IWRITE))==-1)
		return;

	write(i,&m1bank,sizeof(m1bank));

	close(i);
}


void Decode(byte FAR * des, byte FAR * org, unsigned int size)
/* decodifica los bytes que han llegado en 7bit */
{
	byte bits7;
	byte bit;
	int cnt=0;

	while(size)
	{
		/* si no quedan bits que desplazar,
		   coge uno nuevo */
		if(cnt==0)
		{
			bits7=*org;
			org++;
			cnt=7;
		}

		bit=(bits7&0x1)?0x80:0x0;

		*des=*org|bit;

		cnt--;
		bits7>>=1;

		des++;
		org++;

		size--;
	}
}


void Encode(byte FAR * des, byte FAR * org, unsigned int size)
/* codifica en 7 bits org sobre des */
{
	byte mask;
	int cnt=0;
	byte FAR * maskptr;

	maskptr=des;
	des++;
	mask=0;

	while(size)
	{
		if(*org&0x80)
			mask|=0x80;

		*des=(*org&0x7f);

		des++;
		org++;
		size--;

		cnt++;
		mask>>=1;

		if(cnt==7)
		{
			*maskptr=mask;
			maskptr=des;
			des++;
			cnt=0;
			mask=0;
		}
	}

	if(cnt!=0)
		*maskptr=mask;
}


/** opciones **/

void SendSysexFile(char * file)
/* envía un sysex que esté en un fichero */
{
	long total=0;
	FILE * f;
	char tmp[100];
	int c;
	UINT n;
	LPSTR ptr;
	LPMIDI midi;

	/* pide un buffer */
	if((midi=MidiAllocBuffer(MIDI_OUT,MIDIBUFFERSIZE))==NULL)
		return;

	if((f=fopen(file, "rb"))==NULL)
		return;

	SetMessage("Starting send...");

	for(;;)
	{
		/* lee hasta que se llena el buffer o se acaba el fichero */
		for(n=0,ptr=MIDIDATA(midi);n<MIDISIZE(midi);n++)
		{
			if((c=fgetc(f))==EOF)
				break;

			*ptr=(char) c;
			ptr++;

			/* si aquí hay un EOX, se corta */
			if(c==0xf7)
			{
				n++;
				break;
			}
		}

		if(n==0)
			break;

		MIDISETSIZE(midi,n);

		if(MidiAction(midi))
			break;

		total+=n;

		sprintf(tmp,"%lu bytes sent",total);
		SetMessage(tmp);
	}

	sprintf(tmp,"Total: %lu bytes",total);
	SetMessage(tmp);

	MidiFreeBuffer(midi);

	fclose(f);
}


int RecvSysexFile(LPMIDIHDR lpmidihdr)
/* función SysexProc de guardar en fichero */
{
	UINT n;
	char tmp[25];

	/* sospecho que el driver de la SBPro no incluye el 0xf0 al principio, porque
	   en las documentaciones dice que tiene que aparecer y aquí nunca ha
	   llegado. De todas formas, en otros casos eran más bytes los que no aparecían */
	if(*(lpmidihdr->lpData)!=0xf0)
		fputc(0xf0, fd);

	for(n=0;n<lpmidihdr->dwBytesRecorded;n++)
		fputc(lpmidihdr->lpData[n],fd);

	sprintf(tmp,"%lu bytes",ftell(fd));

	SetMessage(tmp);

	/* devuelve 0 para no interrumpir la recepción */
	return(0);
}


int RecvProg(LPMIDIHDR lpmidihdr)
/* Función SysexProc: recibe un programa */
{
	int org;

	if(lpmidihdr->dwBytesRecorded<20)
		return(0);

	if(*lpmidihdr->lpData==0xf0)
		org=5;
	else
		org=4;

	Decode((byte FAR *) &m1bank.prog[prgdes],
		(byte FAR *) &lpmidihdr->lpData[org], M1_PROG_SIZE);

	SetMessage("Received.");

	return(1);
}


int RecvAllProg(LPMIDIHDR lpmidihdr)
/* función SysexProc: recibe todos los programas */
{
	int org;

	if(lpmidihdr->dwBytesRecorded<20)
		return(0);

	if(*lpmidihdr->lpData==0xf0)
		org=6;
	else
		org=5;

	Decode((byte FAR *) &m1bank.prog[0],
		(byte FAR *) &lpmidihdr->lpData[org], M1_PROG_SIZE*M1_PROG_NUM);

	SetMessage("Received.");

	RefreshBank();

	return(1);
}


int RecvAllProgM1(LPMIDIHDR lpmidihdr)
/* función SysexProc: recibe todos los programas y los pone en la lista del M1 */
{
	int n;
	int org;

	if(lpmidihdr->dwBytesRecorded<20)
		return(0);

	if(*lpmidihdr->lpData==0xf0)
		org=6;
	else
		org=5;

	Decode((byte FAR *) &m1bank.prog[0],
		(byte FAR *) &lpmidihdr->lpData[org], M1_PROG_SIZE*M1_PROG_NUM);

	SetMessage(NULL);

	for(n=0;n<M1_PROG_NUM;n++)
	{
		memcpy(m1names[n].progname, m1bank.prog[n].progname, 10);
		m1names[n].progname[10]='\0';
	}

	RefreshBank();

	return(1);
}


int AllFromM1(int copytom1)
/* lee todos los programas del M1 */
{
	LPMIDI midi;
	LPSTR ptr;

	/* pide un buffer */
	if((midi=MidiAllocBuffer(MIDI_OUT, MIDIBUFFERSIZE))==NULL)
		return(-1);

	/* crea la cabecera de petición de envío */
	ptr=MIDIDATA(midi);
	memcpy(ptr,M1STR_GET_ALL_PRG,7);
	MIDISETSIZE(midi,7);

	if(copytom1)
		SysexProc=RecvAllProgM1;
	else
		SysexProc=RecvAllProg;

	SetMessage("Requesting all patches...");

	if(MidiActionDelayed(midi))
		return(-4);

	return(1);
}


int FromM1(void)
/* coge el primer sonido marcado en el M1 sobre el primero marcado en el banco */
{
	int n,m;
	HWND hwndb, hwndm;
	LPMIDI midi;
	LPSTR ptr;
	DWORD midimsg;

	/* si hay un mensaje en proceso, nada */
	if(mididelay!=NULL)
		return(1);

	/* coge los handles de las listas */
	hwndb=GetDlgItem(hwndmain, WM1_LSTBANK);
	hwndm=GetDlgItem(hwndmain, WM1_LSTM1);

	for(n=0;n<M1_PROG_NUM;n++)
	{
		/* si está seleccionado en el M1... */
		if(SendMessage(hwndm, LB_GETSEL, n, 0L))
			break;
	}

	if(n==M1_PROG_NUM)
		return(0);

	/* desactiva ya el programa */
	SendMessage(hwndm, LB_SETSEL, 0, MAKELPARAM(n, 0));

	/* busca el programa destino */
	for(m=0;m<M1_PROG_NUM;m++)
	{
		if(SendMessage(hwndb, LB_GETSEL, m, 0L))
			break;
	}

	/* si no hay ninguno más, fuera */
	if(m==M1_PROG_NUM)
		return(0);

	/* lo desactiva */
	SendMessage(hwndb, LB_SETSEL, 0, MAKELPARAM(m, 0));

	SetMessage("Processing...");

	prgdes=m;

	/* envía un mensaje al M1 para que active el programa */
	midimsg=MAKEMIDISHORTMSG(0xc0,0,(char)n,0);

	midiOutShortMsg(hmidiout, midimsg);

	/* comienza una grabación */
	SysexProc=RecvProg;

	/* pide un buffer */
	if((midi=MidiAllocBuffer(MIDI_OUT, MIDIBUFFERSIZE))==NULL)
		return(-1);

	/* crea la cabecera de petición de envío */
	ptr=MIDIDATA(midi);
	memcpy(ptr,M1STR_GET_PRG,strlen(M1STR_GET_PRG));
	MIDISETSIZE(midi,strlen(M1STR_GET_PRG));

	if(MidiActionDelayed(midi))
		return(-4);

	return(1);
}


int AllToM1(void)
/* envía todo el banco al M1 */
{
	unsigned char * ptr;
	LPMIDI midi;
	int n;

	SetMessage("Sending all patches...");

	midi=MidiAllocBuffer(MIDI_OUT, MIDIBUFFERSIZE);

	ptr=MIDIDATA(midi);
	memcpy(ptr,M1STR_SYSEX_GEN,strlen(M1STR_SYSEX_GEN));

	ptr+=strlen(M1STR_SYSEX_GEN);

	*ptr=0x4c;
	ptr++;

	*ptr=(char) mem_card;
	ptr++;

	Encode(ptr, (LPSTR) &m1bank.prog[0], M1_PROG_NUM*M1_PROG_SIZE);

	ptr+=(16343);

	*ptr=0xf7;
	ptr++;

	MIDISETSIZE(midi,ptr-MIDIDATA(midi));

	MidiAction(midi);

	MidiFreeBuffer(midi);

	for(n=0;n<M1_PROG_NUM;n++)
	{
		memcpy(m1names[n].progname, m1bank.prog[n].progname, 10);
		m1names[n].progname[10]='\0';
	}

	SetMessage("Finished.");

	RefreshBank();

	return(0);
}


int ToM1(void)
/* envía el primer sonido seleccionado del banco al primero del M1 */ 
{
	int n,m;
	HWND hwndb, hwndm;
	LPMIDI midi;
	unsigned char * ptr;

	/* coge los handles de las listas */
	hwndb=GetDlgItem(hwndmain, WM1_LSTBANK);
	hwndm=GetDlgItem(hwndmain, WM1_LSTM1);

	for(m=0;m<M1_PROG_NUM;m++)
	{
		if(SendMessage(hwndb, LB_GETSEL, m, 0L))
			break;
	}

	if(m==M1_PROG_NUM)
		return(0);

	/* desactiva ya el programa */
	SendMessage(hwndb, LB_SETSEL, 0, MAKELPARAM(m, 0));

	/* busca el programa destino */
	for(n=0;n<M1_PROG_NUM;n++)
	{
		if(SendMessage(hwndm, LB_GETSEL, n, 0L))
			break;
	}

	/* si no hay ninguno más, fuera */
	if(n==M1_PROG_NUM)
		return(0);

	/* lo desactiva */
	SendMessage(hwndm, LB_SETSEL, 0, MAKELPARAM(n, 0));

	/* pide un buffer */
	if((midi=MidiAllocBuffer(MIDI_OUT, MIDIBUFFERSIZE))==NULL)
		return(0);

	/* codifica los datos */
	ptr=MIDIDATA(midi);

	/* añade la cabecera de programa */
	memcpy(ptr, M1STR_PRG_HDR, strlen(M1STR_PRG_HDR));
	ptr+=strlen(M1STR_PRG_HDR);

	/* codifica en 7 bits */
	Encode((byte FAR *) ptr, (byte FAR *) &m1bank.prog[m], M1_PROG_SIZE);
	ptr+=M1_PROG_SIZE_7BIT;

	/* añade el EOX */
	*ptr=0xf7;
	ptr++;

	MIDISETSIZE(midi, ptr-MIDIDATA(midi));

	/* lo envía */
	MidiAction(midi);

	/* ya está en el buffer: solicitar ahora que se grabe */
	ptr=MIDIDATA(midi);

	memcpy(ptr, M1STR_SYSEX_GEN, strlen(M1STR_SYSEX_GEN));
	ptr+=strlen(M1STR_SYSEX_GEN);

	*ptr=0x11;
	ptr++;
	*ptr=(char) mem_card;
	ptr++;
	*ptr=n;
	ptr++;
	*ptr=0xf7;
	ptr++;

	MIDISETSIZE(midi, ptr-MIDIDATA(midi));

	MidiAction(midi);

	MidiFreeBuffer(midi);

	/* actualiza el nombre en la lista del M1 */
	memcpy(m1names[n].progname, m1bank.prog[m].progname,10);
	m1names[n].progname[10]='\0';

	return(1);
}


int AskForFile(int foropen, char * file, char * filter, char * title)
/* pide un nombre de fichero */
{
	int ret;
	OPENFILENAME ofn;

	memset(&ofn, 0, sizeof(OPENFILENAME));
	*file='\0';

	ofn.lStructSize = sizeof(OPENFILENAME);
	ofn.hwndOwner = hwndmain;
	ofn.lpstrFilter = filter;
	ofn.nFilterIndex = 1;
	ofn.lpstrFile= file;
	ofn.nMaxFile = 150;
	ofn.lpstrTitle = title;

	if(foropen)
	{
		ofn.Flags = OFN_PATHMUSTEXIST|OFN_HIDEREADONLY|
			OFN_NOCHANGEDIR|OFN_FILEMUSTEXIST;
		ret=GetOpenFileName(&ofn);
	}
	else
	{
		ofn.Flags=OFN_HIDEREADONLY|OFN_NOCHANGEDIR;
		ret=GetSaveFileName(&ofn);
	}

	return(ret);
}


int EditProgram(void)
/* edita el programa */
{
	int n;
	HWND hwnd;

	hwnd=GetDlgItem(hwndmain, WM1_LSTBANK);

	for(n=0;n<M1_PROG_NUM;n++)
	{
		if(SendMessage(hwnd, LB_GETSEL, n, 0L))
			break;
	}

	if(n==M1_PROG_NUM)
	{
		MessageBox(hwndmain,"Please select a program in the left list first.",
			szApp,MB_OK);

		return(0);
	}
	else
		return(WM1Edit(hinst, (char *) &m1bank.prog[n]));
}


#pragma argsused

BOOL FAR PASCAL Wm1ConfigProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
/* procedimiento de la ventana de configuración */
{
	int num;
	int n;
	MIDIINCAPS incaps;
	MIDIOUTCAPS outcaps;

	switch(message)
	{
	case WM_INITDIALOG:

		num=midiInGetNumDevs();

		for(n=0;n<num;n++)
		{
			midiInGetDevCaps(n,(LPMIDIINCAPS) &incaps, sizeof(incaps));

			SendDlgItemMessage(hwnd, WM1C_LSTIN, LB_ADDSTRING, 0,
				(DWORD)(LPSTR)incaps.szPname);
		}

		SendDlgItemMessage(hwnd, WM1C_LSTIN, LB_SETCURSEL, 0, 0L);

		num=midiOutGetNumDevs();

		for(n=0;n<num;n++)
		{
			midiOutGetDevCaps(n,(LPMIDIOUTCAPS) &outcaps, sizeof(outcaps));

			SendDlgItemMessage(hwnd, WM1C_LSTOUT, LB_ADDSTRING, 0,
				(DWORD)(LPSTR)outcaps.szPname);
		}

		SendDlgItemMessage(hwnd, WM1C_LSTOUT, LB_SETCURSEL, 0, 0L);

		return(TRUE);

	case WM_COMMAND:

		switch(wparam)
		{
		case WM1C_OK:

			midiinid=SendDlgItemMessage(hwnd, WM1C_LSTIN, LB_GETCURSEL,0, 0L);
			midioutid=SendDlgItemMessage(hwnd, WM1C_LSTOUT, LB_GETCURSEL,0, 0L);

			EndDialog(hwnd, 1);
			break;

		case WM1C_CANCEL:
			EndDialog(hwnd, 0);
			break;
		}

		return(TRUE);
	}

	return(FALSE);
}



void ReadInitFile(void)
/* lee el fichero de inicio, donde se guardan los números de los dispositivos */
{
	char tmp[100];

	/* lee los dispositivos */
	GetPrivateProfileString("WM1","In","-1",tmp,sizeof(tmp)-1,"WM1.INI");
	midiinid=atoi(tmp);

	/* lee los dispositivos */
	GetPrivateProfileString("WM1","Out","-1",tmp,sizeof(tmp)-1,"WM1.INI");
	midioutid=atoi(tmp);

	/* si alguno es -1... */
	if(midiinid==-1 || midioutid==-1)
	{
		/* si existen los dispositivos... */
		if(midiInGetNumDevs()!=0 && midiOutGetNumDevs()!=0)
		{
			/* si se confirmó, se guardan */
			if(DialogBox(hinst,"WM1CONFIG",hwndmain,Wm1ConfigProc)==1)
			{
				sprintf(tmp,"%d",midiinid);
				WritePrivateProfileString("WM1","In",tmp,"WM1.INI");

				sprintf(tmp,"%d",midioutid);
				WritePrivateProfileString("WM1","Out",tmp,"WM1.INI");
			}
		}
	}
}


#pragma argsused

BOOL FAR PASCAL Wm1Proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
/* procedimiento de la ventana principal */
{
	char tmp[250];

	switch(message)
	{
	case WM_INITDIALOG:

		/* pone el texto en el título */
		SetWindowText(hwnd, szApp);

		/* guarda el handle del cuadro de mensaje */
		hwndmsg=GetDlgItem(hwnd, WM1_MESSAGE);

		/* guarda el handle de la ventana */
		hwndmain=hwnd;

		/* lee el fichero de inicialización */
		ReadInitFile();

		/* inicializa el midi Out */
		if(MidiOutInit())
		{
			EndDialog(hwnd, 0);

			return(TRUE);
		}

		/* inicializa el midi in */
		if(MidiInInit())
		{
			midiOutClose(hmidiout);

			EndDialog(hwnd, 0);

			return(TRUE);
		}

		/* crea un banco nuevo */
		NewBank();

		/* define el timer */
		SetTimer(hwnd, 0, 1000, NULL);

		/* rellena el mensaje */
		SetMessage("Please wait...");

		/* comando retardado a enviar */
		wmcommand=WM1I_PROGRAM_MODE;

		mem_card_ptr=M1STR_GET_ALL_PRG+5;

		return(TRUE);

	case WM_COMMAND:

		switch(wparam)
		{
		case WM1_ALLBANK:

			AllToM1();

			break;

		case WM1_ALLM1:

			AllFromM1(0);

			break;

		case WM1_OPEN:

			if(AskForFile(1,tmp,
				"*.km1;*.m1p (Korg M1 sound library)\0*.km1;*.m1p\0",
				"Korg M1 sound library to load"))
				LoadBank(tmp);

			break;

		case WM1_RIGHT:

			if(ToM1()==1)
				wmcommand=WM1_RIGHT;
			else
			{
				RefreshBank();
				wmcommand=0;
			}

			break;

		case WM1_LEFT:

			if(FromM1()==1)
				wmcommand=WM1_LEFT;
			else
			{
				RefreshBank();
				wmcommand=0;
			}

			break;

		case WM1_SAVE:

			strcpy(tmp, bankfile);
			if(AskForFile(0,tmp,
				"*.km1 (Korg M1 sound library)\0*.km1\0",
				"Korg M1 sound library to save"))
				SaveBank(tmp);

			break;

		case WM1_RECVSYSEX:
			if(!RecvSysexStatus)
			{
				if(AskForFile(0,tmp,"*.ksx (Sysex)\0*.ksx\0",
					"File name to save sysex"))
				{
					if(!MidiRecord(MIDIBUFFERSIZE))
					{
						SetDlgItemText(hwnd, WM1_RECVSYSEX,
							"S&top");

						SetMessage("Waiting for sysex...");

						/* abre el fichero */
						fd=fopen(tmp, "wb");

						/* define la función de proceso */
						SysexProc=RecvSysexFile;
					}
				}
			}
			else
			{
				SetDlgItemText(hwnd, WM1_RECVSYSEX, "&Receive sysex...");

				/* cierra el fichero */
				if(fd!=NULL)
				{
					fclose(fd);
					fd=NULL;
				}

				/* anula la función de proceso */
				SysexProc=NULL;

				MidiRecord(0);
			}

			/* habilita o deshabilita el resto de los botones según
			   el estado de recepción o no */
			EnableWindow(GetDlgItem(hwnd,WM1_SENDSYSEX), !RecvSysexStatus);
			EnableWindow(GetDlgItem(hwnd,WM1_OPEN), !RecvSysexStatus);
			EnableWindow(GetDlgItem(hwnd,WM1_RIGHT), !RecvSysexStatus);
			EnableWindow(GetDlgItem(hwnd,WM1_LEFT), !RecvSysexStatus);
			EnableWindow(GetDlgItem(hwnd,WM1_SAVE), !RecvSysexStatus);
			EnableWindow(GetDlgItem(hwnd,WM1_EDITPROG), !RecvSysexStatus);

			break;

		case WM1_SENDSYSEX:

			if(AskForFile(1,tmp,
				"*.ksx;*.sx;*.syx (Sysex)\0*.ksx;*.sx;*.syx\0All files\0*.*\0",
				"Sysex file to send"))
			{
				EnableWindow(GetDlgItem(hwnd,WM1_RECVSYSEX), FALSE);

				SendSysexFile(tmp);

				EnableWindow(GetDlgItem(hwnd,WM1_RECVSYSEX), TRUE);
			}

			break;

		case WM1_EDITPROG:

			/* Edita el programa */
			if(EditProgram())
				RefreshBank();

			break;

		case WM1_SWAPMEMCARD:

			mem_card=!mem_card;

			if(!mem_card)
				SetDlgItemText(hwnd,WM1_SWAPMEMCARD,"&Mem");
			else
				SetDlgItemText(hwnd,WM1_SWAPMEMCARD,"&Card");

			*mem_card_ptr=mem_card;

			break;

		/* comandos internos */

		case WM1I_PROGRAM_MODE:

			/* pone al M1 en modo programa */
			MidiSendStr(M1STR_PROGRAM_MODE, strlen(M1STR_PROGRAM_MODE));

			/* siguiente mensaje */
			wmcommand=WM1I_GET_M1_PROGS;

			break;

		case WM1I_GET_M1_PROGS:

			/* recibe los programas en el M1, para mostrarlos en la lista */
			AllFromM1(1);

			break;
		}

		return(TRUE);

	case WM_SYSCOMMAND:

		if(wparam==SC_CLOSE)
		{
			SetMessage("Closing MIDI ports...");

			midiInClose(hmidiin);
			midiOutClose(hmidiout);

			KillTimer(hwnd, 0);

			EndDialog(hwnd, 0);
		}

		return(FALSE);

	case MM_MIM_LONGDATA:

		/* llegada de mensaje sysex */
		if(RecvSysexStatus)
			MidiProcessSysex((LPMIDIHDR) lparam);

		return(FALSE);

	case WM_TIMER:

		/* temporizador */

		/* si hay alguna operación delayed, se procesa */
		if(mididelay!=NULL)
		{
			MidiRecord(MIDIBUFFERSIZE);

			MidiAction(mididelay);

			MidiFreeBuffer(mididelay);

			mididelay=NULL;
		}
		else
		/* si hay algún comando, lo mismo */
		if(wmcommand!=0 && !RecvSysexStatus)
		{
			PostMessage(hwnd, WM_COMMAND, wmcommand, 0L);
			wmcommand=0;
		}

		return(FALSE);
	}

	return(FALSE);
}


#pragma argsused

int PASCAL WinMain (HANDLE hInstance, HANDLE hPrevInstance,
		    LPSTR lpszCmdLine, int nCmdShow)
/* Función principal */
{
	/* Crea una cola de mensajes de 100, para tener suficientes postmessage */
	SetMessageQueue(100);

	if (hPrevInstance)
	{
		MessageBox(NULL,
			"There is one copy of the program still running.",
			szApp,MB_OK);
		return(1);
	}

	hinst=hInstance;

	DialogBox(hinst,"WM1",NULL,(DLGPROC) Wm1Proc);

	return(0);
}
