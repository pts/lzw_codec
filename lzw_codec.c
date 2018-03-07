#define DUMMY \
set -ex; \
gcc -DNDEBUG -s -O3 -ansi -pedantic \
  -Wall -W -Wstrict-prototypes -Wnested-externs -Winline \
  -Wpointer-arith -Wbad-function-cast -Wcast-qual -Wmissing-prototypes \
  -Wmissing-declarations lzw_codec.c -o lzw_codec; \
exit
/* lzw_codec.c -- a real, effective implementation of PostScript
 *   LanguageLevel2 and PDF /LZWEncode and /LZWDecode filters (same as the LZW
 *   compression used in TIFF raster image files)
 *
 * based on tif_lzw.c in libtiff-v3.4beta037 by Sam Leffler and Silicon Graphics.
 * by pts@fazekas.hu at Sun Dec 30 15:04:19 CET 2001
 * encoding and decoding works at Sun Dec 30 17:05:23 CET 2001
 *
 * Note that the LZW compression (but not uncompression) is patented by
 * Unisys (patent number #4,558,302), so use this program at your own legal
 * risk!
 *
 * Predictors and PostScript LanguageLevel3 filter options are not supported.
 *
 * Test: linux-2.2.8.tar.gz
 * -- uncompressed:              58 388 480 bytes
 * -- LZWEncode (lzw_codec.c):   26 518 397 bytes (uncompression OK)
 * -- FlateEncode (almost gzip): 13 808 890 bytes (uncompression: zcat)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
/* Imp: implement TIFFReverseBits */
/* Imp: check #ifdef _WINDOWS */
/* Dat: a strip is an interval of rows. Each strip -- probably except for the
 *      last -- has a same number of rows. Strips are useful for TIFF writers
 *      to better organize data in memory or disk.
 */

/* Original: /d1/sam/tiff/libtiff/RCS/tif_lzw.c,v 1.73 1997/08/29 21:45:54 sam Exp */

/*
 * Copyright (c) 1988-1997 Sam Leffler
 * Copyright (c) 1991-1997 Silicon Graphics, Inc.
 *
 * Permission to use, copy, modify, distribute, and sell this software and 
 * its documentation for any purpose is hereby granted without fee, provided
 * that (i) the above copyright notices and this permission notice appear in
 * all copies of the software and related documentation, and (ii) the names of
 * Sam Leffler and Silicon Graphics may not be used in any advertising or
 * publicity relating to the software without the specific, prior written
 * permission of Sam Leffler and Silicon Graphics.
 * 
 * THE SOFTWARE IS PROVIDED "AS-IS" AND WITHOUT WARRANTY OF ANY KIND, 
 * EXPRESS, IMPLIED OR OTHERWISE, INCLUDING WITHOUT LIMITATION, ANY 
 * WARRANTY OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.  
 * 
 * IN NO EVENT SHALL SAM LEFFLER OR SILICON GRAPHICS BE LIABLE FOR
 * ANY SPECIAL, INCIDENTAL, INDIRECT OR CONSEQUENTIAL DAMAGES OF ANY KIND,
 * OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
 * WHETHER OR NOT ADVISED OF THE POSSIBILITY OF DAMAGE, AND ON ANY THEORY OF 
 * LIABILITY, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE 
 * OF THIS SOFTWARE.
 */

/**** pts ****/
#define LZW_SUPPORT 1
typedef unsigned short u_short;
typedef unsigned char u_char;
typedef unsigned long u_long;
#define TO_RDONLY 1
#define _TIFFmalloc malloc
#define _TIFFfree free
#define _TIFFmemset memset
#include <stdlib.h>
#include <string.h>

/*
 * ``Library-private'' definitions.
 */

/*
 * TIFF I/O Library Definitions.
 */
/*
 * Tag Image File Format (TIFF)
 *
 * Based on Rev 6.0 from:
 *    Developer's Desk
 *    Aldus Corporation
 *    411 First Ave. South
 *    Suite 200
 *    Seattle, WA  98104
 *    206-622-5500
 */
#define	TIFF_VERSION	42

#define	TIFF_BIGENDIAN		0x4d4d
#define	TIFF_LITTLEENDIAN	0x4949

#ifndef _TIFF_DATA_TYPEDEFS_
#define _TIFF_DATA_TYPEDEFS_
/*
 * Intrinsic data types required by the file format:
 *
 * 8-bit quantities	int8/uint8
 * 16-bit quantities	int16/uint16
 * 32-bit quantities	int32/uint32
 * strings		unsigned char*
 */
#ifdef __STDC__
typedef	signed char int8;	/* NB: non-ANSI compilers may not grok */
#else
typedef	char int8;
#endif
typedef	unsigned char uint8;
typedef	short int16;
typedef	unsigned short uint16;	/* sizeof (uint16) must == 2 */
#if defined(__alpha) || (defined(_MIPS_SZLONG) && _MIPS_SZLONG == 64)
typedef	int int32;
typedef	unsigned int uint32;	/* sizeof (uint32) must == 4 */
#else
typedef	long int32;
typedef	unsigned long uint32;	/* sizeof (uint32) must == 4 */
#endif
#endif /* _TIFF_DATA_TYPEDEFS_ */

/*
 * NB: In the comments below,
 *  - items marked with a + are obsoleted by revision 5.0,
 *  - items marked with a ! are introduced in revision 6.0.
 *  - items marked with a % are introduced post revision 6.0.
 *  - items marked with a $ are obsoleted by revision 6.0.
 */

/* --- */

/*
 * This define can be used in code that requires
 * compilation-related definitions specific to a
 * version or versions of the library.  Runtime
 * version checking should be done based on the
 * string returned by TIFFGetVersion.
 */
#define	TIFFLIB_VERSION	19970127	/* January 27, 1997 */

/*
 * The following typedefs define the intrinsic size of
 * data types used in the *exported* interfaces.  These
 * definitions depend on the proper definition of types
 * in tiff.h.  Note also that the varargs interface used
 * to pass tag types and values uses the types defined in
 * tiff.h directly.
 *
 * NB: ttag_t is unsigned int and not unsigned short because
 *     ANSI C requires that the type before the ellipsis be a
 *     promoted type (i.e. one of int, unsigned int, pointer,
 *     or double) and because we defined pseudo-tags that are
 *     outside the range of legal Aldus-assigned tags.
 * NB: tsize_t is int32 and not uint32 because some functions
 *     return -1.
 * NB: toff_t is not off_t for many reasons; TIFFs max out at
 *     32-bit file offsets being the most important
 */
typedef	int32 tsize_t;		/* i/o size in bytes */
typedef char tbool_t;

#ifndef NULL
#define	NULL	0
#endif

/*
 * Typedefs for ``method pointers'' used internally.
 */
typedef	unsigned char tidataval_t;	/* internal image data value type */

typedef struct tiff TIFF;

struct tiff {
	/*FILE*/void*	tif_sout;       /**** pts ****/ /* for encode */
	tbool_t		tif_revbits_p;  /**** pts ****/
	tbool_t		tif_reading_p;  /**** pts ****/
	char*		tif_name;	/* name of open file, used for debugging */
/* compression scheme hooks */
	tidataval_t*	tif_data;	/* compression scheme private data */
/* input/output buffering */
        tidataval_t*    tif_rawend;     /**** pts ****/
	tidataval_t*	tif_rawdata;	/* raw data buffer */
	tsize_t		tif_rawdatasize;/* # of bytes in raw data buffer */
	tidataval_t*	tif_rawcp;	/* current spot in raw buffer */
	tsize_t		tif_rawcc;	/* bytes unread from raw buffer */
};

/**** pts ****/
static	int TIFFInitLZW(TIFF*);
static void TIFFError(char const*a, char const*b);
static void TIFFWarning(char const*a, char const*b, int c);
static  int TIFFAppendTo(TIFF*, tidataval_t*, tsize_t); /* tif_write.h */
static void TIFFReverseBits(unsigned char *, unsigned long);


#ifdef LZW_SUPPORT
/*
 * TIFF Library.
 * Rev 5.0 Lempel-Ziv & Welch Compression Support
 *
 * This code is derived from the compress program whose code is
 * derived from software contributed to Berkeley by James A. Woods,
 * derived from original work by Spencer Thomas and Joseph Orost.
 *
 * The original Berkeley copyright notice appears below in its entirety.
 */
 
#include <assert.h>
#include <stdio.h>

/*
 * Internal version of TIFFFlushData that can be
 * called by ``encodestrip routines'' w/o concern
 * for infinite recursion.
 */
static int
TIFFFlushData1(TIFF* tif)
{
        if (tif->tif_rawcc > 0) {
		if (tif->tif_revbits_p)
                        TIFFReverseBits((u_char *)tif->tif_rawdata,
                            tif->tif_rawcc);
                if (!TIFFAppendTo(tif,
                    /* isTiled(tif) ? tif->tif_curtile : tif->tif_curstrip, */
                    tif->tif_rawdata, tif->tif_rawcc))
                        return (0);
                tif->tif_rawcc = 0;
                tif->tif_rawcp = tif->tif_rawdata;
        }
        return (1);
}



/*
 * NB: The 5.0 spec describes a different algorithm than Aldus
 *     implements.  Specifically, Aldus does code length transitions
 *     one code earlier than should be done (for real LZW).
 *     Earlier versions of this library implemented the correct
 *     LZW algorithm, but emitted codes in a bit order opposite
 *     to the TIFF spec.  Thus, to maintain compatibility w/ Aldus
 *     we interpret MSB-LSB ordered codes to be images written w/
 *     old versions of this library, but otherwise adhere to the
 *     Aldus "off by one" algorithm.
 *
 * Future revisions to the TIFF spec are expected to "clarify this issue".
 */
#define	LZW_COMPAT		/* include backwards compatibility code */
/*
 * Each strip of data is supposed to be terminated by a CODE_EOI.
 * If the following #define is included, the decoder will also
 * check for end-of-strip w/o seeing this code.  This makes the
 * library more robust, but also slower.
 */
#define	LZW_CHECKEOS		/* include checks for strips w/o EOI code */
#undef LZW_CHECKEOS /**** pts ****/

#define MAXCODE(n)	((1L<<(n))-1)
/*
 * The TIFF spec specifies that encoded bit
 * strings range from 9 to 12 bits.
 */
#define	BITS_MIN	9		/* start with 9 bits */
#define	BITS_MAX	12		/* max of 12 bit strings */
/* predefined codes */
#define	CODE_CLEAR	256		/* code to clear string table */
#define	CODE_EOI	257		/* end-of-information code */
#define CODE_FIRST	258		/* first free code entry */
#define	CODE_MAX	MAXCODE(BITS_MAX)
#define	HSIZE		9001L		/* 91% occupancy */
#define	HSHIFT		(13-8)
#ifdef LZW_COMPAT
/* NB: +1024 is for compatibility with old files */
#define	CSIZE		(MAXCODE(BITS_MAX)+1024L)
#else
#define	CSIZE		(MAXCODE(BITS_MAX)+1L)
#endif

/*
 * State block for each open TIFF file using LZW
 * compression/decompression.  Note that the predictor
 * state block must be first in this data structure.
 */
typedef	struct {
#if 0 /****pts****/
	TIFFPredictorState predict;	/* predictor super class */
#endif

	u_short		nbits;		/* # of bits/code */
	u_short		maxcode;	/* maximum code for lzw_nbits */
	u_short		free_ent;	/* next free entry in hash table */
	long		nextdata;	/* next bits of i/o */
	long		nextbits;	/* # of valid bits in lzw_nextdata */
} LZWBaseState;

#define	lzw_nbits	base.nbits
#define	lzw_maxcode	base.maxcode
#define	lzw_free_ent	base.free_ent
#define	lzw_nextdata	base.nextdata
#define	lzw_nextbits	base.nextbits

/*
 * Decoding-specific state.
 */
typedef struct code_ent {
	struct code_ent *next;
	u_short	length;			/* string len, including this token */
	u_char	value;			/* data value */
	u_char	firstchar;		/* first token of string */
} code_t;

typedef	int (*decodeFunc)(TIFF*, tidataval_t*, tsize_t);

typedef struct {
	LZWBaseState base;
	long	dec_nbitsmask;		/* lzw_nbits 1 bits, right adjusted */
	long	dec_restart;		/* restart count */
#ifdef LZW_CHECKEOS
	long	dec_bitsleft;		/* available bits in raw data */
#endif
	decodeFunc dec_decode;		/* regular or backwards compatible */
	code_t*	dec_codep;		/* current recognized code */
	code_t*	dec_oldcodep;		/* previously recognized code */
	code_t*	dec_free_entp;		/* next free entry */
	code_t*	dec_maxcodep;		/* max available entry */
	code_t*	dec_codetab;		/* kept separate for small machines */
} LZWDecodeState;

/*
 * Encoding-specific state.
 */
typedef uint16 hcode_t;			/* codes fit in 16 bits */
typedef struct {
	long	hash;
	hcode_t	code;
} hash_t;

typedef struct {
	LZWBaseState base;
	int	enc_oldcode;		/* last code encountered */
	long	enc_checkpoint;		/* point at which to clear table */
#define CHECK_GAP	10000		/* enc_ratio check interval */
	long	enc_ratio;		/* current compression ratio */
	long	enc_incount;		/* (input) data bytes encoded */
	long	enc_outcount;		/* encoded (output) bytes */
	tidataval_t* enc_rawlimit;	/* bound on tif_rawdata buffer */
	hash_t*	enc_hashtab;		/* kept separate for small machines */
} LZWEncodeState;

#define	LZWState(tif)		((LZWBaseState*) (tif)->tif_data)
#define	DecoderState(tif)	((LZWDecodeState*) LZWState(tif))
#define	EncoderState(tif)	((LZWEncodeState*) LZWState(tif))

static	int LZWDecode(TIFF*, tidataval_t*, tsize_t);
#ifdef LZW_COMPAT
static	int LZWDecodeCompat(TIFF*, tidataval_t*, tsize_t);
#endif
static	void cl_hash(LZWEncodeState*);

/*
 * LZW Decoder.
 */

#ifdef LZW_CHECKEOS
/*
 * This check shouldn't be necessary because each
 * strip is suppose to be terminated with CODE_EOI.
 */
#define	NextCode(_tif, _sp, _bp, _code, _get) {				\
	if ((_sp)->dec_bitsleft < nbits) {				\
		TIFFWarning(_tif->tif_name,				\
		    "LZWDecode: Strip %d not terminated with EOI code", \
		    /*_tif->tif_curstrip*/ 0);				\
		_code = CODE_EOI;					\
	} else {							\
		_get(_sp,_bp,_code);					\
		(_sp)->dec_bitsleft -= nbits;				\
	}								\
}
#else
#define	NextCode(tif, sp, bp, code, get) get(sp, bp, code)
#endif

static int
LZWSetupDecode(TIFF* tif)
{
	LZWDecodeState* sp = DecoderState(tif);
	static const char module[] = " LZWSetupDecode";
	int code;

	assert(sp != NULL);
	if (sp->dec_codetab == NULL) {
		sp->dec_codetab = (code_t*)_TIFFmalloc(CSIZE*sizeof (code_t));
		if (sp->dec_codetab == NULL) {
			TIFFError(module, "No space for LZW code table");
			return (0);
		}
		/*
		 * Pre-load the table.
		 */
		for (code = 255; code >= 0; code--) {
			sp->dec_codetab[code].value = code;
			sp->dec_codetab[code].firstchar = code;
			sp->dec_codetab[code].length = 1;
			sp->dec_codetab[code].next = NULL;
		}
	}
	return (1);
}

/*
 * Setup state for decoding a strip.
 */
static int
LZWPreDecode(TIFF* tif)
{
	LZWDecodeState *sp = DecoderState(tif);

	assert(sp != NULL);
	/*
	 * Check for old bit-reversed codes.
	 */
	if (tif->tif_rawdata[0] == 0 && (tif->tif_rawdata[1] & 0x1)) {
#ifdef LZW_COMPAT
		if (!sp->dec_decode) {
			TIFFWarning(tif->tif_name,
			    "Old-style LZW codes, convert file %d", 0);
#if 0 /**** pts ****/
			/*
			 * Override default decoding methods with
			 * ones that deal with the old coding.
			 * Otherwise the predictor versions set
			 * above will call the compatibility routines
			 * through the dec_decode method.
			 */
			tif->tif_decoderow = LZWDecodeCompat;
			tif->tif_decodestrip = LZWDecodeCompat;
			tif->tif_decodetile = LZWDecodeCompat;
			/*
			 * If doing horizontal differencing, must
			 * re-setup the predictor logic since we
			 * switched the basic decoder methods...
			 */
			(*tif->tif_setupdecode)(tif);
#endif
			LZWSetupDecode(tif);
			sp->dec_decode = LZWDecodeCompat;
		}
		sp->lzw_maxcode = MAXCODE(BITS_MIN);
#else /* !LZW_COMPAT */
		if (!sp->dec_decode) {
			TIFFError(tif->tif_name,
			    "Old-style LZW codes not supported");
			sp->dec_decode = LZWDecode;
		}
		return (0);
#endif/* !LZW_COMPAT */
	} else {
		sp->lzw_maxcode = MAXCODE(BITS_MIN)-1;
		sp->dec_decode = LZWDecode;
	}
	sp->lzw_nbits = BITS_MIN;
	sp->lzw_nextbits = 0;
	sp->lzw_nextdata = 0;

	sp->dec_restart = 0;
	sp->dec_nbitsmask = MAXCODE(BITS_MIN);
#ifdef LZW_CHECKEOS
	sp->dec_bitsleft = tif->tif_rawcc << 3;
#endif
	sp->dec_free_entp = sp->dec_codetab + CODE_FIRST;
	/*
	 * Zero entries that are not yet filled in.  We do
	 * this to guard against bogus input data that causes
	 * us to index into undefined entries.  If you can
	 * come up with a way to safely bounds-check input codes
	 * while decoding then you can remove this operation.
	 */
	_TIFFmemset(sp->dec_free_entp, 0, (CSIZE-CODE_FIRST)*sizeof (code_t));
	sp->dec_oldcodep = &sp->dec_codetab[-1];
	sp->dec_maxcodep = &sp->dec_codetab[sp->dec_nbitsmask-1];
	return (1);
}

/*
 * Decode a "hunk of data".
 */
#define	GetNextCode(sp, bp, code) {				\
	nextdata = (nextdata<<8) | *(bp)++;			\
	nextbits += 8;						\
	if (nextbits < nbits) {					\
		nextdata = (nextdata<<8) | *(bp)++;		\
		nextbits += 8;					\
	}							\
	code = (hcode_t)((nextdata >> (nextbits-nbits)) & nbitsmask);	\
	nextbits -= nbits;					\
}

static void
codeLoop(TIFF* tif)
{
	TIFFError(tif->tif_name,
	    "LZWDecode: Bogus encoding, loop in the code table; scanline %d"
	    /*,tif->tif_row*/);
}

static int
LZWDecode(TIFF* tif, tidataval_t* op0, tsize_t occ0)
{
	LZWDecodeState *sp = DecoderState(tif);
	char *op = (char*) op0;
	long occ = (long) occ0;
	char *tp;
	u_char *bp;
	hcode_t code;
	int len;
	long nbits, nextbits, nextdata, nbitsmask;
	code_t *codep, *free_entp, *maxcodep, *oldcodep;

	assert(sp != NULL);
	/*
	 * Restart interrupted output operation.
	 */
	if (sp->dec_restart) {
		long residue;

		codep = sp->dec_codep;
		residue = codep->length - sp->dec_restart;
		if (residue > occ) {
			/*
			 * Residue from previous decode is sufficient
			 * to satisfy decode request.  Skip to the
			 * start of the decoded string, place decoded
			 * values in the output buffer, and return.
			 */
			sp->dec_restart += occ;
			do {
				codep = codep->next;
			} while (--residue > occ && codep);
			if (codep) {
				tp = op + occ;
				do {
					*--tp = codep->value;
					codep = codep->next;
				} while (--occ && codep);
			}
			return occ0-occ;
		}
		/*
		 * Residue satisfies only part of the decode request.
		 */
		op += residue, occ -= residue;
		tp = op;
		do {
			int t;
			--tp;
			t = codep->value;
			codep = codep->next;
			*tp = t;
		} while (--residue && codep);
		sp->dec_restart = 0;
	}

	bp = (u_char *)tif->tif_rawcp; /* reading from here */
	nbits = sp->lzw_nbits;
	nextdata = sp->lzw_nextdata;
	nextbits = sp->lzw_nextbits;
	nbitsmask = sp->dec_nbitsmask;
	oldcodep = sp->dec_oldcodep;
	free_entp = sp->dec_free_entp;
	maxcodep = sp->dec_maxcodep;

	while (occ > 0 && bp<tif->tif_rawend /**** pts ****/) {
		NextCode(tif, sp, bp, code, GetNextCode);
		#if 0
		  if (bp>tif->tif_rawend) fprintf(stderr, "over %d\n", tif->tif_rawend-bp);
		  assert(bp<=tif->tif_rawend);
		#endif
		if (code == CODE_EOI)
			break;
		if (code == CODE_CLEAR) {
			free_entp = sp->dec_codetab + CODE_FIRST;
			nbits = BITS_MIN;
			nbitsmask = MAXCODE(BITS_MIN);
			maxcodep = sp->dec_codetab + nbitsmask-1;
			
#if 1 /**** pts ****/
			NextCode(tif, sp, bp, code, GetNextCode);
			if (code == CODE_EOI)
				break;
			*op++ = code, occ--;
			oldcodep = sp->dec_codetab + code; /* ! */
#endif			
			continue;
		}
		codep = sp->dec_codetab + code;

		/*
	 	 * Add the new entry to the code table.
	 	 */
		assert(&sp->dec_codetab[0] <= free_entp && free_entp < &sp->dec_codetab[CSIZE]);
		free_entp->next = oldcodep;
		free_entp->firstchar = free_entp->next->firstchar;
		free_entp->length = free_entp->next->length+1;
		free_entp->value = (codep < free_entp) ?
		    codep->firstchar : free_entp->firstchar;
		if (++free_entp > maxcodep) {
			if (++nbits > BITS_MAX)		/* should not happen */
				nbits = BITS_MAX;
			nbitsmask = MAXCODE(nbits);
			maxcodep = sp->dec_codetab + nbitsmask-1;
		}
		oldcodep = codep;
		if (code >= 256) {
			/*
		 	 * Code maps to a string, copy string
			 * value to output (written in reverse).
		 	 */
			if (codep->length > occ) {
				/*
				 * String is too long for decode buffer,
				 * locate portion that will fit, copy to
				 * the decode buffer, and setup restart
				 * logic for the next decoding call.
				 */
				sp->dec_codep = codep;
				do {
					codep = codep->next;
				} while (codep && codep->length > occ);
				if (codep) {
					sp->dec_restart = occ;
					tp = op + occ;
					do  {
						*--tp = codep->value;
						codep = codep->next;
					}  while (--occ && codep);
					if (codep) {
						codeLoop(tif);
						return -1;
					}
				}
				break;
			}
			len = codep->length;
			tp = op + len;
			do {
				int t;
				--tp;
				t = codep->value;
				codep = codep->next;
				*tp = t;
			} while (codep && tp > op);
			if (codep) {
			    codeLoop(tif);
			    return -1;
			    /* break; */
			}
			op += len, occ -= len;
		} else
			*op++ = code, occ--;
	}

	tif->tif_rawcp = (tidataval_t*) bp;
	sp->lzw_nbits = (u_short) nbits;
	sp->lzw_nextdata = nextdata;
	sp->lzw_nextbits = nextbits;
	sp->dec_nbitsmask = nbitsmask;
	sp->dec_oldcodep = oldcodep;
	sp->dec_free_entp = free_entp;
	sp->dec_maxcodep = maxcodep;

#if 0 /**** pts ****/
	if (occ > 0) {
		TIFFError(tif->tif_name,
		"LZWDecode: Not enough data at scanline %d (short %d bytes)"
		    /*,tif->tif_row, occ*/);
		return (0);
	}
#endif
	return occ0-occ;
}

#ifdef LZW_COMPAT
/*
 * Decode a "hunk of data" for old images.
 */
#define	GetNextCodeCompat(sp, bp, code) {			\
	nextdata |= (u_long) *(bp)++ << nextbits;		\
	nextbits += 8;						\
	if (nextbits < nbits) {					\
		nextdata |= (u_long) *(bp)++ << nextbits;	\
		nextbits += 8;					\
	}							\
	code = (hcode_t)(nextdata & nbitsmask);			\
	nextdata >>= nbits;					\
	nextbits -= nbits;					\
}

static int
LZWDecodeCompat(TIFF* tif, tidataval_t* op0, tsize_t occ0)
{
	LZWDecodeState *sp = DecoderState(tif);
	char *op = (char*) op0;
	long occ = (long) occ0;
	char *tp;
	u_char *bp;
	int code, nbits;
	long nextbits, nextdata, nbitsmask;
	code_t *codep, *free_entp, *maxcodep, *oldcodep;

	assert(0);
	assert(sp != NULL);
	/*
	 * Restart interrupted output operation.
	 */
	if (sp->dec_restart) {
		long residue;

		codep = sp->dec_codep;
		residue = codep->length - sp->dec_restart;
		if (residue > occ) {
			/*
			 * Residue from previous decode is sufficient
			 * to satisfy decode request.  Skip to the
			 * start of the decoded string, place decoded
			 * values in the output buffer, and return.
			 */
			sp->dec_restart += occ;
			do {
				codep = codep->next;
			} while (--residue > occ);
			tp = op + occ;
			do {
				*--tp = codep->value;
				codep = codep->next;
			} while (--occ);
			return occ0-occ;
		}
		/*
		 * Residue satisfies only part of the decode request.
		 */
		op += residue, occ -= residue;
		tp = op;
		do {
			*--tp = codep->value;
			codep = codep->next;
		} while (--residue);
		sp->dec_restart = 0;
	}

	bp = (u_char *)tif->tif_rawcp;
	nbits = sp->lzw_nbits;
	nextdata = sp->lzw_nextdata;
	nextbits = sp->lzw_nextbits;
	nbitsmask = sp->dec_nbitsmask;
	oldcodep = sp->dec_oldcodep;
	free_entp = sp->dec_free_entp;
	maxcodep = sp->dec_maxcodep;

	while (occ > 0 && bp<tif->tif_rawend) {
		NextCode(tif, sp, bp, code, GetNextCodeCompat);
		if (code == CODE_EOI)
			break;
		if (code == CODE_CLEAR) {
			free_entp = sp->dec_codetab + CODE_FIRST;
			nbits = BITS_MIN;
			nbitsmask = MAXCODE(BITS_MIN);
			maxcodep = sp->dec_codetab + nbitsmask;
			NextCode(tif, sp, bp, code, GetNextCodeCompat);
			if (code == CODE_EOI)
				break;
			*op++ = code, occ--;
			oldcodep = sp->dec_codetab + code;
			continue;
		}
		codep = sp->dec_codetab + code;

		/*
	 	 * Add the new entry to the code table.
	 	 */
		assert(&sp->dec_codetab[0] <= free_entp && free_entp < &sp->dec_codetab[CSIZE]);
		free_entp->next = oldcodep;
		free_entp->firstchar = free_entp->next->firstchar;
		free_entp->length = free_entp->next->length+1;
		free_entp->value = (codep < free_entp) ?
		    codep->firstchar : free_entp->firstchar;
		if (++free_entp > maxcodep) {
			if (++nbits > BITS_MAX)		/* should not happen */
				nbits = BITS_MAX;
			nbitsmask = MAXCODE(nbits);
			maxcodep = sp->dec_codetab + nbitsmask;
		}
		oldcodep = codep;
		if (code >= 256) {
			/*
		 	 * Code maps to a string, copy string
			 * value to output (written in reverse).
		 	 */
			if (codep->length > occ) {
				/*
				 * String is too long for decode buffer,
				 * locate portion that will fit, copy to
				 * the decode buffer, and setup restart
				 * logic for the next decoding call.
				 */
				sp->dec_codep = codep;
				do {
					codep = codep->next;
				} while (codep->length > occ);
				sp->dec_restart = occ;
				tp = op + occ;
				do  {
					*--tp = codep->value;
					codep = codep->next;
				}  while (--occ);
				break;
			}
			op += codep->length, occ -= codep->length;
			tp = op;
			do {
				*--tp = codep->value;
			} while (0!=(codep = codep->next));
		} else
			*op++ = code, occ--;
	}

	tif->tif_rawcp = (tidataval_t*) bp;
	sp->lzw_nbits = nbits;
	sp->lzw_nextdata = nextdata;
	sp->lzw_nextbits = nextbits;
	sp->dec_nbitsmask = nbitsmask;
	sp->dec_oldcodep = oldcodep;
	sp->dec_free_entp = free_entp;
	sp->dec_maxcodep = maxcodep;

#if 0 /**** pts ****/
	if (occ > 0) {
		TIFFError(tif->tif_name,
		    "LZWDecodeCompat: Not enough data at scanline %d (short %d bytes)"
		    /*,tif->tif_row, occ*/);
		return (0);
	}
#endif
	return occ0-occ;
}
#endif /* LZW_COMPAT */

/* --- */

/*
 * LZW Encoding.
 */

static int
LZWSetupEncode(TIFF* tif)
{
	LZWEncodeState* sp = EncoderState(tif);
	static const char module[] = "LZWSetupEncode";

	assert(sp != NULL);
	sp->enc_hashtab = (hash_t*) _TIFFmalloc(HSIZE*sizeof (hash_t));
	if (sp->enc_hashtab == NULL) {
		TIFFError(module, "No space for LZW hash table");
		return (0);
	}
	return (1);
}

/*
 * Reset encoding state at the start of a strip.
 */
static int
LZWPreEncode(TIFF* tif)
{
	LZWEncodeState *sp = EncoderState(tif);

	assert(sp != NULL);
	sp->lzw_nbits = BITS_MIN;
	sp->lzw_maxcode = MAXCODE(BITS_MIN);
	sp->lzw_free_ent = CODE_FIRST;
	sp->lzw_nextbits = 0;
	sp->lzw_nextdata = 0;
	sp->enc_checkpoint = CHECK_GAP;
	sp->enc_ratio = 0;
	sp->enc_incount = 0;
	sp->enc_outcount = 0;
	/*
	 * The 4 here insures there is space for 2 max-sized
	 * codes in LZWEncode and LZWPostDecode.
	 */
	sp->enc_rawlimit = tif->tif_rawdata + tif->tif_rawdatasize-1 - 4;
	cl_hash(sp);		/* clear hash table */
	sp->enc_oldcode = (hcode_t) -1;	/* generates CODE_CLEAR in LZWEncode */
	return (1);
}

#define	CALCRATIO(sp, rat) {					\
	if (incount > 0x007fffff) { /* NB: shift will overflow */\
		rat = outcount >> 8;				\
		rat = (rat == 0 ? 0x7fffffff : incount/rat);	\
	} else							\
		rat = (incount<<8) / outcount;			\
}
#define	PutNextCode(op, c) {					\
	nextdata = (nextdata << nbits) | c;			\
	nextbits += nbits;					\
	*op++ = (u_char)(nextdata >> (nextbits-8));		\
	nextbits -= 8;						\
	if (nextbits >= 8) {					\
		*op++ = (u_char)(nextdata >> (nextbits-8));	\
		nextbits -= 8;					\
	}							\
	outcount += nbits;					\
}

/*
 * Encode a chunk of pixels.
 *
 * Uses an open addressing double hashing (no chaining) on the 
 * prefix code/next character combination.  We do a variant of
 * Knuth's algorithm D (vol. 3, sec. 6.4) along with G. Knott's
 * relatively-prime secondary probe.  Here, the modular division
 * first probe is gives way to a faster exclusive-or manipulation. 
 * Also do block compression with an adaptive reset, whereby the
 * code table is cleared when the compression ratio decreases,
 * but after the table fills.  The variable-length output codes
 * are re-sized at this point, and a CODE_CLEAR is generated
 * for the decoder. 
 */
static int
LZWEncode(TIFF* tif, tidataval_t* bp, tsize_t cc)
{
	register LZWEncodeState *sp = EncoderState(tif);
	register long fcode;
	register hash_t *hp;
	register int h, c;
	hcode_t ent;
	long disp;
	long incount, outcount, checkpoint;
	long nextdata, nextbits;
	int free_ent, maxcode, nbits;
	tidataval_t* op, *limit;

	if (sp == NULL)
		return (0);
	/*
	 * Load local state.
	 */
	incount = sp->enc_incount;
	outcount = sp->enc_outcount;
	checkpoint = sp->enc_checkpoint;
	nextdata = sp->lzw_nextdata;
	nextbits = sp->lzw_nextbits;
	free_ent = sp->lzw_free_ent;
	maxcode = sp->lzw_maxcode;
	nbits = sp->lzw_nbits;
	op = tif->tif_rawcp;
	limit = sp->enc_rawlimit;
	ent = sp->enc_oldcode;

	if (ent == (hcode_t) -1 && cc > 0) {
		/*
		 * NB: This is safe because it can only happen
		 *     at the start of a strip where we know there
		 *     is space in the data buffer.
		 */
		PutNextCode(op, CODE_CLEAR);
		ent = *bp++; cc--; incount++;
	}
	while (cc > 0) {
		c = *bp++; cc--; incount++;
		fcode = ((long)c << BITS_MAX) + ent;
		h = (c << HSHIFT) ^ ent;	/* xor hashing */
#ifdef _WINDOWS /* ?? */
		/*
		 * Check hash index for an overflow.
		 */
		if (h >= HSIZE)
			h -= HSIZE;
#endif
		hp = &sp->enc_hashtab[h];
		if (hp->hash == fcode) {
			ent = hp->code;
			continue;
		}
		if (hp->hash >= 0) {
			/*
			 * Primary hash failed, check secondary hash.
			 */
			disp = HSIZE - h;
			if (h == 0)
				disp = 1;
			do {
				/*
				 * Avoid pointer arithmetic 'cuz of
				 * wraparound problems with segments.
				 */
				if ((h -= disp) < 0)
					h += HSIZE;
				hp = &sp->enc_hashtab[h];
				if (hp->hash == fcode) {
					ent = hp->code;
					goto hit;
				}
			} while (hp->hash >= 0);
		}
		/*
		 * New entry, emit code and add to table.
		 */
		/*
		 * Verify there is space in the buffer for the code
		 * and any potential Clear code that might be emitted
		 * below.  The value of limit is setup so that there
		 * are at least 4 bytes free--room for 2 codes.
		 */
		if (op > limit) {
			tif->tif_rawcc = (tsize_t)(op - tif->tif_rawdata);
			TIFFFlushData1(tif);
			op = tif->tif_rawdata;
		}
		PutNextCode(op, ent);
		ent = c;
		hp->code = free_ent++;
		hp->hash = fcode;
		if (free_ent == CODE_MAX-1) {
			/* table is full, emit clear code and reset */
			cl_hash(sp);
			sp->enc_ratio = 0;
			incount = 0;
			outcount = 0;
			free_ent = CODE_FIRST;
			PutNextCode(op, CODE_CLEAR);
			nbits = BITS_MIN;
			maxcode = MAXCODE(BITS_MIN);
		} else {
			/*
			 * If the next entry is going to be too big for
			 * the code size, then increase it, if possible.
			 */
			if (free_ent > maxcode) {
				nbits++;
				assert(nbits <= BITS_MAX);
				maxcode = (int) MAXCODE(nbits);
			} else if (incount >= checkpoint) {
				long rat;
				/*
				 * Check compression ratio and, if things seem
				 * to be slipping, clear the hash table and
				 * reset state.  The compression ratio is a
				 * 24+8-bit fractional number.
				 */
				checkpoint = incount+CHECK_GAP;
				CALCRATIO(sp, rat);
				if (rat <= sp->enc_ratio) {
					cl_hash(sp);
					sp->enc_ratio = 0;
					incount = 0;
					outcount = 0;
					free_ent = CODE_FIRST;
					PutNextCode(op, CODE_CLEAR);
					nbits = BITS_MIN;
					maxcode = MAXCODE(BITS_MIN);
				} else
					sp->enc_ratio = rat;
			}
		}
	hit:
		;
	}

	/*
	 * Restore global state.
	 */
	sp->enc_incount = incount;
	sp->enc_outcount = outcount;
	sp->enc_checkpoint = checkpoint;
	sp->enc_oldcode = ent;
	sp->lzw_nextdata = nextdata;
	sp->lzw_nextbits = nextbits;
	sp->lzw_free_ent = free_ent;
	sp->lzw_maxcode = maxcode;
	sp->lzw_nbits = nbits;
	tif->tif_rawcp = op;
	return (1);
}

/*
 * Finish off an encoded strip by flushing the last
 * string and tacking on an End Of Information code.
 */
static int
LZWPostEncode(TIFF* tif)
{
	register LZWEncodeState *sp = EncoderState(tif);
	tidataval_t* op = tif->tif_rawcp;
	long nextbits = sp->lzw_nextbits;
	long nextdata = sp->lzw_nextdata;
	long outcount = sp->enc_outcount;
	int nbits = sp->lzw_nbits;

	if (op > sp->enc_rawlimit) {
		/* fprintf(stderr, "Yupp!\n"); */
		tif->tif_rawcc = (tsize_t)(op - tif->tif_rawdata);
		TIFFFlushData1(tif);
		op = tif->tif_rawdata;
	}
	if (sp->enc_oldcode != (hcode_t) -1) {
		/* fprintf(stderr, "EIK\n"); */
		PutNextCode(op, sp->enc_oldcode);
		sp->enc_oldcode = (hcode_t) -1;
	}
	PutNextCode(op, CODE_EOI);
	if (nextbits > 0) 
		*op++ = (u_char)(nextdata << (8-nextbits));
	tif->tif_rawcc = (tsize_t)(op - tif->tif_rawdata);
	return (1);
}

/*
 * Reset encoding hash table.
 */
static void
cl_hash(LZWEncodeState* sp)
{
	register hash_t *hp = &sp->enc_hashtab[HSIZE-1];
	register long i = HSIZE-8;

 	do {
		i -= 8;
		hp[-7].hash = -1;
		hp[-6].hash = -1;
		hp[-5].hash = -1;
		hp[-4].hash = -1;
		hp[-3].hash = -1;
		hp[-2].hash = -1;
		hp[-1].hash = -1;
		hp[ 0].hash = -1;
		hp -= 8;
	} while (i >= 0);
    	for (i += 8; i > 0; i--, hp--)
		hp->hash = -1;
}

static void
LZWCleanup(TIFF* tif)
{
	if (tif->tif_data) {
		if (tif->tif_reading_p) {
			if (DecoderState(tif)->dec_codetab)
				_TIFFfree(DecoderState(tif)->dec_codetab);
		} else {
			if (EncoderState(tif)->enc_hashtab)
				_TIFFfree(EncoderState(tif)->enc_hashtab);
		}
		_TIFFfree(tif->tif_data);
		tif->tif_data = NULL;
	}
}

static int
TIFFInitLZW(TIFF* tif)
{
	/* assert(scheme == COMPRESSION_LZW); */
	/*
	 * Allocate state block so tag methods have storage to record values.
	 */
	if (tif->tif_reading_p) {
		tif->tif_data = (tidataval_t*) _TIFFmalloc(sizeof (LZWDecodeState));
		if (tif->tif_data == NULL)
			goto bad;
		DecoderState(tif)->dec_codetab = NULL;
		DecoderState(tif)->dec_decode = NULL;
	} else {
		tif->tif_data = (tidataval_t*) _TIFFmalloc(sizeof (LZWEncodeState));
		if (tif->tif_data == NULL)
			goto bad;
		EncoderState(tif)->enc_hashtab = NULL;
	}
#if 0 /**** pts ****/
	/*
	 * Install codec methods.
	 */
	tif->tif_setupdecode = LZWSetupDecode;
	tif->tif_predecode = LZWPreDecode;
	tif->tif_decoderow = LZWDecode;
	tif->tif_decodestrip = LZWDecode;
	tif->tif_decodetile = LZWDecode;
	tif->tif_setupencode = LZWSetupEncode;
	tif->tif_preencode = LZWPreEncode;
	tif->tif_postencode = LZWPostEncode;
	tif->tif_encoderow = LZWEncode;
	tif->tif_encodestrip = LZWEncode;
	tif->tif_encodetile = LZWEncode;
	tif->tif_cleanup = LZWCleanup;
#endif
#if 0 /**** pts ****/
	/*
	 * Setup predictor setup.
	 */
	(void) TIFFPredictorInit(tif);
#endif
	return (1);
bad:
	TIFFError("TIFFInitLZW", "No space for LZW state block");
	return (0);
}

/*
 * Copyright (c) 1985, 1986 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * James A. Woods, derived from original work by Spencer Thomas
 * and Joseph Orost.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that the above copyright notice and this paragraph are
 * duplicated in all such forms and that any documentation,
 * advertising materials, and other materials related to such
 * distribution and use acknowledge that the software was developed
 * by the University of California, Berkeley.  The name of the
 * University may not be used to endorse or promote products derived
 * from this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */
#endif /* LZW_SUPPORT */

#include <stdio.h>
extern FILE *fdopen (int fildes, const char *mode); /* POSIX, but not ANSI */

void
TIFFReverseBits(register u_char* cp, register u_long n)
{
        char *TIFFBitRevTable="z"; /**** pts !! ****/
        for (; n > 8; n -= 8) {
                cp[0] = TIFFBitRevTable[cp[0]];
                cp[1] = TIFFBitRevTable[cp[1]];
                cp[2] = TIFFBitRevTable[cp[2]];
                cp[3] = TIFFBitRevTable[cp[3]];
                cp[4] = TIFFBitRevTable[cp[4]];
                cp[5] = TIFFBitRevTable[cp[5]];
                cp[6] = TIFFBitRevTable[cp[6]];
                cp[7] = TIFFBitRevTable[cp[7]];
                cp += 8;
        }
        while (n-- > 0)
                *cp = TIFFBitRevTable[*cp], cp++;
}

void TIFFError(char const*a, char const*b) {
  fprintf(stderr, "%s: %s\n", a, b);
}
void TIFFWarning(char const*a, char const*b, int c) {
  fprintf(stderr, "Warning: %s: ", a);
  fprintf(stderr, b, c);
  fprintf(stderr, "\n");
}
int TIFFAppendTo(TIFF*tif, tidataval_t* data, tsize_t cc) {
   (void)tif;
   (void)data;
   (void)cc;
   fwrite(data, 1, cc, (FILE*)tif->tif_sout);
#if 0 /**** pts ****/ /* tif_write.h */
   (void)strip;
        TIFFDirectory *td = &tif->tif_dir;
        static const char module[] = "TIFFAppendToStrip";

        if (td->td_stripoffset[strip] == 0 || tif->tif_curoff == 0) {
                /*
                 * No current offset, set the current strip.
                 */
                if (td->td_stripoffset[strip] != 0) {
                        if (!SeekOK(tif, td->td_stripoffset[strip])) {
                                TIFFError(module,
                                    "%s: Seek error at scanline %lu",
                                    tif->tif_name, (u_long) tif->tif_row);
                                return (0);
                        }
                } else
                        td->td_stripoffset[strip] =
                            TIFFSeekFile(tif, (toff_t) 0, SEEK_END);
                tif->tif_curoff = td->td_stripoffset[strip];
        }
        if (!WriteOK(tif, data, cc)) {
                TIFFError(module, "%s: Write error at scanline %lu",
                    tif->tif_name, (u_long) tif->tif_row);
                return (0);
        }
        tif->tif_curoff += cc;
        td->td_stripbytecount[strip] += cc;
#endif
        return (1);
}

static tidataval_t readbuf[4096];
unsigned int readlen;
static char *inname;

typedef int (*filter_t)(FILE *sin, FILE*sout);

/** /LZWEncode filter, STDIN -> STDOUT */
static int lzw_encode(FILE *sin, FILE *sout) {
  TIFF tif;
  tif.tif_sout=sout;
  tif.tif_reading_p=0;
  tif.tif_revbits_p=0;
  tif.tif_name=inname;
  tif.tif_rawdata=_TIFFmalloc(tif.tif_rawdatasize=4096); /* Imp: check */
  tif.tif_rawcp=tif.tif_rawdata;
  tif.tif_rawcc=0;
  if (TIFFInitLZW(&tif) &&
      LZWSetupEncode(&tif) &&
      LZWPreEncode(&tif) /* for each strip */) {
    while ((readlen=fread(readbuf, 1, sizeof(readbuf), sin))!=0) {
      if (!LZWEncode(&tif, readbuf, readlen)) goto err;
      /* fprintf(stderr, "readlen=%d\n", readlen); */
    }
    if (!LZWPostEncode(&tif)) goto err; /* for each strip */
    LZWCleanup(&tif);
    if (!TIFFFlushData1(&tif)) { _TIFFfree(tif.tif_rawdata); fflush(sout); return 0; }
    fflush(sout);
  } else { err:
    fflush(sout);
    LZWCleanup(&tif);
    _TIFFfree(tif.tif_rawdata);
    return 0;
  }
  return 1;
}

/** /LZWEncode filter, STDIN -> STDOUT */
static int lzw_decode(FILE *sin, FILE *sout) {
  TIFF tif;
  /* tidataval_t *rawend0; */
  /* char *xbuf; */
  int got;
  unsigned int left;
  tif.tif_reading_p=1;
  tif.tif_revbits_p=0;
  tif.tif_name=inname;
  tif.tif_rawdata=_TIFFmalloc(tif.tif_rawdatasize=4096); /* Imp: check */
  tif.tif_rawcc=0;
  left=0;
  if (TIFFInitLZW(&tif) &&
      LZWSetupDecode(&tif) &&
      LZWPreDecode(&tif) /* for each strip */) {
    /* vvv Dat: fread returns >=0 ! */
    while ((readlen=left+fread(tif.tif_rawdata+left, 1, tif.tif_rawdatasize-left, sin))!=0) {
     #if DEBUGMSG
      fprintf(stderr, "readlen+=%d\n", readlen);
     #endif
      while (readlen<=3) {
        if ((got=fread(tif.tif_rawdata+readlen, 1, tif.tif_rawdatasize-readlen, sin))==0) {
          tif.tif_rawend=tif.tif_rawdata+readlen;
          goto star;
        }
        readlen+=got;
      }
      tif.tif_rawend=tif.tif_rawdata+readlen-3;
     star:
      tif.tif_rawcp=tif.tif_rawdata;
     #if DEBUGMSG
      fprintf(stderr, "readlen:=%d\n", readlen);
     #endif
      while (1) {
        if (-1==(got=(DecoderState(&tif)->dec_decode)(&tif, readbuf, sizeof(readbuf)))) goto err;
       #if DEBUGMSG
        fprintf(stderr, "OK, written: %d %d\n", got, tif.tif_rawend-tif.tif_rawcp);
       #endif
        if (0==got) break;
        fwrite(readbuf, 1, got, sout);
      }
      left=tif.tif_rawdata+readlen-tif.tif_rawcp;
      got=left;
     #if DEBUGMSG
      fprintf(stderr, "left=%d\n", left); fflush(stderr);
     #endif
      while (got--!=0) { tif.tif_rawdata[got]=tif.tif_rawcp[got]; }
    }
#if 0   
    if (!LZWPostDecode(&tif)) { LZWCleanup(&tif); return 0; } /* for each strip */
#endif
    LZWCleanup(&tif);
    fflush(sout);
  } else { err:
    fflush(sout);
    LZWCleanup(&tif);
    _TIFFfree(tif.tif_rawdata);
    return 0;
  }
  return 1;
}

/**** pts ****/
int main(int argc, char **argv) {
  filter_t filter;
  FILE *sin, *sout;
  inname="STDIN";
  
  if (argc>=2 && argc<=4 && argv[1][0]=='e') {
    filter=lzw_encode;
  } else if (argc>=2 && argc<=4 && argv[1][0]=='d') {
    filter=lzw_decode;
  } else {
    fprintf(stderr,
      "This is LZW codec v0.11, (C) pts@fazekas.hu in Late Dec 2001\n"
      "THIS SOFTWARE COMES WITH ABSOLUTELY NO WARRANTY! USE AT YOUR OWN RISK!\n"
      "This program is free software, covered by the GNU GPL.\n"
      "  Derived from code Copyright (c) 1988-1997 Sam Leffler\n"
      "  Derived from code Copyright (c) 1991-1997 Silicon Graphics, Inc.\n\n"
      "Usage: %s encode|decode [INFILE] [OUTFILE]\n\n%s", argv[0],
      "Unspecified file names mean STDIN or STDOUT.\n"
      "Encoding is /LZWEncode compression, decoding is /LZWDecode uncompression.\n\n"
      "Note that the LZW compression (but not uncompression) is patented by\n"
      "Unisys (patent number #4,558,302), so use this program at your own legal\n"
      "risk!\n");
    return 2;
  }
  if (argc>=3) sin= fopen(inname=argv[2],"rb");
          else sin= fdopen(0, "rb");
  if (sin==0) {
    fprintf(stderr, "%s: error opening infile\n", argv[0]);
    return 3;
  }
  if (argc>=4) sout=fopen(inname=argv[3],"wb");
          else sout=fdopen(1, "wb");
  if (sout==0) {
    fprintf(stderr, "%s: error opening outfile\n", argv[0]);
    return 4;
  }
  return !filter(sin, sout);
  /* fclose(sout); fclose(sin); */
}