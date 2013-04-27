/*************************************************************************
** NewWolf
** Copyright (C) 1999-2002 by DarkOne
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the GNU General Public License
** as published by the Free Software Foundation; either version 2
** of the License, or (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**************************************************************************
** console input/output processing
*************************************************************************/
#include "WolfDef.h"

// ------------------------- * types * -------------------------
#define CON_NOTIFY_LINES		4		// number of notify lines
#define CON_LINES						512	// number of lines in console buffer
#define CON_MAX_LINE_WIDTH	64	// maximum line width /normally it is ~100 under 800x600/
#define CON_INPUT_LENGTH		256	// maximum input string length
#define CON_INPUT_HISTORY		32	// console input history
// - specific stuff -
#define CON_X_OFFSET				4		// offset from the right side of the screen
#define CON_BOTT_OFFSET			4		// offset from the bottom of the console
#define CON_FONT_HEIGHT			14	// console font size

typedef struct con_input_s
{
	int x, len, scroll;
	char text[CON_INPUT_LENGTH];
} con_input_t;

typedef struct console_s
{
	bool initialized;
	bool forcedup;		// full screen console
	bool active;			// console can get input
	float frac_drop;	// amount console is dropped [0..1]

	int width;				// real line width /based on current resolution, but not greater then CON_MAX_LINE_WIDTH/
	int currentline;	// current print line
	int totallines;		// total lines in the console
	int backscroll;		// console back scroll
	int x;						// current print position
	char *line;				// current print line
	float notifytime[CON_NOTIFY_LINES]; // realtime the line was generated /for last n lines only/

	int in_total;			// total items in input history
	int in_history;		// which history line we've taken
	bool in_ovr;		// overwrite mode

	char text[CON_LINES][CON_MAX_LINE_WIDTH];					// console text buffer
	con_input_t in[CON_INPUT_HISTORY];	// console input buffer
	char logfile[MAX_OSPATH];													// console logfile
} console_t;

console_t con;
cvar_t *con_notifytime, *con_speed, *con_dropheight, *con_alpha;

char *Con_Line(int n);
void Con_ClearInput(void);

// ------------------------- * Devider * -------------------------

/*
** Con_Clear_f
*/
void Con_Clear_f(void)
{
	con.backscroll=con.totallines=con.currentline=con.x=0;
	con.line=con.text[0];
	*con.line=0;
}

/*
** Con_Dump_f
**
** Save the console contents out to a file
*/
void Con_Dump_f(void)
{
	char name[MAX_OSPATH];
	FILE *fp;
	int n;

	if(Cmd_Argc()!=2)
	{
		Con_Printf("use: condump <filename>\n");
		return;
	}

	strcpy(name, FS_ExpandFilename(Cmd_Argv(1)));
	fp=fopen(name, "wt");
	if(!fp)
	{
		Con_Printf("Error opening %s\n", name);
		return;
	}

	for(n=con.totallines-1; n>=0; n--)
		fprintf(fp, "%s\n", Con_Line(n));

	fclose(fp);
	Con_Printf("Dumped console to %s\n", name);
}

/*
** Con_ToggleConsole_f
*/
void Con_ToggleConsole_f(void)
{
	if(!con.initialized || con.forcedup) return;

	if(con.active=!con.active)
		key_dest=key_console;
	else
	{
		Con_ClearNotify();
		Con_ClearInput();
		key_dest=key_game;
	}
}

// ------------------------- * Devider * -------------------------

/*
** Con_Init
**
** Console initialization
*/
void Con_Init(void)
{
	cvar_t *logfile, *logfilename;
	FILE *fp;

// logging settings
	logfile=Cvar_Get("con_logfile", "1", CVAR_NOSET);
	logfilename=Cvar_Get("con_logfilename", "condump.txt", CVAR_NOSET);

	if(logfile->value)
	{
		strcpy(con.logfile, FS_ExpandFilename(logfilename->string));

		if(logfile->value==2) // append logfile
			fp=fopen(con.logfile, "at");
		else
			fp=fopen(con.logfile, "wt");
	}
	else
		fp=NULL;

	if(fp)
	{
		fprintf(fp, "\n");
		fprintf(fp, "This file was generated by NewWolf version %4.2f\n\n", VERSION);
		fprintf(fp, "If you have problems with it send this file\n");
		fprintf(fp, "with problem description to DarkOne@navigators.lv\n\n");
		fclose(fp);
	}
	else
	{
		Cvar_ForceSetInteger("con_logfile", 0);
		*con.logfile=0;
	}

	con.width=63; // fixed console width

// register our commands
	con_notifytime=Cvar_Get("con_notifytime", "3", CVAR_ARCHIVE);
	con_speed=Cvar_Get("con_speed", "1", CVAR_ARCHIVE);
	con_dropheight=Cvar_Get("con_dropheight", "0.5", CVAR_ARCHIVE);
	con_alpha=Cvar_Get("con_alpha", "1", CVAR_ARCHIVE);

	Cmd_AddCommand("clear", Con_Clear_f);
	Cmd_AddCommand("condump", Con_Dump_f);
	Cmd_AddCommand("toggleconsole", Con_ToggleConsole_f);

	con.initialized=true;
	Con_Printf("Console Initialized\n");
}

// ------------------------- * aux * -------------------------

/*
** Con_ClearInput
*/
void Con_ClearInput(void)
{
	con.in_history=0;
	con.in->x=con.in->len=con.in->scroll=0;
	*con.in->text=0;
}

/*
** Con_ClearNotify
*/
void Con_ClearNotify(void)
{
	memset(con.notifytime, 0, sizeof(con.notifytime));
}

/*
** Con_LineFeed
*/
void Con_LineFeed(void)
{
	if(++con.totallines>=CON_LINES) con.totallines=CON_LINES;
	con.currentline=(con.currentline+1)%CON_LINES;
	con.line=con.text[con.currentline];
	*con.line=0;
}

/*
** Con_Line
**
** returns pointer to line data
** n=0 for the latest line
*/
char *Con_Line(int n)
{
	if(n>=con.totallines || n<0) return "";

	return con.text[(CON_LINES+con.currentline-n)%CON_LINES];
}

/*
** Con_Print
*/
void Con_Print(char *txt)
{
	static bool cr=false;
	int c, l;

	con.backscroll=0;

// FIXME: colored chat messages
	if(*txt==1 || *txt==2)
		txt++;

	while(c=*txt)
	{
		if(*txt<=' ' && *txt!='\r' && *txt!='\n') // wrap only whole words
		{
		// count word length
			for(l=1; l<con.width; l++)
				if(txt[l]<=' ')	break;

		// word wrap
			if(l!=con.width && (con.x+l>=con.width))
			{
				con.line[con.x]='\0';
				c=*++txt;
				con.x=0;
			}
		}

		txt++;
		
		if(!con.x)
		{
			if(!cr)
				Con_LineFeed();
			else
				cr=false;
			con.notifytime[con.currentline%CON_NOTIFY_LINES]=ftime;
		}

		switch(c)
		{
		case '\n':
			con.line[con.x]='\0';
			con.x=0;
			break;
		case '\r':
			con.line[con.x]='\0';
			con.x=0;
			cr=true;
			break;
		case '\b':
			if(con.x>0)	con.x--;
			break;
		default: // display character and advance
			con.line[con.x++]=c;
			if(con.x>=con.width)
			{
				con.line[con.width]='\0';
			// do not wrap if end of line
				if(!*txt || *txt=='\r' || *txt=='\n' || *txt=='\r' || *txt=='\b')
					break; 
				con.x=0;
			}
			break;
		}
	}
	if(con.x) con.line[con.x]='\0';
	con.notifytime[con.currentline%CON_NOTIFY_LINES]=ftime;
}

/*
** Con_LogFile
*/
void Con_LogFile(char *text)
{
	FILE *fp;

	if(*con.logfile && (fp=fopen(con.logfile, "at")))
	{
		fprintf(fp, "%s", text);
		fclose(fp);
	}
}

// ------------------------- * Drawing * -------------------------

/*
** Con_DrawLines
*/
void Con_DrawLines(int lines, int y)
{
	int n;

	if(lines<=0) return;
	if(con.backscroll)
	{
	// draw backscroll sign on the first line
		FNT_SetColor(0xFF, 0xFF, 0x00, 0xFF);
		for(n=1; n<con.width-1; n+=2)
			FNT_PrintPosOff(n, lines, CON_X_OFFSET, y, "^");
		FNT_SetColor(0xFF, 0xFF, 0xFF, 0xFF);
	}
	else
		FNT_PrintPosOff(0, lines, CON_X_OFFSET, y, con.line);

	for(n=1; n<=lines; n++)
		FNT_PrintPosOff(0, lines-n, CON_X_OFFSET, y, Con_Line(n+con.backscroll));
}

/*
** Con_DrawInput
*/
void Con_DrawInput(int line, int y)
{
	int cur_pos;

	if(con.in->scroll)
	{
		FNT_PrintPosOff(0, line, CON_X_OFFSET, y, &con.in->text[con.in->scroll]);
		cur_pos=con.in->x-con.in->scroll;
	}
	else
	{
		FNT_PrintfPosOff(0, line, CON_X_OFFSET, y, "]%s", con.in->text);
		cur_pos=con.in->x+1;
	}

// draw blinking cursor
	if((int)(ftime*2)&1)
	{
		FNT_SetColor(0xFF, 0x00, 0xFF, 0xFF);
		FNT_PrintPosOff(cur_pos, line, CON_X_OFFSET, y, con.in_ovr?"\x0b":"_");
	}
}

/*
** Con_DrawNotify
*/
void Con_DrawNotify(void)
{
	int n, line;
	int offset=0;
	float t;

	n=CON_NOTIFY_LINES-1;
	if(n>=con.totallines) n=con.totallines-1;
	line=0;

	for(; n>=0; n--)
	{
		t=con.notifytime[(con.currentline+CON_NOTIFY_LINES-n)%CON_NOTIFY_LINES];
		if(!t || ftime-t>con_notifytime->value) continue;
		if(!offset)
		{
			t=(ftime-t)-0.8f*con_notifytime->value;
			if(t>0) offset=(int)(CON_FONT_HEIGHT*t/(0.2f*con_notifytime->value));
		}
		else
		{
			if(ftime-t>0.8f*con_notifytime->value)
				con.notifytime[(con.currentline+CON_NOTIFY_LINES-n)%CON_NOTIFY_LINES]=ftime-0.8f*con_notifytime->value;
		}

		FNT_PrintPosOff(0, line++, CON_X_OFFSET, -offset, Con_Line(n));
	}
}

/*
** Con_Draw
**
** draws whole console system
*/
void Con_Draw(void)
{
	float height; // ideal height
	int lines, offset, alpha;

	if(!con.initialized) return;

// check values change
	if(con_alpha->modified)
	{
	// clamp to the range of [0.1..1]
		if(con_alpha->value<0.1f)
			Cvar_SetValue("con_alpha", 0.1f);
		else if(con_alpha->value>1)
			Cvar_SetValue("con_alpha", 1);
	}
	if(con_dropheight->modified)
	{
	// clamp to the range of [0.1..0.9]
		if(con_dropheight->value<0.1f)
			Cvar_SetValue("con_dropheight", 0.1f);
		else if(con_dropheight->value>0.9f)
			Cvar_SetValue("con_dropheight", 0.9f);
	}

// height is where console should be
	if(con.active)
	{
		if(con.forcedup)
			height=con.frac_drop=1;
		else
			height=con_dropheight->value;
	}
	else
		height=0;

// moving console
	if(con.frac_drop>height)
	{
		con.frac_drop-=con_speed->value*dftime;
		if(con.frac_drop<height) con.frac_drop=height;
	}
	else if(con.frac_drop<height)
	{
		con.frac_drop+=con_speed->value*dftime;
		if(con.frac_drop>height) con.frac_drop=height;
	}

// calculation stuff
	lines=(int)(con.frac_drop*480);
	alpha=(int)(255*con_alpha->value*con.frac_drop/con_dropheight->value);
	if(alpha>255) alpha=255;

	FNT_SetFont(FNT_CONSOLE);
	FNT_AllowFMT(true);
	FNT_SetScale(1, 1);
	FNT_SetStyle(0, 0, 0);
	FNT_SetColor(0xFF, 0x00, 0x00, 0xFF);

// notify lines
	if(!con.active) Con_DrawNotify();

	if(!lines) // no console visible
	{
		FNT_AllowFMT(false);
		return; 
	}

// console background picture
	Vid_DrawConBack(lines, alpha);

// number of lines to draw
	offset=lines%CON_FONT_HEIGHT-CON_BOTT_OFFSET;
	lines=lines/CON_FONT_HEIGHT-1;
	if(lines<0) lines=0;

	FNT_SetColor(0xFF, 0xFF, 0xFF, alpha);
	FNT_SetColorSh(0x00, 0x00, 0x00, alpha);
	FNT_SetStyle(0, 1, 0);
	Con_DrawLines(lines-1, offset);
	FNT_AllowFMT(false);

	if(con.active) // console is active
		Con_DrawInput(lines, offset);
}

// ------------------------- * interface * -------------------------
#define	MAXPRINTMSG	4096

/*
** Con_Printf
*/
void Con_Printf(char *fmt, ...)
{
	va_list argptr;
	char msg[MAXPRINTMSG];

	if(!con.initialized) return;

	va_start(argptr,fmt);
	vsprintf(msg, fmt, argptr);
	va_end(argptr);

	Con_Print(msg);
	Con_LogFile(msg);
}

/*
** Msg_Printf
*/
void Msg_Printf(char *fmt, ...)
{
	va_list argptr;
	char msg[MAXPRINTMSG];

	if(!con.initialized) return;

	va_start(argptr,fmt);
	vsprintf(msg, fmt, argptr);
	va_end(argptr);

	Con_Print(msg);
	Con_Print("\n");
	Con_LogFile(msg);
	Con_LogFile("\n");
}

// ------------------------- * console keyboard input * -------------------------
extern bool keydown[256];
#define KP_FIRST		K_KP_HOME
#define KP_LAST			K_KP_PLUS
int kp_key[KP_LAST-KP_FIRST+1]=
{
	'7', '8', '9',
	'4', '5', '6',
	'1', '2', '3', K_ENTER,
	'0', '.',
	'/', '*', '-', '+'
};

/*
** Con_KeyInput
**
** console input processing
*/
void Con_Key(int key)
{
	if(!con.active) return; // console is not interested in input

// translate keypad keys to normal
	if(key>=KP_FIRST && key<=KP_LAST)
		key=kp_key[key-KP_FIRST];

	switch(key)
	{
	case K_ENTER: // Return (Enter)
		if(!con.in->len)
		{
			Con_Printf("]\n");
			Con_ClearInput();
			return;
		}
		Con_Printf("]%s\n", con.in->text);
		Cbuf_AddText(con.in->text);
		Cbuf_AddText("\n");
		memmove(con.in+1, con.in, sizeof(con.in)-sizeof(con.in[0]));
		if(con.in_total<CON_INPUT_HISTORY-1) con.in_total++;
		Con_ClearInput();
		return;
	case K_TAB: // Tab
	{
		char *cmd;

		if(cmd=Cmd_CompleteCommand(con.in->text))
		{
			strcpy(con.in->text, cmd);
			con.in->x=con.in->len=strlen(cmd);
		}
		return;
	}
	case K_BACKSPACE: // BackSpace
		if(!con.in->x) return;
		memmove(&con.in->text[con.in->x-1], &con.in->text[con.in->x], CON_INPUT_LENGTH-con.in->x);
		con.in->x--;
		con.in->len--;
		if(con.in->x<=con.in->scroll)
		{
			con.in->scroll-=20;
			if(con.in->scroll<0) con.in->scroll=0;
		}
		return;
	case K_DEL: // Del
		if(con.in->x==con.in->len) return;
		memmove(&con.in->text[con.in->x], &con.in->text[con.in->x+1], CON_INPUT_LENGTH-con.in->x-1);
		con.in->len--;
		return;
	case K_INS: // Insert
		con.in_ovr=!con.in_ovr;
		return;
	case K_RIGHTARROW:
		if(con.in->x<con.in->len) con.in->x++;
		if(con.in->x-con.in->scroll>=con.width-1) con.in->scroll++;
		return;
	case K_LEFTARROW:
		if(con.in->x>0) con.in->x--;
		if(con.in->x<=con.in->scroll)
		{
			con.in->scroll-=20;
			if(con.in->scroll<0) con.in->scroll=0;
		}
		return;
	case K_UPARROW:
		if(con.in_history>=con.in_total) return;
		con.in[0]=con.in[++con.in_history];
		return;
	case K_DOWNARROW:
		if(con.in_history<1) return;
		if(con.in_history==1)
		{
			Con_ClearInput();
			return;
		}
		con.in[0]=con.in[--con.in_history];
		return;
	case K_PGUP: // PgUp
	case K_MWHEELUP: // Mouse wheel Up
		if(keydown[K_CTRL])
			con.backscroll+=5;
		else
			con.backscroll+=2;
		if(con.backscroll>con.totallines-1)	con.backscroll=con.totallines-1;
		return;
	case K_PGDN: // PgDn
	case K_MWHEELDOWN: // Mouse wheel Down
		if(keydown[K_CTRL])
			con.backscroll-=5;
		else
			con.backscroll-=2;
		if(con.backscroll<0) con.backscroll=0;
		return;
	case K_HOME: // Home
		if(keydown[K_CTRL])
			con.backscroll=con.totallines-1;
		else
			con.in->x=con.in->scroll=0; // start of input line
		return;
	case K_END: // End
		if(keydown[K_CTRL])
			con.backscroll=0;
		else
		{
			con.in->x=con.in->len;			// end of input line
			if(con.in->x>=con.width-1)
				con.in->scroll=con.in->len-(con.width-2);
			return;
		}
	}
	if(key<32 || key>127)	return; // not printable

	if(!con.in_ovr || con.in->x==con.in->len) // inserting character at current position
	{
		if(con.in->x>=CON_INPUT_LENGTH) return;
		memmove(&con.in->text[con.in->x+1], &con.in->text[con.in->x], CON_INPUT_LENGTH-con.in->x-1);
		if(con.in->len<CON_INPUT_LENGTH) con.in->len++;
	}
	con.in->text[con.in->x++]=key;
	con.in->text[con.in->len]=0;
	if(con.in->x-con.in->scroll>=con.width-1) con.in->scroll++;
}