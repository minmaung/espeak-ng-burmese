/***************************************************************************
 *   Copyright (C) 2005, 2006 by Jonathan Duddington                       *
 *   jsd@clara.co.uk                                                       *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>

#include <wctype.h>

#include "speech.h"
#include "phoneme.h"
#include "synthesize.h"
#include "translate.h"

#define WORD_STRESS_CHAR   '*'


Translator *translator = NULL;
int option_log_trans = 0;
int option_tone1 = 0;
int option_tone2 = 0;
int option_stress_rule = 0;
int option_words = 0;
int option_vowel_pause;
int option_unstressed_wd1;
int option_unstressed_wd2;
int option_phonemes = 0;
int option_capitals = 0;
int option_punctuation = 0;
char option_punctlist[N_PUNCTLIST]={0};
char ctrl_embedded = '\001';    // to allow an alternative CTRL for embedded commands
int option_utf8=2;   // 0=no, 1=yes, 2=auto

FILE *f_trans = NULL;
FILE *f_input = NULL;
int ungot_char2 = 0;
char *p_input;
int ungot_char;
int end_of_input;

// these are overridden by defaults set in the "speak" file
int option_linelength = 0;

#define N_EMBEDDED_LIST  250
static int embedded_ix;
unsigned char embedded_list[N_EMBEDDED_LIST];

#define N_TR_SOURCE    300
static char source[N_TR_SOURCE];     // the source text of a single clause

int n_replace_phonemes;
REPLACE_PHONEMES replace_phonemes[N_REPLACE_PHONEMES];


// brackets, also 0x2014 to 0x021f which don't need to be in this list
static const short brackets[] = {
'(',')','[',']','{','}','<','>','"','\'','`',
0xab,0xbb,  // double angle brackets
0};

// other characters which break a word, but don't produce a pause
static const short breaks[] = {'_', 0};

// punctuations symbols that can end a clause
static const short punct_chars[] = {',','.','?','!',':',';',
  0x2026,  // elipsis
  0};

// bits 0-7 pause x 10mS, bits 8-11 intonation type,
#define CLAUSE_NONE        0 + 0400
#define CLAUSE_PARAGRAPH  70
#define CLAUSE_EOF        35
#define CLAUSE_VOICE       0 + 0x1400
#define CLAUSE_PERIOD     35
#define CLAUSE_COMMA      20 + 0x100
#define CLAUSE_QUESTION   35 + 0x200
#define CLAUSE_EXCLAMATION 40 + 0x300
#define CLAUSE_COLON      30
#ifdef PLATFORM_RISCOS
#define CLAUSE_SEMICOLON  30
#else
#define CLAUSE_SEMICOLON  30 + 0x100
#endif

// indexed by (entry num. in punct_chars) + 1
static const short punct_attributes [] = { 0,
  CLAUSE_COMMA, CLAUSE_PERIOD, CLAUSE_QUESTION, CLAUSE_EXCLAMATION, CLAUSE_COLON, CLAUSE_SEMICOLON,
  CLAUSE_SEMICOLON,  // elipsis
  0 };


static const char *punct_stop = ".:!?";    // pitch fall if followed by space
static const char *punct_close = ")]}>;'\"";  // always pitch fall unless followed by alnum


// translations from 8bit charsets to unicode
static const unsigned short *charset_a0;
static const unsigned short ISO_8859_1[0x60] = {
   0x00a0, 0x00a1, 0x00a2, 0x00a3, 0x00a4, 0x00a5, 0x00a6, 0x00a7, // a0
   0x00a8, 0x00a9, 0x00aa, 0x00ab, 0x00ac, 0x00ad, 0x00ae, 0x00af, // a8
   0x00b0, 0x00b1, 0x00b2, 0x00b3, 0x00b4, 0x00b5, 0x00b6, 0x00b7, // b0
   0x00b8, 0x00b9, 0x00ba, 0x00bb, 0x00bc, 0x00bd, 0x00be, 0x00bf, // b8
   0x00c0, 0x00c1, 0x00c2, 0x00c3, 0x00c4, 0x00c5, 0x00c6, 0x00c7, // c0
   0x00c8, 0x00c9, 0x00ca, 0x00cb, 0x00cc, 0x00cd, 0x00ce, 0x00cf, // c8
   0x00d0, 0x00d1, 0x00d2, 0x00d3, 0x00d4, 0x00d5, 0x00d6, 0x00d7, // d0
   0x00d8, 0x00d9, 0x00da, 0x00db, 0x00dc, 0x00dd, 0x00de, 0x00df, // d8
   0x00e0, 0x00e1, 0x00e2, 0x00e3, 0x00e4, 0x00e5, 0x00e6, 0x00e7, // e0
   0x00e8, 0x00e9, 0x00ea, 0x00eb, 0x00ec, 0x00ed, 0x00ee, 0x00ef, // e8
   0x00f0, 0x00f1, 0x00f2, 0x00f3, 0x00f4, 0x00f5, 0x00f6, 0x00f7, // f0
   0x00f8, 0x00f9, 0x00fa, 0x00fb, 0x00fc, 0x00fd, 0x00fe, 0x00ff, // f8
};

static const unsigned short ISO_8859_2[0x60] = {
   0x00a0, 0x0104, 0x02d8, 0x0141, 0x00a4, 0x013d, 0x015a, 0x00a7, // a0
   0x00a8, 0x0160, 0x015e, 0x0164, 0x0179, 0x00ad, 0x017d, 0x017b, // a8
   0x00b0, 0x0105, 0x02db, 0x0142, 0x00b4, 0x013e, 0x015b, 0x02c7, // b0
   0x00b8, 0x0161, 0x015f, 0x0165, 0x017a, 0x02dd, 0x017e, 0x017c, // b8
   0x0154, 0x00c1, 0x00c2, 0x0102, 0x00c4, 0x0139, 0x0106, 0x00c7, // c0
   0x010c, 0x00c9, 0x0118, 0x00cb, 0x011a, 0x00cd, 0x00ce, 0x010e, // c8
   0x0110, 0x0143, 0x0147, 0x00d3, 0x00d4, 0x0150, 0x00d6, 0x00d7, // d0
   0x0158, 0x016e, 0x00da, 0x0170, 0x00dc, 0x00dd, 0x0162, 0x00df, // d8
   0x0155, 0x00e1, 0x00e2, 0x0103, 0x00e4, 0x013a, 0x0107, 0x00e7, // e0
   0x010d, 0x00e9, 0x0119, 0x00eb, 0x011b, 0x00ed, 0x00ee, 0x010f, // e8
   0x0111, 0x0144, 0x0148, 0x00f3, 0x00f4, 0x0151, 0x00f6, 0x00f7, // f0
   0x0159, 0x016f, 0x00fa, 0x0171, 0x00fc, 0x00fd, 0x0163, 0x02d9, // f8
};

static const unsigned short ISO_8859_3[0x60] = {
   0x00a0, 0x0126, 0x02d8, 0x00a3, 0x00a4, 0x0000, 0x0124, 0x00a7, // a0
   0x00a8, 0x0130, 0x015e, 0x011e, 0x0134, 0x00ad, 0x0000, 0x017b, // a8
   0x00b0, 0x0127, 0x00b2, 0x00b3, 0x00b4, 0x00b5, 0x0125, 0x00b7, // b0
   0x00b8, 0x0131, 0x015f, 0x011f, 0x0135, 0x00bd, 0x0000, 0x017c, // b8
   0x00c0, 0x00c1, 0x00c2, 0x0000, 0x00c4, 0x010a, 0x0108, 0x00c7, // c0
   0x00c8, 0x00c9, 0x00ca, 0x00cb, 0x00cc, 0x00cd, 0x00ce, 0x00cf, // c8
   0x0000, 0x00d1, 0x00d2, 0x00d3, 0x00d4, 0x0120, 0x00d6, 0x00d7, // d0
   0x011c, 0x00d9, 0x00da, 0x00db, 0x00dc, 0x016c, 0x015c, 0x00df, // d8
   0x00e0, 0x00e1, 0x00e2, 0x0000, 0x00e4, 0x010b, 0x0109, 0x00e7, // e0
   0x00e8, 0x00e9, 0x00ea, 0x00eb, 0x00ec, 0x00ed, 0x00ee, 0x00ef, // e8
   0x0000, 0x00f1, 0x00f2, 0x00f3, 0x00f4, 0x0121, 0x00f6, 0x00f7, // f0
   0x011d, 0x00f9, 0x00fa, 0x00fb, 0x00fc, 0x016d, 0x015d, 0x02d9, // f8
};


Translator::Translator()
{//=====================


	// It seems that the wctype functions don't work until the locale has been set
	// to something other than the default "C".  Then, not only Latin1 but also the
	// other characters give the correct results with iswalpha() etc.
#ifdef PLATFORM_RISCOS
	static char *locale = "ISO8859-1";
#else
	static char *locale = "german";
#endif
   setlocale(LC_CTYPE,locale);


	charset_a0 = ISO_8859_1;   // this is for when the input is not utf8
	dictionary_name[0] = 0;
	dict_condition=0;
	ungot_char = 0;


	// only need lower case
	memset(letter_bits,0,sizeof(letter_bits));

	// 0-5 sets of characters matched by A B C E F G in pronunciation rules
	// these may be set differently for different languages
	SetLetterBits(0,"aeiou");  // A  vowels, except y
	SetLetterBits(1,"bcdfgjklmnpqstvxz");      // B  hard consonants, excluding h,r,w
	SetLetterBits(2,"bcdfghjklmnpqrstvwxz");  // C  all consonants

	SetLetterBits(7,"aeiouy");  // vowels, including y

	option_stress_rule = 2;
	option_vowel_pause = 1;
   option_unstressed_wd1 = 1;
   option_unstressed_wd2 = 3;

}


Translator_Esperanto::Translator_Esperanto()
{//=========================================
	// set options for Esperanto pronunciation

	// use stress=2 for unstressed words
	static int stress_lengths2[8] = {160, 160,  180, 160,  220, 240,  320, 320};

	charset_a0 = ISO_8859_3;

	option_stress_rule = 2;
	option_words = 2;
	option_vowel_pause = 1;
	option_unstressed_wd1 = 2;
	option_unstressed_wd2 = 2;
	memcpy(stress_lengths,stress_lengths2,sizeof(stress_lengths));
}


Translator::~Translator(void)
{//==========================
}


int lookupwchar(const short *list,int c)
{//====================================
// Is the character c in the list ?
	int ix;

	for(ix=0; list[ix] != 0; ix++)
	{
		if(list[ix] == c)
			return(ix+1);
	}
	return(0);
}

int IsBracket(int c)
{//=================
	if((c >= 0x2014) && (c <= 0x201f))
		return(1);
	return(lookupwchar(brackets,c));
}


static void GetC_unget(int c)
{//==========================
	if(f_input != NULL)
		ungetc(c,f_input);
	else
	{
		p_input--;
		*p_input = c;
	}
}

static int Eof(void)
{//=================
	if(f_input != 0)
		return(feof(f_input));

	return(end_of_input);
}

static int Pos(void)
{//=================
// return the current index into the input text

	if(f_input != 0)
		return(ftell(f_input));

	return(p_input - translator->input_start);
}



static int GetC_get(void)
{//======================
	if(f_input != NULL)
	{
		return(fgetc(f_input) & 0xff);
	}

	if(*p_input == 0)
	{
		end_of_input = 1;
		return(0);
	}

	if(!end_of_input)
	{
		return(*p_input++ & 0xff);
	}
	return(0);
}


static int utf8_out(unsigned int c, char *buf)
{//===========================================
// write a unicode character into a buffer as utf8
// returns the number of bytes written
	int n_bytes;
	int j;
	int shift;
	static char code[4] = {0,0xc0,0xe0,0xf0};

	if(c < 0x80)
	{
		buf[0] = c;
		return(1);
	}
	if(c >= 0x110000)
	{
		buf[0] = ' ';      // out of range character code
		return(1);
	}
	if(c < 0x0800)
		n_bytes = 1;
	else
	if(c < 0x10000)
		n_bytes = 2;
	else
		n_bytes = 3;

	shift = 6*n_bytes;
	buf[0] = code[n_bytes] | (c >> shift);
	for(j=0; j<n_bytes; j++)
	{
		shift -= 6;
		buf[j+1] = 0x80 + ((c >> shift) & 0x3f);
	}
	return(n_bytes+1);
}  // end of utf8_out


int utf8_in(int *c, char *buf, int backwards)
{//==========================================
	int c1;
	int n_bytes;
	int ix;
	static const unsigned char mask[4] = {0xff,0x1f,0x0f,0x07};

	// find the start of the next/previous character
	while((*buf & 0xc0) == 0x80)
	{
		// skip over non-initial bytes of a multi-byte utf8 character
		if(backwards)
			buf--;
		else
			buf++;
	}

	n_bytes = 0;

	if((c1 = *buf++) & 0x80)
	{
		if((c1 & 0xe0) == 0xc0)
			n_bytes = 1;
		else
		if((c1 & 0xf0) == 0xe0)
			n_bytes = 2;
		else
		if((c1 & 0xf8) == 0xf0)
			n_bytes = 3;

		c1 &= mask[n_bytes];
		for(ix=0; ix<n_bytes; ix++)
		{
			c1 = (c1 << 6) + (*buf++ & 0x3f);
		}
	}
	*c = c1;
	return(n_bytes+1);
}




static int GetC(void)
{//==================
// Returns a unicode wide character
// Performs UTF8 checking and conversion

	int c;
	int c1;
	int c2;
	int n_bytes;
	unsigned char m;
	static const unsigned char mask[4] = {0xff,0x1f,0x0f,0x07};
	static const unsigned char mask2[4] = {0,0x80,0x20,0x30};

	if((c1 = ungot_char) != 0)
	{
		ungot_char = 0;
		return(c1);
	}

	c1 = GetC_get();

	if((option_utf8) && (c1 & 0x80))
	{
		// multi-byte utf8 encoding, convert to unicode
		n_bytes = 0;

		if(((c1 & 0xe0) == 0xc0) && ((c1 & 0x1e) != 0))
			n_bytes = 1;
		else
		if(((c1 & 0xf0) == 0xe0) && ((c1 & 0x0f) != 0))
			n_bytes = 2;
		else
		if(((c1 & 0xf8) == 0xf0) && ((c1 & 0x0f) != 0))
			n_bytes = 3;

		if(n_bytes > 0)
		{
			c = c1 & mask[n_bytes];
			m = mask2[n_bytes];
			while(n_bytes > 0)
			{
				c2 = GetC_get();
				if(((c2 & 0xc0) != 0x80) && ((c2 & m) != 0))
				{
					GetC_unget(c2);
					break;
				}
				m = 0x80;
				c = (c << 6) + (c2 & 0x3f);
				n_bytes--;
			}
			if(n_bytes == 0)
				return(c);
		}
		// top-bit-set character is not utf8, drop through to 8bit charset case
		if((option_utf8==2) && !Eof())
			option_utf8=0;   // change "auto" option to "no"
	}

	// 8 bit character set, convert to unicode if
	if(c1 >= 0xa0)
		return(charset_a0[c1-0xa0]);
	return(c1);
}  // end of GetC


static void UngetC(int c)
{//======================
	ungot_char = c;
}


char *strchr_w(const char *s, int c)
{//=================================
// return NULL for any non-ascii character
	if(c >= 0x80)
		return(NULL);
	return(strchr(s,c));
}


void LoadConfig(void)
{//==================
// Load configuration file, if one exists
	char buf[120];
	FILE *f;
	int ix;
	char c1;
	char *p;
	char string[120];

	sprintf(buf,"%s%c%s",path_home,PATHSEP,"config");
	if((f = fopen(buf,"r"))==NULL)
	{
		return;
	}

	while(fgets(buf,sizeof(buf),f)!=NULL)
	{
		if(memcmp(buf,"soundicon",9)==0)
		{
			ix = sscanf(&buf[10],"_%c %s",&c1,string);
			if(ix==2)
			{
				soundicon_tab[n_soundicon_tab].name = c1;
				p = Alloc(strlen(string+1));
				strcpy(p,string);
				soundicon_tab[n_soundicon_tab].filename = p;
				soundicon_tab[n_soundicon_tab++].length = 0;
			}
		}
	}
}  //  end of LoadConfig


const char *Translator::LookupCharName(int c)
{//==========================================
// Find the phoneme string (in ascii) to speak the name of character c

	int ix;
	unsigned int flags;
	static char buf[60];
	char phonemes[55];
	char phonemes2[55];

	buf[0] = '_';
	ix = utf8_out(c,&buf[1]);
	buf[1+ix]=0;

	if(LookupDictList(buf,phonemes,&flags,0))
	{
		SetWordStress(phonemes,flags,-1,0);
		DecodePhonemes(phonemes,phonemes2);
		sprintf(buf,"[[%s]] ",phonemes2);
	}
	else
		sprintf(buf,"char%d ",c);
	return(buf);
}


int LoadSoundFile(const char *fname, int index)
{//===============================================
	FILE *f;
	char *p;
	int *ip;
	int  length;
	char fname_temp[80];
	char fname2[80];
	char command[150];

	if(fname == NULL)
		fname = soundicon_tab[index].filename;

	if(fname==NULL)
		return(1);
	if(fname[0] != '/')
	{
		// a relative path, look in espeak-data/soundicons
		sprintf(fname2,"%s%csoundicons%c%s",path_home,PATHSEP,PATHSEP,fname);
		fname = fname2;
	}
	sprintf(fname_temp,"%s.wav",tmpnam(NULL));
	sprintf(command,"sox \"%s\" -r %d -w %s polyphase\n",fname,samplerate,fname_temp);
	if(system(command) < 0)
	{
		fprintf(stderr,"Failed to resample: %s\n",command);
		return(2);
	}

	length = GetFileLength(fname_temp);
	f = fopen(fname_temp,"rb");
	if(f == NULL)
	{
		fprintf(stderr,"Can't read temp file: %s",fname_temp);
		return(3);
	}

	fseek(f,0,SEEK_SET);
	p = Alloc(length);
	fread(p,length,1,f);
	fclose(f);

	ip = (int *)(&p[40]);
	soundicon_tab[index].length = (*ip) / 2;  // length in samples
	soundicon_tab[index].data = p+44;  // skip WAV header
	return(0);
}



int LookupSoundicon(int c)
{//========================
// Find the sound icon number for a punctuation chatacter
	int ix;

	for(ix=0; ix<n_soundicon_tab; ix++)
	{
		if(soundicon_tab[ix].name == c)
		{
			if(soundicon_tab[ix].length == 0)
			{
				if(LoadSoundFile(NULL,ix)!=0)
					return(-1);  // sound file is not available
			}
			return(ix);
		}
	}
	return(-1);
}


int Translator::AnnouncePunctuation(int c1, int c2, char *buf, int bufix)
{//======================================================================
	// announce punctuation names
	// c1:  the punctuation character
	// c2:  the following character

	int punct_count;
	const char *punctname;
	int found = 0;
	int soundicon;

	static const char *tone_punct_on = "\001+50R\001+15T";  // add reverberation, reduce low frequencies
	static const char *tone_punct_off = "\001R\001T";

	if((soundicon = LookupSoundicon(c1)) >= 0)
	{
		// add an embedded command to play the soundicon
		sprintf(&buf[bufix],"\001%dI ",soundicon);
		UngetC(c2);
		found = 1;
	}
	else
	if((punctname = LookupCharName(c1)) != NULL)
	{
		found = 1;
		if(bufix==0)
		{
			punct_count=1;
			while(c2 == c1)
			{
				punct_count++;
				c2 = GetC();
			}
			UngetC(c2);
			if(punct_count==1)
				sprintf(buf,"%s %s %s",tone_punct_on,punctname,tone_punct_off);
			else
			if(punct_count < 4)
			{
				sprintf(buf,"\001+10S%s",tone_punct_on);
				while(punct_count-- > 0)
					sprintf(buf,"%s %s",buf,punctname);
				sprintf(buf,"%s %s\001S",buf,tone_punct_off);
			}
			else
				sprintf(buf,"%s %s %d %s %s [[______]]",
						tone_punct_on,punctname,punct_count,punctname,tone_punct_off);
		}
		else
		{
			// end the clause now and pick up the punctuation next time
			UngetC(c2);
			ungot_char2 = c1;
			buf[bufix] = ' ';
			buf[bufix+1] = 0;
		}
	}

	if(found == 0)
		return(-1);

	if(c1 == '-')
		return(CLAUSE_NONE);   // no pause
	if((strchr_w(punct_close,c1) != NULL) && !iswalnum(c2))
		return(CLAUSE_COLON);
	if(iswspace(c2) && strchr_w(punct_stop,c1)!=NULL)
		return(punct_attributes[lookupwchar(punct_chars,c1)]);

	return(CLAUSE_COMMA);
}  //  end of AnnouncePunctuation


int Translator::ReadClause(FILE *f_in, char *buf, int n_buf)
{//=========================================================
/* Find the end of the current clause.
	Write the clause into  buf

	returns: clause type (bits 0-7: pause x10mS, bits 8-11 intonation type)

	Also checks for blank line (paragraph) as end-of-clause indicator.

	Does not end clause for:
		punctuation immediately followed by alphanumeric  eg.  1.23  !Speak  :path
		repeated punctuation, eg.   ...   !!!
*/
	int c1;  // current character
	int c2;  // next character
	int parag;
	int ix = 0;
	int j;
	int nl_count;
	int linelength = 0;
	int phoneme_mode = 0;
	int terminator;
	int punct;

	clause_upper_count = 0;
	clause_lower_count = 0;

f_input = f_in;  // for GetC etc

	if(ungot_char2 != 0)
	{
		c2 = ungot_char2;
		ungot_char2 = 0;
	}
	else
	{
		c2 = GetC();
	}

	while(!Eof())
	{
		c1 = c2;
		c2 = GetC();
		if((c2=='\n') && (option_linelength == -1))
		{
			// single-line mode, return immediately on NL
			if((punct = lookupwchar(punct_chars,c1)) == 0)
			{
				buf[ix++] = c1;
				terminator = CLAUSE_PERIOD;  // line doesn't end in punctuation, assume period
			}
			else
			{
				terminator = punct_attributes[punct];
			}
			buf[ix] = ' ';
			buf[ix] = 0;
			return(terminator);
		}

		if(Eof())
		{
			c2 = ' ';
		}

		if((c1 == CTRL_EMBEDDED) || (c1 == ctrl_embedded))
		{
			// an embedded command. If it's a voice change, end the clause
			if(c2 == 'V')
			{
				buf[ix++] = 0;      // end the clause at this point
				while(!iswspace(c1 = GetC()) && !Eof() && (ix < (n_buf-1)))
					buf[ix++] = c1;  // add voice name to end of buffer, after the text
				buf[ix++] = 0;
				return(CLAUSE_VOICE);
			}
			else
			if(c2 == 'B')
			{
				// set the punctuation option from an embedded command
				//  B0     B1     B<punct list><space>
				strcpy(&buf[ix],"   ");
				ix += 3;

				if((c2 = GetC()) == '0')
					option_punctuation = 0;
				else
				{
					option_punctuation = 1;
					option_punctlist[0] = 0;
					if(c2 != '1')
					{
						// a list of punctuation characters to be spoken, terminated by space
						j = 0;
						while(!iswspace(c2) && !Eof())
						{
							option_punctlist[j++] = c2;
							c2 = GetC();
							buf[ix++] = ' ';
						}
						option_punctlist[j] = 0;  // terminate punctuation list
					}
				}
				c2 = GetC();
				continue;
			}
		}

		if(iswupper(c1))
			clause_upper_count++;
		if(iswlower(c1))
			clause_lower_count++;

		if((c1 == '[') && (c2 == '['))
			phoneme_mode = 1;         // input is phoneme mnemonics, so don't look for punctuation
		else
		if((c1 == ']') && (c2 == ']'))
			phoneme_mode = 0;
		else
		if(c1 == '\n')
		{
			parag = 0;

			// count consecutive newlines, ignoring other spaces
			while(!Eof() && iswspace(c2))
			{
				if(c2 == '\n')
					parag++;
				c2 = GetC();
			}
			if(parag > 0)
			{
				// 2nd newline, assume paragraph
				UngetC(c2);

				buf[ix] = ' ';
				buf[ix+1] = 0;
				if(parag > 3)
					parag = 3;
				return((CLAUSE_PARAGRAPH-30) + 30*parag);  // several blank lines, longer pause
			}

			if(linelength < option_linelength)
			{
				// treat lines shorter than a specified length as end-of-clause
				UngetC(c2);
				buf[ix] = ' ';
				buf[ix] = 0;
				return(CLAUSE_COLON);
			}

			linelength = 0;
		}

		if(option_punctuation && (phoneme_mode==0) && iswpunct(c1))
		{
			// option is set to explicitly speak punctuation characters
			// if a list of allowed punctuation has been set up, check whether the character is in it
			if((option_punctlist[0] == 0) || (strchr_w(option_punctlist,c1) != NULL))
			{
				if((terminator = AnnouncePunctuation(c1, c2, buf, ix)) >= 0)
					return(terminator);
			}
		}

		if((phoneme_mode==0) && ((punct = lookupwchar(punct_chars,c1)) != 0) &&
			(iswspace(c2) || IsBracket(c2) || (c2=='?') || (c2=='-') || Eof()))
		{
			// note: (c2='?') is for when a smart-quote has been replaced by '?'
			buf[ix] = ' ';
			buf[ix+1] = 0;

			nl_count = 0;
			while(!Eof() && iswspace(c2))
			{
				if(c2 == '\n')
					nl_count++;
				c2 = GetC();   // skip past space(s)
			}
			UngetC(c2);

			if((nl_count==0) && (c1 == '.') && (iswlower(c2)))
			{
				c2 = ' ';
				continue;  // next word has no capital letter, this dot is probably from an abbreviation
			}
			if(nl_count > 1)
				return(CLAUSE_PARAGRAPH);
			return(punct_attributes[punct]);   // only recognise punctuation if followed by a blank or bracket/quote
		}

		ix += utf8_out(c1,&buf[ix]);    //	buf[ix++] = c1;

		if(!iswalnum(c1) && (ix > (n_buf-20)))
		{
			// clause too long, getting near end of buffer, so break here
			buf[ix] = ' ';
			buf[ix+1] = 0;
			UngetC(c2);
			return(CLAUSE_NONE);
		}
		if(ix >= (n_buf-2))
		{
			// reached end of buffer, must break now
			buf[n_buf-2] = ' ';
			buf[n_buf-1] = 0;
			UngetC(c2);
			return(CLAUSE_NONE);
		}
	}
	buf[ix] = ' ';
	buf[ix+1] = 0;
	return(CLAUSE_EOF);   //  end of file
}  //  end of ReadClause



void Translator::MakePhonemeList(int post_pause, int embedded)
{//===========================================================
// embedded: there's an embedded command flag left over which needs to be added to the phoneme list

	int  ix=0;
	int  j;
	int  k;
	int  insert_ph = 0;
	PHONEME_LIST *phlist;
	PHONEME_LIST2 *plist2;
	PHONEME_TAB *ph;
	PHONEME_TAB *next, *next2;
	int unstress_count = 0;
	int word_has_stress = 0;
	int max_stress;
	int word_end;
	int ph_new;

	phlist = phoneme_list;

	// is the last word of the clause unstressed ?
	max_stress = 0;
	for(j=n_ph_list2-1; j>=0; j--)
	{
		if((ph_list2[j].stress & 0x7f) > max_stress)
			max_stress = ph_list2[j].stress & 0x7f;
		if(ph_list2[j].sourceix != 0)
			break;
	}
	if(max_stress < 4)
	{
		// the last word is unstressed, look for a previous word that can be stressed
		while(--j >= 0)
		{
			if(ph_list2[j].stress & 0x80)  // dictionary flags indicated that this stress can be promoted
			{
				ph_list2[j].stress = 4;   // promote to stressed
				break;
			}
			if((ph_list2[j].stress & 0x7f) >= 4)
			{
				// found a stressed syllable, so stop looking
				break;
			}
		}
	}

	// transfer all the phonemes of the clause into phoneme_list
	for(j=0; (j<n_ph_list2) && (ix < N_PHONEME_LIST-3); j++)
	{
		if(insert_ph != 0)
		{
			// we have a (linking) phoneme which we need to insert here
			j--;
			ph = &phoneme_tab[insert_ph];
			insert_ph = 0;
		}
		else
		{
			// otherwise get the next phoneme from the list
			plist2 = &ph_list2[j];
			ph = &phoneme_tab[plist2->phcode];
		}
		next = &phoneme_tab[(plist2+1)->phcode];      // the phoneme after this one

		word_end = 0;
		if((plist2+1)->sourceix || next->type == phPAUSE)
			word_end = 1;        // this phoneme is the end of a word

		// check whether a Voice has specified that we should replace this phoneme
		ph_new = 1;
		for(k=0; k<n_replace_phonemes; k++)
		{
			if(ph->code == replace_phonemes[k].old_ph)
			{
				if((replace_phonemes[k].type == 1) && (word_end == 0))
					continue;     // this replacement only occurs at the end of a word

				// substitute the replacement phoneme
				ph = &phoneme_tab[ph_new = replace_phonemes[k].new_ph];
			}
		}

		if(plist2->sourceix)
			word_has_stress = 0;   // start of a word

		if(ph_new == 0)
			continue;   // phoneme has been replaced by NULL

		if(ph->type == phVOWEL)
		{
			// check for consecutive unstressed syllables
			if(plist2->stress == 0)
			{
				// an unstressed vowel
				unstress_count++;
				if((unstress_count > 1) && ((unstress_count & 1)==0))
				{
					// in a sequence of unstressed syllables, reduce alternate syllables to 'diminished'
               // stress.  But not for the last phoneme of a stressed word
					if((word_has_stress) && ((plist2+1)->sourceix!=0))
					{
						// An unstressed final vowel of a stressed word
						unstress_count=1;    // try again for next syllable
					}
					else
					{
						plist2->stress = 1;    // change stress to 'diminished'
					}
				}
			}
			else
			{
				unstress_count = 0;
				if(plist2->stress > 3)
					word_has_stress = 1;   // word has a primary or a secondary stress
			}
		}

		if(ph->vowel_follows > 0)
		{
			// This phoneme changes if vowel follows, or doesn't follow, depending on its phNOTFOLLOWS flag
			if(ph->flags & phNOTFOLLOWS)
			{
				if(next->type != phVOWEL)
					ph = &phoneme_tab[ph->vowel_follows];
			}
			else
			if(ph->flags & phORPAUSEFOLLOWS)
			{
				if((next->type == phVOWEL) || (next->type == phPAUSE))
					ph = &phoneme_tab[ph->vowel_follows];
			}
			else
			{
				if(next->type == phVOWEL)
					ph = &phoneme_tab[ph->vowel_follows];
			}
		}

		if((ph->link_out != 0) && (option_words < 3) && (((plist2+1)->synthflags & SFLAG_EMBEDDED)==0))
		{
			// This phoneme can be linked to a following vowel by inserting a linking phoneme
			if(next->type == phVOWEL)
				insert_ph = ph->link_out;
			else
			if((next->type == phPAUSE) && !(next->flags & phNOLINK))
			{
				// Pause followed by Vowel, replace the Pause with the linking phoneme, unless
				// the Pause phoneme has the phNOLINK flag
				next2 = &phoneme_tab[(plist2+2)->phcode];
				if(next2->type == phVOWEL)
					(plist2+1)->phcode = ph->link_out;  // replace pause by linking phoneme
			}
		}

		if(ph->flags & phVOICED)
		{
			// check that a voiced consonant is preceded or followed by a vowel or liquid
			// and if not, add a short schwa

			// not yet implemented
		}

		phlist[ix].ph = ph;
		phlist[ix].type = ph->type;
		phlist[ix].env = PITCHfall;          // default, can be changed in the "intonation" module
		phlist[ix].synthflags = plist2->synthflags;
		phlist[ix].tone = plist2->stress & 0xf;
		phlist[ix].tone_ph = plist2->tone_number;
		phlist[ix].sourceix = 0;

		if(plist2->sourceix > 0)
		{
			phlist[ix].sourceix = plist2->sourceix;
			phlist[ix].newword = 1;     // this phoneme is the start of a word
		}
		else
			phlist[ix].newword = 0;
		phlist[ix].length = ph->std_length;

		if(ph->type==phVOWEL || ph->type==phLIQUID || ph->type==phNASAL || ph->type==phVSTOP || ph->type==phVFRICATIVE)
		{
			phlist[ix].length = 128;  // length_mod
			phlist[ix].env = PITCHfall;
		}

		phlist[ix].prepause = 0;
		phlist[ix].amp = 20;          // default, will be changed later
		phlist[ix].pitch1 = 0x400;
		phlist[ix].pitch2 = 0x400;
		ix++;
	}
	phlist[ix].newword = 2;     // end of clause
   phlist[ix].type = phPAUSE;  // terminate with 2 Pause phonemes
	phlist[ix].length = post_pause;  // length of the pause, depends on the punctuation
	phlist[ix].sourceix=0;
	phlist[ix].synthflags = 0;
	if(embedded)
	{
		phlist[ix].synthflags = SFLAG_EMBEDDED;
		embedded_list[embedded_ix++] = 0;
	}
   phlist[ix++].ph = &phoneme_tab[phonPAUSE];
   phlist[ix].type = phPAUSE;
	phlist[ix].length = 0;
	phlist[ix].sourceix=0;
	phlist[ix].synthflags = 0;
   phlist[ix++].ph = &phoneme_tab[phonPAUSE];

	n_phoneme_list = ix;
}  // end of MakePhonemeList




int Translator::TranslateLetter(char *word, char *phonemes)
{//========================================================
// get pronunciation for an isolated letter
// return number of bytes used by the letter
	int n_bytes;
	int letter;

	static char single_letter[6] = {0,' ',' ',' ','9',0};
	static char stress_2[2] = {phonSTRESS_2,0};

	n_bytes = utf8_in(&letter,word,0);

	memcpy(&single_letter[2],word,n_bytes);
	strcpy(&single_letter[2+n_bytes]," 9");
	strcat(phonemes,stress_2);
	TranslateRules(&single_letter[2], phonemes, NULL,0);
	return(n_bytes);
}



int Translator::TranslateWord(char *word1, int next_pause, int wflags)
{//===================================================================
// word1 is terminated by space (0x20) character

	int length;
	int word_length;
	int posn;
	unsigned int dictionary_flags=0;
	unsigned int dictionary_flags2=0;
	int end_type=0;
	char *word;
	char phonemes[N_WORD_PHONEMES];
	char *phonemes_ptr;
	char prefix_phonemes[N_WORD_PHONEMES];
	char end_phonemes[N_WORD_PHONEMES];
	int found;
   int end_flags;
	char c_temp;   // save a character byte while we temporarily replace it with space
	int last_char = 0;
	int unpron_length;
	int add_plural_suffix = 0;
	int prefix_flags = 0;

	// translate these to get pronunciations of plural 's' suffix (different forms depending on
	// the preceding letter
	static char word_zz[4] = {0,'z','z',0};
	static char word_iz[4] = {0,'i','z',0};
	static char word_ss[4] = {0,'s','s',0};

	if(option_log_trans)
	{
		// write a log file of the pronunciation translation of just this one word
		// in contrast, the "speak" program sets f_trans = stdout
		char buf[80];
		sprintf(buf,"%s/log_trans.txt",getenv("HOME"));
		f_trans = fopen(buf,"w");
	}

	word = word1;
	prefix_phonemes[0] = 0;

	// count the length of the word
	for(word_length=0; word[word_length]!=0 && (word[word_length] != ' '); word_length++);
	if(word_length > 0)
	{
		utf8_in(&last_char,&word[word_length-1],1);
	}

	// try an initial lookup in the dictionary list, we may find a pronunciation specified, or
	// we may just find some flags
	found = LookupDictList(word,phonemes,&dictionary_flags,wflags << 16);

	if((wflags & FLAG_ALL_UPPER) && (clause_upper_count <= clause_lower_count) &&
		 !(dictionary_flags & FLAG_ABBREV) && (word_length>1) && (word_length<4) && iswalpha(word1[0]))
	{
		// An upper case word in a lower case clause. This could be an abbreviation.
		// Speak as individual letters
		word = word1;
		posn = 0;
		phonemes[0] = 0;
		end_type = 0;

		while(*word != ' ')
		{
			word += TranslateLetter(word, phonemes);
			if(++posn < 5)
			{
					/* stress the last vowel (assume one syllable each - not "w") */
					dictionary_flags = posn;
			}
		}
	}
	else
	if(found == 0)
	{
		// word's pronunciation is not given in the dictionary list, although
		// dictionary_flags may have ben set there

		posn = 0;
		length = 999;
		while(((length < 3) && (length > 0))|| (word_length > 1 && Unpronouncable(word)))
		{
			// This word looks "unpronouncable", so speak letters individually until we
			// find a remainder that we can pronounce.
			word += TranslateLetter(word,phonemes);
			if(++posn < 5)
			{
					/* stress last vowel (assume one syllable each - not "w") */
					dictionary_flags = posn;
			}
			if(memcmp(word,"'s ",3) == 0)
			{
				// remove a 's suffix and pronounce this separately (not as an individual letter)
				add_plural_suffix = 1;
				word[0] = ' ';
				word[1] = ' ';
				break;
			}

			length=0;
			while(word[length] != ' ') length++;
			word[-1] = ' ';            // prevent this affecting the pronunciation of the pronuncable part
		}

		// anything left ?
		if(*word != ' ')
		{
			// Translate the stem
			unpron_length = strlen(phonemes);
			end_type = TranslateRules(word, phonemes, end_phonemes,0);


			c_temp = word[-1];

			found = 0;
			while(end_type & SUFX_P)
			{
				// Found a standard prefix, remove it and retranslate
				strcat(prefix_phonemes,end_phonemes);

				word += (end_type & 0xf);
				c_temp = word[-1];
				word[-1] = ' ';

				end_type = 0;
				found = LookupDictList(word,phonemes,&dictionary_flags2,SUFX_P | (wflags << 16));
				if(dictionary_flags==0)
					dictionary_flags = dictionary_flags2;
				else
					prefix_flags = 1;
				if(found == 0)
				{
					end_type = TranslateRules(word, phonemes, end_phonemes,0);
				}
			}

			if((end_type != 0) && !(end_type & SUFX_P))
			{
				// The word has a standard ending, re-translate without this ending
				end_flags = RemoveEnding(word,end_type);

				phonemes_ptr = &phonemes[unpron_length];
				phonemes_ptr[0] = 0;

				if(prefix_phonemes[0] != 0)
				{
					// lookup the stem without the prefix removed
					word[-1] = c_temp;
					found = LookupDictList(word1,phonemes_ptr,&dictionary_flags2,end_flags | (wflags << 16));
					word[-1] = ' ';
					if(dictionary_flags==0)
						dictionary_flags = dictionary_flags2;
					if(found)
						prefix_phonemes[0] = 0;  // matched whole word, don't need prefix now
					if(found || (dictionary_flags2 != 0))
						prefix_flags = 1;
				}
				if(found == 0)
				{
					found = LookupDictList(word,phonemes_ptr,&dictionary_flags2,end_flags | (wflags << 16));
					if(dictionary_flags==0)
						dictionary_flags = dictionary_flags2;
				}
				if(found == 0)
				{
					TranslateRules(word, phonemes, NULL,end_flags);
				}

				AppendPhonemes(phonemes,end_phonemes);
			}
			word[-1] = c_temp;
		}
	}

	if((add_plural_suffix) || (wflags & FLAG_HAS_PLURAL))
	{
		// s or 's suffix, append [s], [z] or [Iz] depending on previous letter
		if(last_char == 'f')
			TranslateRules(&word_ss[1],phonemes,NULL,0);
		else
		if(strchr_w("hsx",last_char)==NULL)
			TranslateRules(&word_zz[1],phonemes,NULL,0);
		else
			TranslateRules(&word_iz[1],phonemes,NULL,0);
	}


	/* determine stress pattern for this word */
	/******************************************/
	/* NOTE: this also adds a single PAUSE if the previous word ended
				in a primary stress, and this one starts with one */
	if(prefix_flags || (strchr(prefix_phonemes,phonSTRESS_P)!=NULL))
	{
		// stress position affects the whole word, including prefix
		strcpy(word_phonemes,prefix_phonemes);
		strcat(word_phonemes,phonemes);
		SetWordStress(word_phonemes,dictionary_flags,-1,prev_last_stress);
	}
	else
	{
		if(prefix_phonemes[0] == 0)
			SetWordStress(phonemes,dictionary_flags,-1,prev_last_stress);
		else
			SetWordStress(phonemes,dictionary_flags,-1,0);
		strcpy(word_phonemes,prefix_phonemes);
		strcat(word_phonemes,phonemes);
	}

//	if(next_pause > 2)
	if(wflags & FLAG_LAST_WORD)
	{
		if(dictionary_flags & (FLAG_STRESS_END | FLAG_STRESS_END2))
			SetWordStress(word_phonemes,0,4,prev_last_stress);
		else
		if(dictionary_flags & FLAG_UNSTRESS_END)
			SetWordStress(word_phonemes,0,3,prev_last_stress);
	}
	if(wflags & FLAG_STRESSED_WORD)
	{
		// A word is indicated in the source text as stressed

		// we need to improve the intonation module to deal better with a clauses tonic
		// stress being early in the clause, before enabling this
		//SetWordStress(word_phonemes,0,5,prev_last_stress);
	}

	// dictionary flags for this word give a clue about which alternative pronunciations of
	// following words to use.
	if(end_type & SUFX_F)
	{
		// expect a verb form, with or without -s suffix
		expect_verb = 2;
		expect_verb_s = 2;
	}

	if(dictionary_flags & FLAG_PASTF)
	{
		/* expect perfect tense in next two words */
		expect_past = 3;
		expect_verb = 0;
	}
	else
	if(dictionary_flags & FLAG_VERBF)
	{
		/* expect a verb in the next word */
		expect_verb = 2;
		expect_verb_s = 0;   /* verb won't have -s suffix */
	}
	else
	if(dictionary_flags & FLAG_VERBSF)
	{
		// expect a verb, must have a -s suffix
		expect_verb = 0;
		expect_verb_s = 2;
		expect_past = 0;
	}
	else
	if(dictionary_flags & FLAG_NOUNF)
	{
		/* not expecting a verb next */
		expect_verb = 0;
		expect_verb_s = 0;
		expect_past = 0;
	}

	if((word[0] != 0) && (!(dictionary_flags & FLAG_VERB_EXT)))
	{
		if(expect_verb > 0)
			expect_verb -= 1;

		if(expect_verb_s > 0)
			expect_verb_s -= 1;

		if(expect_past > 0)
			expect_past -= 1;
	}

	if((word_length == 1) && iswalpha(word1[0]) && (word1[0] != 'i'))
	{
// English Specific !!!!
		// any single letter before a dot is an abbreviation, except 'I'
		dictionary_flags |= FLAG_DOT;
	}

	if((f_trans != NULL) && (f_trans != stdout))
		fclose(f_trans);
	return(dictionary_flags);
}  //  end of TranslateWord



static void SetPlist2(PHONEME_LIST2 *p, unsigned char phcode)
{//==========================================================
	p->phcode = phcode;
	p->stress = 0;
	p->tone_number = 0;
	p->synthflags = 0;
	p->sourceix = 0;
}


int Translator::TranslateWord2(char *word, int wflags, int pre_pause, int next_pause, int source_ix)
{//=================================================================================================
	int flags=0;
	int stress;
	unsigned char *p;
	int srcix;
	int embedded_flag=0;
	unsigned char ph_code;
	PHONEME_LIST2 *plist2;
	PHONEME_TAB *ph;
	int max_stress;
	int max_stress_ix=0;
	int prev_vowel = -1;
	int pitch_raised = 0;
	char bad_phoneme[4];


	word_flags = wflags;
	if(wflags & FLAG_EMBEDDED)
		embedded_flag = SFLAG_EMBEDDED;

	if(prepause_timeout > 0)
		prepause_timeout--;

	if(wflags & FLAG_FIRST_UPPER)
	{
		if((option_capitals > 2) && (embedded_ix < N_EMBEDDED_LIST-6))
		{
			// indicate capital letter by raising pitch
			if(embedded_flag)
				embedded_ix--;   // already embedded command before this word, remove zero terminator
			embedded_list[embedded_ix++] = EMBED_P+0x80;  // raise pitch
			embedded_list[embedded_ix++] = pitch_raised = option_capitals;
			embedded_list[embedded_ix++] = 0;
			embedded_flag = SFLAG_EMBEDDED;
		}
	}

	if(wflags & FLAG_PHONEMES)
	{
		// The input is in phoneme mnemonics, not language text
		EncodePhonemes(word,word_phonemes,bad_phoneme);
	}
	else
	{
		flags = translator->TranslateWord(word, next_pause, wflags);

		if((flags & FLAG_PREPAUSE) && (prepause_timeout == 0) && !(wflags & FLAG_LAST_WORD))
		{
			if(pre_pause < 2) pre_pause = 2;
			prepause_timeout = 3;
		}
	}
	p = (unsigned char *)translator->word_phonemes;

	plist2 = &ph_list2[n_ph_list2];
	stress = 0;
	srcix = 0;
	max_stress = -1;


	while((pre_pause-- > 0) && (n_ph_list2 < N_PHONEME_LIST-4))
	{
		// add pause phonemes here. Either because of punctuation (brackets or quotes) in the
		// text, or because the word is marked in the dictionary lookup as a conjunction
		SetPlist2(&ph_list2[n_ph_list2++],phonPAUSE);
	}
	if((option_capitals==1) && (wflags & FLAG_FIRST_UPPER))
	{
		SetPlist2(&ph_list2[n_ph_list2++],phonPAUSE_SHORT);
		SetPlist2(&ph_list2[n_ph_list2++],phonCAPITAL);
		if((wflags & FLAG_ALL_UPPER) && iswalpha(word[1]))
		{
			// word > 1 letter and all capitals
			SetPlist2(&ph_list2[n_ph_list2++],phonPAUSE_SHORT);
			SetPlist2(&ph_list2[n_ph_list2++],phonCAPITAL);
		}
	}

	while(((ph_code = *p++) != 0) && (n_ph_list2 < N_PHONEME_LIST-4))
	{
		if(ph_code == 255)
			continue;      // unknown phoneme

		// Add the phonemes to the first stage phoneme list (ph_list2)
		ph = &phoneme_tab[ph_code];
		if(ph->type == phSTRESS)
		{
			// don't add stress phonemes codes to the list, but give their stress
			// value to the next vowel phoneme
			// std_length is used to hold stress number or (if >10) a tone number for a tone language
			if(ph->spect == 0)
				stress = ph->std_length;
			else
			{
				// for tone languages, the tone number for a syllable folows the vowel
				if(prev_vowel >= 0)
					ph_list2[prev_vowel].tone_number = ph_code;
			}
		}
		else
		if(ph_code == phonEND_WORD)
		{
			// a || symbol in a phoneme string was used to indicate a word boundary
			// Don't add this phoneme to the list, but make sure the next phoneme has
			// a newword indication
			srcix = source_ix+1;
		}
		else
		{
			ph_list2[n_ph_list2].phcode = ph_code;
			ph_list2[n_ph_list2].stress = stress;
			ph_list2[n_ph_list2].tone_number = 0;
			ph_list2[n_ph_list2].synthflags = embedded_flag;
			embedded_flag = 0;
			ph_list2[n_ph_list2].sourceix = srcix;
			srcix = 0;

			if(ph->type == phVOWEL)
			{
				prev_vowel = n_ph_list2;

				if(stress > max_stress)
				{
					max_stress = stress;
					max_stress_ix = n_ph_list2;
				}
				stress = 0;
			}
			n_ph_list2++;
		}
	}
	plist2->sourceix = source_ix;

	if(pitch_raised > 0)
	{
		embedded_list[embedded_ix++] = EMBED_P+0xc0;  // lower pitch
		embedded_list[embedded_ix++] = pitch_raised;
		embedded_list[embedded_ix++] = 0;
		SetPlist2(&ph_list2[n_ph_list2],phonPAUSE_SHORT);
		ph_list2[n_ph_list2++].synthflags = SFLAG_EMBEDDED;
	}

	if(flags & FLAG_STRESS_END2)
	{
		// this's word's stress could be increased later
		ph_list2[max_stress_ix].stress |= 0x80;
	}
	return(flags);
}  //  end of TranslateWord2



int Translator::EmbeddedCommand(unsigned int &source_index)
{//========================================================
	// An embedded command to change the pitch, volume, etc.
	// returns number of commands added to embedded_list

	// pitch,speed,amplitude,expression,reverb,tone,voice
	const char *commands = "PSAERTIV";
	int value = -1;
	int sign = 0;
	unsigned char c;
	char *p;
	int cmd;

	if((c = source[source_index]) == '+')
	{
		sign = 0x80;
		source_index++;
	}
	else
	if(c == '-')
	{
		sign = 0xc0;
		source_index++;
	}

	if(isdigit(source[source_index]))
	{
		value = atoi(&source[source_index]);
		while(isdigit(source[source_index]))
			source_index++;
	}

	c = source[source_index++];
	if(embedded_ix >= (N_EMBEDDED_LIST - 2))
		return(0);  // list is full

	if((p = strchr_w(commands,c)) == NULL)
		return(0);
	cmd = (p - commands)+1;
	if(value == -1)
	{
		value = embedded_default[cmd];
		sign = 0;
	}

	embedded_list[embedded_ix++] = cmd + sign;
	embedded_list[embedded_ix++] = value;
	return(1);
}



char *Translator::TranslateClause(FILE *f_text, char *buf, int *tone_out, char **voice_change)
{//===========================================================================================
	int ix;
	int c;
	int cc;
	unsigned int source_index=0;
	unsigned int prev_source_index;
	int prev_in;
	int prev_out;
	int prev_in2=0;
	int next_in;
	int clause_pause;
	int pre_pause=0;
	int pre_pause_add=0;
	int all_upper_case=FLAG_ALL_UPPER;
	int finished;
	int skip_words;
	int single_quoted;
	int phoneme_mode = 0;
	int dict_flags;        // returned from dictionary lookup
	int word_flags;        // set here
	int embedded_count = 0;
	char *word;
	int n_digits;

	WORD_TAB words[N_CLAUSE_WORDS];
	int word_count=0;      // index into words

	char sbuf[512];

	int terminator;
	int tone;

	f_input = f_text;
	p_input = buf;
	end_of_input = 0;
	embedded_ix = 0;

	clause_start_index = Pos()-1;
	terminator = translator->ReadClause(f_text,source,sizeof(source));
	clause_pause = 300;  // mS
	tone = 0;

	clause_pause = (terminator & 0xff) * 10;  // mS
	tone = (terminator & 0xf00) >> 8;

	ph_list2[0].phcode = phonPAUSE;
   ph_list2[0].stress = 0;
	ph_list2[0].tone_number = 0;
	ph_list2[0].sourceix = 0;
	n_ph_list2 = 1;
	prev_last_stress = 0;
	prepause_timeout = 0;
	word_count = 0;
	single_quoted = 0;
	word_flags = 0;
	expect_verb=0;
	expect_past=0;
	expect_verb_s=0;

	sbuf[0] = 0;
	sbuf[1] = ' ';
	ix = 2;
	prev_in = ' ';

	words[0].start = ix;
	words[0].sourceix = 0;
	words[0].flags = 0;
	finished = 0;

	while(!finished && (ix < (int)sizeof(sbuf))&& (n_ph_list2 < N_PHONEME_LIST-4))
	{
		utf8_in(&prev_out,&sbuf[ix-1],1);   // prev_out = sbuf[ix-1];

		if(prev_in2 != 0)
		{
			prev_in = prev_in2;
			prev_in2 = 0;
		}
		else
		if(source_index > 0)
		{
			utf8_in(&prev_in,&source[source_index-1],1);  //  prev_in = source[source_index-1];
		}
		prev_source_index = source_index;
		source_index += utf8_in(&cc,&source[source_index],0);   // cc = source[source_index++];
		c = cc;
		utf8_in(&next_in,&source[source_index],0);

		if((c == CTRL_EMBEDDED) || (c == ctrl_embedded))
		{
			// start of embedded command in the text
			int srcix = source_index-1;

			if(prev_in != ' ')
			{
				c = ' ';
				prev_in2 = c;
				source_index--;
			}
			else
			{
				embedded_count += EmbeddedCommand(source_index);
				prev_in2 = prev_in;
				// replace the embedded command by spaces
				memset(&source[srcix],' ',source_index-srcix);
				source_index = srcix;
				continue;
			}
		}

		if(phoneme_mode)
		{
			all_upper_case = FLAG_PHONEMES;

			if((c == ']') && (next_in == ']'))
			{
				phoneme_mode = 0;
				source_index++;
				c = ' ';
			}
		}
		else
		{
			if(c == 0x92)
				c = '\'';    // 'microsoft' quote

			if((c == '?') && iswalpha(prev_out) && iswalpha(next_in))
			{
				// ? between two letters may be a smart-quote replaced by ?
				c = '\'';
			}

			if(!iswalpha(c) && !iswspace(c) && (c != '\''))
			{
				if(iswalpha(prev_out))
				{
					c = ' ';   // ensure we have an end-of-word terminator
					source_index = prev_source_index;
				}
			}
			if(iswdigit(prev_out))
			{
				if(!iswdigit(c) && (c != '.') && (c != ','))
				{
					c = ' ';   // terminate digit string with a space
					source_index = prev_source_index;
				}
			}

			if((c == '[') && (next_in == '['))
			{
				phoneme_mode = FLAG_PHONEMES;
				source_index++;
				continue;
			}

			if(c == 0)
			{
				finished = 1;
				c = ' ';
			}
			else
			if(iswalpha(c))
			{
				if(!iswalpha(prev_out))
				{
					if(iswupper(c))
						word_flags |= FLAG_FIRST_UPPER;

					if((prev_out != ' ') && (prev_out != '\''))
					{
						// start of word, insert space if not one there already
						c = ' ';
						source_index = prev_source_index;  // unget
					}
					else
					if((prev_out == ' ') && iswdigit(sbuf[ix-2]) && !iswdigit(prev_in))
					{
						// word, following a number, but with a space between
						// Add an extra space, to distinguish "2 a" from "2a"
						sbuf[ix++] = ' ';
						words[word_count].start++;
					}
				}

				if(iswupper(c))
				{
					c = towlower(c);
					if(iswlower(prev_in))
					{
						c = ' ';      // lower case followed by upper case, treat as new word
						prev_in2 = c;
						source_index = prev_source_index;  // unget
					}
					else
					if((c != ' ') && iswupper(prev_in) && iswlower(next_in) &&
							(memcmp(&source[source_index],"s ",2) != 0))          // ENGLISH specific plural
					{
						c = ' ';      // change from upper to lower case, start new word at the last uppercase
						prev_in2 = c;
						source_index = prev_source_index;  // unget
					}
				}
				else
				{
					if((all_upper_case) && ((ix - words[word_count].start) > 1))
					{
						if((c == 's') && (next_in==' '))
						{
							c = ' ';
							all_upper_case |= FLAG_HAS_PLURAL;

							if(sbuf[ix-1] == '\'')
								sbuf[ix-1] = ' ';
						}
						else
							all_upper_case = 0;  // current word contains lower case letters, not "'s"
					}
					else
						all_upper_case = 0;
				}
			}
			else
			if(c=='-')
			{
				if(iswalpha(prev_in) && iswalpha(next_in))
				{
					// '-' between two letters is a hyphen, treat as a space
					c = ' ';
				}
				else
				if((prev_in==' ') && (next_in==' '))
				{
					// ' - ' dash between two spaces, treat as pause
					c = ' ';
					pre_pause_add = 2;
				}
				else
				if(next_in=='-')
				{
					// double hyphen, treat as pause
					source_index++;
					c = ' ';
					pre_pause_add = 2;
				}
			}
			else
			if(c == '\'')
			{
				if(iswalpha(prev_in) && iswalpha(next_in))
				{
					// between two letters, consider apostrophe as part of the word
					single_quoted = 0;
				}
				else
				{
					if((prev_out == 's') && (single_quoted==0))
					{
						// looks like apostrophe after an 's'
						c = ' ';
					}
					else
					{
						if(iswspace(prev_out))
							single_quoted = 1;
						else
							single_quoted = 0;

						pre_pause_add = 2;   // single quote
						c = ' ';
					}
				}
			}
			else
			if(IsBracket(c))
			{
				pre_pause_add = 2;
				c = ' ';
			}
			else
			if(lookupwchar(breaks,c) != 0)
			{
				c = ' ';  // various characters to treat as space
			}
			else
			if(iswdigit(c))
			{
				if((prev_out != ' ') && !iswdigit(prev_out) && (prev_out != '.'))
				{
					c = ' ';
					source_index = prev_source_index;
				}
				else
				if((prev_out == ' ') && iswalpha(sbuf[ix-2]) && !iswalpha(prev_in))
				{
					// insert extra space between a word and a number, to distinguish 'a 2' from 'a2'
					sbuf[ix++] = ' ';
					words[word_count].start++;
				}
			}
		}

		if(iswspace(c))
		{
			if(prev_out == ' ')
				continue;   // multiple spaces

			// end of 'word'
			sbuf[ix++] = ' ';

			if((ix > words[word_count].start) && (word_count < N_CLAUSE_WORDS-1))
			{
				if(embedded_count > 0)
				{
					// there are embedded commands before this word
					embedded_list[embedded_ix++] = 0;   // terminate list of commands for this word
					words[word_count].flags |= FLAG_EMBEDDED;
					embedded_count = 0;
				}
				words[word_count].pre_pause = pre_pause;
				words[word_count].flags |= (all_upper_case | word_flags);
				word_count++;
				words[word_count].start = ix;
				words[word_count].sourceix = source_index;
				words[word_count].flags = 0;
				word_flags = 0;
				pre_pause = 0;
				all_upper_case = FLAG_ALL_UPPER;
			}
		}
		else
		{
			ix += utf8_out(c,&sbuf[ix]);   // sbuf[ix++] = c;
		}
		if(pre_pause_add > pre_pause)
			pre_pause = pre_pause_add;
		pre_pause_add = 0;
	}

	clause_end = &sbuf[ix-1];
	sbuf[ix] = 0;
	words[0].pre_pause = 0;  // don't add extra pause at beginning of clause
	words[word_count].pre_pause = 4;
	if(word_count > 0)
		words[word_count-1].flags |= FLAG_LAST_WORD;

	for(ix=0; ix<word_count; ix++)
	{
		int j;
		char *pn;
		char *pw;
		char number_buf[80];
		static char string_capital[30];

		if((words[ix].flags & FLAG_FIRST_UPPER) && (option_capitals==2))
		{
			if(words[ix].flags & FLAG_ALL_UPPER)
				strcpy(string_capital,"  (X1)'O:l(X1)k'apIt@L  ");
			else
				strcpy(string_capital,"  (X1)k'apIt@L  ");
			TranslateWord2(&string_capital[2],FLAG_PHONEMES,1,1,words[ix].sourceix + clause_start_index);
		}

		word = pw = &sbuf[words[ix].start];
		for(n_digits=0; iswdigit(word[n_digits]); n_digits++);  // count consecutive digits
		if(n_digits > 4)
		{
			// word is entirely digits, insert commas and break into 3 digit "words"
			number_buf[0] = ' ';
			pn = &number_buf[1];
			j = n_digits;
			while((j > 0) && (pn < &number_buf[sizeof(number_buf)-3]))
			{
				*pn++ = *pw++;
				if((--j > 0) && (j % 3)==0)
				{
					*pn++ = ',';
					*pn++ = ' ';
				}
			}
			pn[0] = ' ';
			pn[1] = 0;
			for(pw = &number_buf[1]; pw < pn;)
			{
				TranslateWord2(pw, words[ix].flags, words[ix].pre_pause,0, words[ix].sourceix + clause_start_index);
				while(*pw++ != ' ');
				words[ix].pre_pause = 0;
				words[ix].flags = 0;
			}
			if(word[n_digits] != ' ')
			{
				TranslateWord2(&word[n_digits], 0, 0, 0, words[ix].sourceix + clause_start_index);
			}
		}
		else
		{
			dict_flags = TranslateWord2(word, words[ix].flags, words[ix].pre_pause,
		 		words[ix+1].pre_pause, words[ix].sourceix + clause_start_index);
			skip_words = (dict_flags >> 29) & 3;
			ix += skip_words;

			if((dict_flags & FLAG_DOT) && (ix == word_count-1) && (terminator == CLAUSE_PERIOD))
			{
				// probably an abbreviation such as Mr. or B. rather than end of sentence
				clause_pause = 30;
				tone = 4;
			}
		}
	}

	for(ix=0; ix<2; ix++)
	{
		// terminate the clause with 2 PAUSE phonemes
		ph_list2[n_ph_list2+ix].phcode = phonPAUSE;
   	ph_list2[n_ph_list2+ix].stress = 0;
		ph_list2[n_ph_list2+ix].sourceix = 0;
		ph_list2[n_ph_list2+ix].synthflags = 0;
	}

	MakePhonemeList(clause_pause,embedded_count);
	GetTranslatedPhonemeString(phon_out,sizeof(phon_out));
	*tone_out = tone;

	if(voice_change != NULL)
	{
		// return new voice name if an embedded voice change command terminated the clause
		if(terminator == CLAUSE_VOICE)
			*voice_change = &source[source_index];
		else
			*voice_change = NULL;
	}

	if(Eof() || (buf==NULL))
		return(NULL);
	return(p_input);
}  //  end of TranslateClause

