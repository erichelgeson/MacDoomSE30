// Emacs style mode select   -*- C++ -*- 
//-----------------------------------------------------------------------------
//
// $Id:$
//
// Copyright (C) 1993-1996 by id Software, Inc.
//
// This source is available for distribution and/or modification
// only under the terms of the DOOM Source Code License as
// published by id Software. All rights reserved.
//
// The source is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// FITNESS FOR A PARTICULAR PURPOSE. See the DOOM Source Code License
// for more details.
//
// $Log:$
//
// DESCRIPTION:
//	The actual span/column drawing functions.
//	Here find the main potential for optimization,
//	 e.g. inline assembly, different algorithms.
//
//-----------------------------------------------------------------------------


static const char
rcsid[] = "$Id: r_draw.c,v 1.4 1997/02/03 16:47:55 b1 Exp $";


#include "doomdef.h"

#include "i_system.h"
#include "z_zone.h"
#include "w_wad.h"

#include "r_local.h"

// Needs access to LFB (guess what).
#include "v_video.h"

// State.
#include "doomstat.h"


// ?
#define MAXWIDTH			1120
#define MAXHEIGHT			832

// status bar height at bottom of screen
#define SBARHEIGHT		32

/* ---- Direct 1-bit framebuffer access (set by i_video_mac.c) ---- */
extern byte           *fb_mono_base;
extern int             fb_mono_rowbytes;
extern int             fb_mono_xoff;
extern int             fb_mono_yoff;
extern byte            grayscale_pal[];   /* palette index → 0-255 gray */
extern byte            mono_colormaps[];  /* [level*256+pal] = grayscale_pal[colormap[level*256+pal]] */
extern int             no_lighting;       /* 1 = force fullbright colormap */
extern const byte      bayer4x4[4][4];   /* 4x4 Bayer dither matrix */

/* ---- SE/30 performance opt flags (defined in d_main.c, set via doom.cfg) ---- */
extern int opt_halfline;      /* halfline:   skip odd rows, 2x texture step */
extern int opt_affine_texcol; /* affinetex:  linear texcol interp per wall (r_segs.c) */
extern int opt_solidfloor;    /* solidfloor: fill floors/ceilings with solid colour */

//
// All drawing to the view buffer is accomplished in this file.
// The other refresh files only know about ccordinates,
//  not the architecture of the frame buffer.
// Conveniently, the frame buffer is a linear one,
//  and we need only the base address,
//  and the total size == width*height*depth/8.,
//


byte*		viewimage; 
int		viewwidth;
int		scaledviewwidth;
int		viewheight;
int		viewwindowx;
int		viewwindowy; 
byte*		ylookup[MAXHEIGHT]; 
int		columnofs[MAXWIDTH]; 

// Color tables for different players,
//  translate a limited part to another
//  (color ramps used for  suit colors).
//
byte		translations[3][256];	
 
 


//
// R_DrawColumn
// Source is the top of the column to scale.
//
lighttable_t*		dc_colormap; 
int			dc_x; 
int			dc_yl; 
int			dc_yh; 
fixed_t			dc_iscale; 
fixed_t			dc_texturemid;

// first pixel in a column (possibly virtual) 
byte*			dc_source;		

// just for profiling 
int			dccount;

//
// A column is a vertical slice/span from a wall texture that,
//  given the DOOM style restrictions on the view orientation,
//  will always have constant z depth.
// Thus a special case loop for very fast rendering can
//  be used. It has also been used with Wolfenstein 3D.
// 
void R_DrawColumn (void) 
{ 
    int			count; 
    byte*		dest; 
    fixed_t		frac;
    fixed_t		fracstep;	 
 
    count = dc_yh - dc_yl; 

    // Zero length, column does not exceed a pixel.
    if (count < 0) 
	return; 
				 
#ifdef RANGECHECK 
    if ((unsigned)dc_x >= SCREENWIDTH
	|| dc_yl < 0
	|| dc_yh >= SCREENHEIGHT) 
	I_Error ("R_DrawColumn: %i to %i at %i", dc_yl, dc_yh, dc_x); 
#endif 

    // Framebuffer destination address.
    // Use ylookup LUT to avoid multiply with ScreenWidth.
    // Use columnofs LUT for subwindows? 
    dest = ylookup[dc_yl] + columnofs[dc_x];  

    // Determine scaling,
    //  which is the only mapping to be done.
    fracstep = dc_iscale; 
    frac = dc_texturemid + (dc_yl-centery)*fracstep; 

    // Inner loop that does the actual texture mapping,
    //  e.g. a DDA-lile scaling.
    // This is as fast as it gets.
    do 
    {
	// Re-map color indices from wall texture column
	//  using a lighting/special effects LUT.
	*dest = dc_colormap[dc_source[(frac>>FRACBITS)&127]];
	
	dest += SCREENWIDTH; 
	frac += fracstep;
	
    } while (count--); 
} 



// UNUSED.
// Loop unrolled.
#if 0
void R_DrawColumn (void) 
{ 
    int			count; 
    byte*		source;
    byte*		dest;
    byte*		colormap;
    
    unsigned		frac;
    unsigned		fracstep;
    unsigned		fracstep2;
    unsigned		fracstep3;
    unsigned		fracstep4;	 
 
    count = dc_yh - dc_yl + 1; 

    source = dc_source;
    colormap = dc_colormap;		 
    dest = ylookup[dc_yl] + columnofs[dc_x];  
	 
    fracstep = dc_iscale<<9; 
    frac = (dc_texturemid + (dc_yl-centery)*dc_iscale)<<9; 
 
    fracstep2 = fracstep+fracstep;
    fracstep3 = fracstep2+fracstep;
    fracstep4 = fracstep3+fracstep;
	
    while (count >= 8) 
    { 
	dest[0] = colormap[source[frac>>25]]; 
	dest[SCREENWIDTH] = colormap[source[(frac+fracstep)>>25]]; 
	dest[SCREENWIDTH*2] = colormap[source[(frac+fracstep2)>>25]]; 
	dest[SCREENWIDTH*3] = colormap[source[(frac+fracstep3)>>25]];
	
	frac += fracstep4; 

	dest[SCREENWIDTH*4] = colormap[source[frac>>25]]; 
	dest[SCREENWIDTH*5] = colormap[source[(frac+fracstep)>>25]]; 
	dest[SCREENWIDTH*6] = colormap[source[(frac+fracstep2)>>25]]; 
	dest[SCREENWIDTH*7] = colormap[source[(frac+fracstep3)>>25]]; 

	frac += fracstep4; 
	dest += SCREENWIDTH*8; 
	count -= 8;
    } 
	
    while (count > 0)
    { 
	*dest = colormap[source[frac>>25]]; 
	dest += SCREENWIDTH; 
	frac += fracstep; 
	count--;
    } 
}
#endif


void R_DrawColumnLow (void) 
{ 
    int			count; 
    byte*		dest; 
    byte*		dest2;
    fixed_t		frac;
    fixed_t		fracstep;	 
 
    count = dc_yh - dc_yl; 

    // Zero length.
    if (count < 0) 
	return; 
				 
#ifdef RANGECHECK 
    if ((unsigned)dc_x >= SCREENWIDTH
	|| dc_yl < 0
	|| dc_yh >= SCREENHEIGHT)
    {
	
	I_Error ("R_DrawColumn: %i to %i at %i", dc_yl, dc_yh, dc_x);
    }
    //	dccount++; 
#endif 
    // Blocky mode, need to multiply by 2.
    dc_x <<= 1;
    
    dest = ylookup[dc_yl] + columnofs[dc_x];
    dest2 = ylookup[dc_yl] + columnofs[dc_x+1];
    
    fracstep = dc_iscale; 
    frac = dc_texturemid + (dc_yl-centery)*fracstep;
    
    do 
    {
	// Hack. Does not work corretly.
	*dest2 = *dest = dc_colormap[dc_source[(frac>>FRACBITS)&127]];
	dest += SCREENWIDTH;
	dest2 += SCREENWIDTH;
	frac += fracstep; 

    } while (count--);
}


//
// Spectre/Invisibility.
//
#define FUZZTABLE		50 
#define FUZZOFF	(SCREENWIDTH)


int	fuzzoffset[FUZZTABLE] =
{
    FUZZOFF,-FUZZOFF,FUZZOFF,-FUZZOFF,FUZZOFF,FUZZOFF,-FUZZOFF,
    FUZZOFF,FUZZOFF,-FUZZOFF,FUZZOFF,FUZZOFF,FUZZOFF,-FUZZOFF,
    FUZZOFF,FUZZOFF,FUZZOFF,-FUZZOFF,-FUZZOFF,-FUZZOFF,-FUZZOFF,
    FUZZOFF,-FUZZOFF,-FUZZOFF,FUZZOFF,FUZZOFF,FUZZOFF,FUZZOFF,-FUZZOFF,
    FUZZOFF,-FUZZOFF,FUZZOFF,FUZZOFF,-FUZZOFF,-FUZZOFF,FUZZOFF,
    FUZZOFF,-FUZZOFF,-FUZZOFF,-FUZZOFF,-FUZZOFF,FUZZOFF,FUZZOFF,
    FUZZOFF,FUZZOFF,-FUZZOFF,FUZZOFF,FUZZOFF,-FUZZOFF,FUZZOFF 
}; 

int	fuzzpos = 0; 


//
// Framebuffer postprocessing.
// Creates a fuzzy image by copying pixels
//  from adjacent ones to left and right.
// Used with an all black colormap, this
//  could create the SHADOW effect,
//  i.e. spectres and invisible players.
//
void R_DrawFuzzColumn (void) 
{ 
    int			count; 
    byte*		dest; 
    fixed_t		frac;
    fixed_t		fracstep;	 

    // Adjust borders. Low... 
    if (!dc_yl) 
	dc_yl = 1;

    // .. and high.
    if (dc_yh == viewheight-1) 
	dc_yh = viewheight - 2; 
		 
    count = dc_yh - dc_yl; 

    // Zero length.
    if (count < 0) 
	return; 

    
#ifdef RANGECHECK 
    if ((unsigned)dc_x >= SCREENWIDTH
	|| dc_yl < 0 || dc_yh >= SCREENHEIGHT)
    {
	I_Error ("R_DrawFuzzColumn: %i to %i at %i",
		 dc_yl, dc_yh, dc_x);
    }
#endif


    // Keep till detailshift bug in blocky mode fixed,
    //  or blocky mode removed.
    /* WATCOM code 
    if (detailshift)
    {
	if (dc_x & 1)
	{
	    outpw (GC_INDEX,GC_READMAP+(2<<8) ); 
	    outp (SC_INDEX+1,12); 
	}
	else
	{
	    outpw (GC_INDEX,GC_READMAP); 
	    outp (SC_INDEX+1,3); 
	}
	dest = destview + dc_yl*80 + (dc_x>>1); 
    }
    else
    {
	outpw (GC_INDEX,GC_READMAP+((dc_x&3)<<8) ); 
	outp (SC_INDEX+1,1<<(dc_x&3)); 
	dest = destview + dc_yl*80 + (dc_x>>2); 
    }*/

    
    // Does not work with blocky mode.
    dest = ylookup[dc_yl] + columnofs[dc_x];

    // Looks familiar.
    fracstep = dc_iscale; 
    frac = dc_texturemid + (dc_yl-centery)*fracstep; 

    // Looks like an attempt at dithering,
    //  using the colormap #6 (of 0-31, a bit
    //  brighter than average).
    do 
    {
	// Lookup framebuffer, and retrieve
	//  a pixel that is either one column
	//  left or right of the current one.
	// Add index from colormap to index.
	*dest = colormaps[6*256+dest[fuzzoffset[fuzzpos]]]; 

	// Clamp table lookup index.
	if (++fuzzpos == FUZZTABLE) 
	    fuzzpos = 0;
	
	dest += SCREENWIDTH;

	frac += fracstep; 
    } while (count--); 
} 
 
  
 

//
// R_DrawTranslatedColumn
// Used to draw player sprites
//  with the green colorramp mapped to others.
// Could be used with different translation
//  tables, e.g. the lighter colored version
//  of the BaronOfHell, the HellKnight, uses
//  identical sprites, kinda brightened up.
//
byte*	dc_translation;
byte*	translationtables;

void R_DrawTranslatedColumn (void) 
{ 
    int			count; 
    byte*		dest; 
    fixed_t		frac;
    fixed_t		fracstep;	 
 
    count = dc_yh - dc_yl; 
    if (count < 0) 
	return; 
				 
#ifdef RANGECHECK 
    if ((unsigned)dc_x >= SCREENWIDTH
	|| dc_yl < 0
	|| dc_yh >= SCREENHEIGHT)
    {
	I_Error ( "R_DrawColumn: %i to %i at %i",
		  dc_yl, dc_yh, dc_x);
    }
    
#endif 


    // WATCOM VGA specific.
    /* Keep for fixing.
    if (detailshift)
    {
	if (dc_x & 1)
	    outp (SC_INDEX+1,12); 
	else
	    outp (SC_INDEX+1,3);
	
	dest = destview + dc_yl*80 + (dc_x>>1); 
    }
    else
    {
	outp (SC_INDEX+1,1<<(dc_x&3)); 

	dest = destview + dc_yl*80 + (dc_x>>2); 
    }*/

    
    // FIXME. As above.
    dest = ylookup[dc_yl] + columnofs[dc_x]; 

    // Looks familiar.
    fracstep = dc_iscale; 
    frac = dc_texturemid + (dc_yl-centery)*fracstep; 

    // Here we do an additional index re-mapping.
    do 
    {
	// Translation tables are used
	//  to map certain colorramps to other ones,
	//  used with PLAY sprites.
	// Thus the "green" ramp of the player 0 sprite
	//  is mapped to gray, red, black/indigo. 
	*dest = dc_colormap[dc_translation[dc_source[frac>>FRACBITS]]];
	dest += SCREENWIDTH;
	
	frac += fracstep; 
    } while (count--); 
} 




//
// R_InitTranslationTables
// Creates the translation tables to map
//  the green color ramp to gray, brown, red.
// Assumes a given structure of the PLAYPAL.
// Could be read from a lump instead.
//
void R_InitTranslationTables (void)
{
    int		i;
	
    translationtables = Z_Malloc (256*3+255, PU_STATIC, 0);
    translationtables = (byte *)(( (int)translationtables + 255 )& ~255);
    
    // translate just the 16 green colors
    for (i=0 ; i<256 ; i++)
    {
	if (i >= 0x70 && i<= 0x7f)
	{
	    // map green ramp to gray, brown, red
	    translationtables[i] = 0x60 + (i&0xf);
	    translationtables [i+256] = 0x40 + (i&0xf);
	    translationtables [i+512] = 0x20 + (i&0xf);
	}
	else
	{
	    // Keep all other colors as is.
	    translationtables[i] = translationtables[i+256] 
		= translationtables[i+512] = i;
	}
    }
}




//
// R_DrawSpan 
// With DOOM style restrictions on view orientation,
//  the floors and ceilings consist of horizontal slices
//  or spans with constant z depth.
// However, rotation around the world z axis is possible,
//  thus this mapping, while simpler and faster than
//  perspective correct texture mapping, has to traverse
//  the texture at an angle in all but a few cases.
// In consequence, flats are not stored by column (like walls),
//  and the inner loop has to step in texture space u and v.
//
int			ds_y; 
int			ds_x1; 
int			ds_x2;

lighttable_t*		ds_colormap; 

fixed_t			ds_xfrac; 
fixed_t			ds_yfrac; 
fixed_t			ds_xstep; 
fixed_t			ds_ystep;

// start of a 64*64 tile image 
byte*			ds_source;	

// just for profiling
int			dscount;


//
// Draws the actual span.
void R_DrawSpan (void) 
{ 
    fixed_t		xfrac;
    fixed_t		yfrac; 
    byte*		dest; 
    int			count;
    int			spot; 
	 
#ifdef RANGECHECK 
    if (ds_x2 < ds_x1
	|| ds_x1<0
	|| ds_x2>=SCREENWIDTH  
	|| (unsigned)ds_y>SCREENHEIGHT)
    {
	I_Error( "R_DrawSpan: %i to %i at %i",
		 ds_x1,ds_x2,ds_y);
    }
//	dscount++; 
#endif 

    
    xfrac = ds_xfrac; 
    yfrac = ds_yfrac; 
	 
    dest = ylookup[ds_y] + columnofs[ds_x1];

    // We do not check for zero spans here?
    count = ds_x2 - ds_x1; 

    do 
    {
	// Current texture index in u,v.
	spot = ((yfrac>>(16-6))&(63*64)) + ((xfrac>>16)&63);

	// Lookup pixel from flat texture tile,
	//  re-index using light/colormap.
	*dest++ = ds_colormap[ds_source[spot]];

	// Next step in u,v.
	xfrac += ds_xstep; 
	yfrac += ds_ystep;
	
    } while (count--); 
} 



// UNUSED.
// Loop unrolled by 4.
#if 0
void R_DrawSpan (void) 
{ 
    unsigned	position, step;

    byte*	source;
    byte*	colormap;
    byte*	dest;
    
    unsigned	count;
    usingned	spot; 
    unsigned	value;
    unsigned	temp;
    unsigned	xtemp;
    unsigned	ytemp;
		
    position = ((ds_xfrac<<10)&0xffff0000) | ((ds_yfrac>>6)&0xffff);
    step = ((ds_xstep<<10)&0xffff0000) | ((ds_ystep>>6)&0xffff);
		
    source = ds_source;
    colormap = ds_colormap;
    dest = ylookup[ds_y] + columnofs[ds_x1];	 
    count = ds_x2 - ds_x1 + 1; 
	
    while (count >= 4) 
    { 
	ytemp = position>>4;
	ytemp = ytemp & 4032;
	xtemp = position>>26;
	spot = xtemp | ytemp;
	position += step;
	dest[0] = colormap[source[spot]]; 

	ytemp = position>>4;
	ytemp = ytemp & 4032;
	xtemp = position>>26;
	spot = xtemp | ytemp;
	position += step;
	dest[1] = colormap[source[spot]];
	
	ytemp = position>>4;
	ytemp = ytemp & 4032;
	xtemp = position>>26;
	spot = xtemp | ytemp;
	position += step;
	dest[2] = colormap[source[spot]];
	
	ytemp = position>>4;
	ytemp = ytemp & 4032;
	xtemp = position>>26;
	spot = xtemp | ytemp;
	position += step;
	dest[3] = colormap[source[spot]]; 
		
	count -= 4;
	dest += 4;
    } 
    while (count > 0) 
    { 
	ytemp = position>>4;
	ytemp = ytemp & 4032;
	xtemp = position>>26;
	spot = xtemp | ytemp;
	position += step;
	*dest++ = colormap[source[spot]]; 
	count--;
    } 
} 
#endif


//
// Again..
//
void R_DrawSpanLow (void) 
{ 
    fixed_t		xfrac;
    fixed_t		yfrac; 
    byte*		dest; 
    int			count;
    int			spot; 
	 
#ifdef RANGECHECK 
    if (ds_x2 < ds_x1
	|| ds_x1<0
	|| ds_x2>=SCREENWIDTH  
	|| (unsigned)ds_y>SCREENHEIGHT)
    {
	I_Error( "R_DrawSpan: %i to %i at %i",
		 ds_x1,ds_x2,ds_y);
    }
//	dscount++; 
#endif 
	 
    xfrac = ds_xfrac; 
    yfrac = ds_yfrac; 

    // Blocky mode, need to multiply by 2.
    ds_x1 <<= 1;
    ds_x2 <<= 1;
    
    dest = ylookup[ds_y] + columnofs[ds_x1];
  
    
    count = ds_x2 - ds_x1; 
    do 
    { 
	spot = ((yfrac>>(16-6))&(63*64)) + ((xfrac>>16)&63);
	// Lowres/blocky mode does it twice,
	//  while scale is adjusted appropriately.
	*dest++ = ds_colormap[ds_source[spot]]; 
	*dest++ = ds_colormap[ds_source[spot]];
	
	xfrac += ds_xstep; 
	yfrac += ds_ystep; 

    } while (count--); 
}

//
// R_InitBuffer 
// Creats lookup tables that avoid
//  multiplies and other hazzles
//  for getting the framebuffer address
//  of a pixel to draw.
//
void
R_InitBuffer
( int		width,
  int		height ) 
{ 
    int		i; 

    // Handle resize,
    //  e.g. smaller view windows
    //  with border and/or status bar.
    viewwindowx = (SCREENWIDTH-width) >> 1; 

    // Column offset. For windows.
    for (i=0 ; i<width ; i++) 
	columnofs[i] = viewwindowx + i;

    // Samw with base row offset.
    if (width == SCREENWIDTH) 
	viewwindowy = 0; 
    else 
	viewwindowy = (SCREENHEIGHT-SBARHEIGHT-height) >> 1; 

    // Preclaculate all row offsets.
    for (i=0 ; i<height ; i++) 
	ylookup[i] = screens[0] + (i+viewwindowy)*SCREENWIDTH; 
} 
 
 


//
// R_FillBackScreen
// Fills the back screen with a pattern
//  for variable screen sizes
// Also draws a beveled edge.
//
void R_FillBackScreen (void) 
{ 
    byte*	src;
    byte*	dest; 
    int		x;
    int		y; 
    patch_t*	patch;

    // DOOM border patch.
    char	name1[] = "FLOOR7_2";

    // DOOM II border patch.
    char	name2[] = "GRNROCK";	

    char*	name;
	
    if (scaledviewwidth == 320)
	return;
	
    if ( gamemode == commercial)
	name = name2;
    else
	name = name1;
    
    src = W_CacheLumpName (name, PU_CACHE); 
    dest = screens[1]; 
	 
    for (y=0 ; y<SCREENHEIGHT-SBARHEIGHT ; y++) 
    { 
	for (x=0 ; x<SCREENWIDTH/64 ; x++) 
	{ 
	    memcpy (dest, src+((y&63)<<6), 64); 
	    dest += 64; 
	} 

	if (SCREENWIDTH&63) 
	{ 
	    memcpy (dest, src+((y&63)<<6), SCREENWIDTH&63); 
	    dest += (SCREENWIDTH&63); 
	} 
    } 
	
    patch = W_CacheLumpName ("brdr_t",PU_CACHE);

    for (x=0 ; x<scaledviewwidth ; x+=8)
	V_DrawPatch (viewwindowx+x,viewwindowy-8,1,patch);
    patch = W_CacheLumpName ("brdr_b",PU_CACHE);

    for (x=0 ; x<scaledviewwidth ; x+=8)
	V_DrawPatch (viewwindowx+x,viewwindowy+viewheight,1,patch);
    patch = W_CacheLumpName ("brdr_l",PU_CACHE);

    for (y=0 ; y<viewheight ; y+=8)
	V_DrawPatch (viewwindowx-8,viewwindowy+y,1,patch);
    patch = W_CacheLumpName ("brdr_r",PU_CACHE);

    for (y=0 ; y<viewheight ; y+=8)
	V_DrawPatch (viewwindowx+scaledviewwidth,viewwindowy+y,1,patch);


    // Draw beveled edge. 
    V_DrawPatch (viewwindowx-8,
		 viewwindowy-8,
		 1,
		 W_CacheLumpName ("brdr_tl",PU_CACHE));
    
    V_DrawPatch (viewwindowx+scaledviewwidth,
		 viewwindowy-8,
		 1,
		 W_CacheLumpName ("brdr_tr",PU_CACHE));
    
    V_DrawPatch (viewwindowx-8,
		 viewwindowy+viewheight,
		 1,
		 W_CacheLumpName ("brdr_bl",PU_CACHE));
    
    V_DrawPatch (viewwindowx+scaledviewwidth,
		 viewwindowy+viewheight,
		 1,
		 W_CacheLumpName ("brdr_br",PU_CACHE));
} 
 

//
// Copy a screen buffer.
//
void
R_VideoErase
( unsigned	ofs,
  int		count ) 
{ 
  // LFB copy.
  // This might not be a good idea if memcpy
  //  is not optiomal, e.g. byte by byte on
  //  a 32bit CPU, as GNU GCC/Linux libc did
  //  at one point.
    memcpy (screens[0]+ofs, screens[1]+ofs, count); 
} 


//
// R_DrawViewBorder
// Draws the border around the view
//  for different size windows?
//
void
V_MarkRect
( int		x,
  int		y,
  int		width,
  int		height ); 
 
void R_DrawViewBorder (void) 
{ 
    int		top;
    int		side;
    int		ofs;
    int		i; 
 
    if (scaledviewwidth == SCREENWIDTH) 
	return; 
  
    top = ((SCREENHEIGHT-SBARHEIGHT)-viewheight)/2; 
    side = (SCREENWIDTH-scaledviewwidth)/2; 
 
    // copy top and one line of left side 
    R_VideoErase (0, top*SCREENWIDTH+side); 
 
    // copy one line of right side and bottom 
    ofs = (viewheight+top)*SCREENWIDTH-side; 
    R_VideoErase (ofs, top*SCREENWIDTH+side); 
 
    // copy sides using wraparound 
    ofs = top*SCREENWIDTH + SCREENWIDTH-side; 
    side <<= 1;
    
    for (i=1 ; i<viewheight ; i++) 
    { 
	R_VideoErase (ofs, side); 
	ofs += SCREENWIDTH; 
    } 

    // ?
    V_MarkRect (0,0,SCREENWIDTH, SCREENHEIGHT-SBARHEIGHT);
}


/*
 * ==========================================================================
 * PHASE 4: DIRECT 1-BIT RENDERING
 *
 * These functions write directly to the Mac 1-bit framebuffer, bypassing
 * the 320x200 8-bit intermediate buffer (screens[0]).  They are used during
 * gameplay (GS_LEVEL) when the view area is rendered direct-to-screen.
 *
 * Key pre-computed values (constant per column/span):
 *   fb_x      = dc_x + fb_mono_xoff          (absolute framebuffer x)
 *   byte_off  = fb_x >> 3                     (byte index in fb row)
 *   bit_set   = 0x80 >> (fb_x & 7)           (mask to SET   bit = black pixel)
 *   bit_clr   = ~bit_set                      (mask to CLEAR bit = white pixel)
 *   thresh[4] = bayer4x4[0..3][fb_x & 3]     (Bayer column for this x)
 *
 * On Mac 1-bit: bit 7 (MSB) of byte N is screen pixel x = N*8+0,
 *               bit 0 (LSB) is screen pixel x = N*8+7.
 * So "black pixel" (bit set) means the monitor renders it dark.
 * ==========================================================================
 */


/* ---- 68030 inner-pixel macros for direct 1-bit column renderers ----
 *
 * COLMONO_GRAY: compute gray level for one screen row.
 *   SWAP for frac>>16 avoids the multi-cycle ASR chain on 68030 (no barrel shifter).
 *   AND.W #127 also zero-clears D[15:8] so D.W is a safe index for the next MOVE.B.
 *   Two indexed MOVE.B: dc_source[] palette index, then mono_colormaps[] gray value.
 *
 * COLMONO_BIT: write one pixel via BSET/BCLR with a bit NUMBER (0-7).
 *   Using a bit number instead of a bitmask frees one D register vs OR.B/AND.B.
 *   That freed register holds a precomputed threshold, eliminating a stack load per row.
 *   CMP.B sets C if gray < thresh (unsigned); BCS → dark (BSET), else white (BCLR).
 *   bit_pos = 7 - (fb_x & 7)  (Mac MSB-first: pixel 0 = bit 7 of byte).
 */
#ifdef __m68k__
# define COLMONO_GRAY(_frac, _src, _cm, _gray)       \
    __asm__ ("move.l %1,%0\n\t"                        \
             "swap   %0\n\t"                           \
             "and.w  #127,%0\n\t"                      \
             "move.b (%2,%0.w),%0\n\t"                 \
             "move.b (%3,%0.w),%0"                     \
             : "=&d" (_gray)                           \
             : "d" (_frac), "a" (_src), "a" (_cm))
# define COLMONO_BIT(_gray, _th, _bp, _dst)                              \
    __asm__ __volatile__ ("cmp.b  %2,%0\n\t"                             \
                          "bcs.s  1f\n\t"                                 \
                          "bclr   %1,(%3)\n\t"                            \
                          "bra.s  2f\n\t"                                 \
                          "1: bset %1,(%3)\n\t"                           \
                          "2:"                                            \
                          : : "d" ((unsigned int)(_gray)),                \
                              "d" ((int)(_bp)),                           \
                              "d" ((unsigned int)(_th)),                  \
                              "a" (_dst)                                  \
                          : "cc", "memory")
#else /* C fallback for non-68k builds */
# define COLMONO_GRAY(_frac, _src, _cm, _gray) \
    (_gray) = (unsigned int)(_cm)[(_src)[((unsigned int)(_frac) >> FRACBITS) & 127]]
# define COLMONO_BIT(_gray, _th, _bp, _dst) do {                         \
    unsigned char _bit = (unsigned char)(1 << (_bp));                     \
    if ((_gray) < (unsigned int)(_th)) *(_dst) |= _bit;                  \
    else *(_dst) &= (unsigned char)~_bit; } while (0)
#endif /* __m68k__ */

/*
 * R_DrawColumn_Mono
 * High-detail wall column renderer → direct 1-bit framebuffer.
 *
 * Two paths selected by opt_halfline (default 0 = full-line):
 *
 * Full-line (opt_halfline=0):
 *  - Draws every screen row, Bayer-4 unrolled inner loop.
 *  - (unsigned int)frac >> FRACBITS: 68030 SWAP (4 cycles) vs slow ASR chain.
 *
 * Half-line (opt_halfline=1, enabled by -halfline arg):
 *  - Only draws even screen rows (odd rows pre-cleared to white = 0x00).
 *  - fracstep = dc_iscale<<1: advances 2 texture rows per screen step.
 *  - Only 2 Bayer thresholds (th_a, th_b) for the 2 even bayer_y values.
 *  - Unrolled 2-way loop: no branch or index in the pixel loop.
 *  - Cuts pixel RMW operations by 50%.
 */
void R_DrawColumn_Mono(void)
{
    int             count;
    unsigned char  *dst;
    unsigned char   bit_set, bit_clr;
    fixed_t         frac, fracstep;
    int             fb_x;
    const byte     *mono_cm;

    count = dc_yh - dc_yl;
    if (count < 0)
        return;

    fb_x    = dc_x + viewwindowx + fb_mono_xoff;
    bit_set = (unsigned char)(0x80 >> (fb_x & 7));
    bit_clr = (unsigned char)(~bit_set);
    mono_cm = no_lighting ? mono_colormaps
                          : mono_colormaps + (dc_colormap - colormaps);

    if (opt_halfline) {
        /* === Half-line path === */
        byte th_a, th_b;
        int  start_y, bayer_y0, nrows;

        start_y = dc_yl;
        if ((start_y + viewwindowy) & 1) { start_y++; count--; }
        if (count < 0) return;

        bayer_y0 = (start_y + viewwindowy) & 3;   /* 0 or 2 */
        th_a = bayer4x4[bayer_y0][fb_x & 3];
        th_b = bayer4x4[(bayer_y0 + 2) & 3][fb_x & 3];

        dst      = (unsigned char *)fb_mono_base
                   + (start_y + viewwindowy + fb_mono_yoff) * fb_mono_rowbytes
                   + (fb_x >> 3);
        fracstep = dc_iscale << 1;
        frac     = dc_texturemid + (start_y - centery) * dc_iscale;
        nrows    = (count >> 1) + 1;

        { int bit_pos = 7 - (fb_x & 7);  /* bit NUMBER for BSET/BCLR */
          unsigned int gray;
          while (nrows >= 2) {
              COLMONO_GRAY(frac, dc_source, mono_cm, gray);
              COLMONO_BIT(gray, th_a, bit_pos, dst);
              dst += fb_mono_rowbytes << 1; frac += fracstep;

              COLMONO_GRAY(frac, dc_source, mono_cm, gray);
              COLMONO_BIT(gray, th_b, bit_pos, dst);
              dst += fb_mono_rowbytes << 1; frac += fracstep;

              nrows -= 2;
          }
          if (nrows > 0) {
              COLMONO_GRAY(frac, dc_source, mono_cm, gray);
              COLMONO_BIT(gray, th_a, bit_pos, dst);
          }
        }
    } else {
        /* === Full-line path: all rows, Bayer-4 unrolled === */
        byte thresh[4];
        int  bayer_y;

        thresh[0] = bayer4x4[0][fb_x & 3];
        thresh[1] = bayer4x4[1][fb_x & 3];
        thresh[2] = bayer4x4[2][fb_x & 3];
        thresh[3] = bayer4x4[3][fb_x & 3];

        dst      = (unsigned char *)fb_mono_base
                   + (dc_yl + viewwindowy + fb_mono_yoff) * fb_mono_rowbytes
                   + (fb_x >> 3);
        fracstep = dc_iscale;
        frac     = dc_texturemid + (dc_yl - centery) * fracstep;
        bayer_y  = (dc_yl + viewwindowy) & 3;

        /* Align to Bayer-4 boundary */
        while (bayer_y != 0 && count >= 0) {
            byte gray = mono_cm[dc_source[((unsigned int)frac >> FRACBITS) & 127]];
            if (gray < thresh[bayer_y]) *dst |= bit_set; else *dst &= bit_clr;
            dst += fb_mono_rowbytes; frac += fracstep;
            bayer_y = (bayer_y + 1) & 3;
            count--;
        }
        /* Main loop: 4 rows at a time — bayer_y == 0, thresholds are constants */
        while (count >= 3) {
            byte gray;
            gray = mono_cm[dc_source[((unsigned int)frac >> FRACBITS) & 127]];
            if (gray < thresh[0]) *dst |= bit_set; else *dst &= bit_clr;
            dst += fb_mono_rowbytes; frac += fracstep;

            gray = mono_cm[dc_source[((unsigned int)frac >> FRACBITS) & 127]];
            if (gray < thresh[1]) *dst |= bit_set; else *dst &= bit_clr;
            dst += fb_mono_rowbytes; frac += fracstep;

            gray = mono_cm[dc_source[((unsigned int)frac >> FRACBITS) & 127]];
            if (gray < thresh[2]) *dst |= bit_set; else *dst &= bit_clr;
            dst += fb_mono_rowbytes; frac += fracstep;

            gray = mono_cm[dc_source[((unsigned int)frac >> FRACBITS) & 127]];
            if (gray < thresh[3]) *dst |= bit_set; else *dst &= bit_clr;
            dst += fb_mono_rowbytes; frac += fracstep;

            count -= 4;
        }
        /* Trailing rows */
        while (count-- >= 0) {
            byte gray = mono_cm[dc_source[((unsigned int)frac >> FRACBITS) & 127]];
            if (gray < thresh[bayer_y]) *dst |= bit_set; else *dst &= bit_clr;
            dst += fb_mono_rowbytes; frac += fracstep;
            bayer_y = (bayer_y + 1) & 3;
        }
    }
}


/*
 * R_DrawColumnLow_Mono
 * Low-detail wall column renderer → direct 1-bit framebuffer.
 * Each logical column is 2 screen pixels wide.
 */
void R_DrawColumnLow_Mono(void)
{
    int             count;
    unsigned char  *dst0, *dst1;
    unsigned char   bit_set0, bit_clr0, bit_set1, bit_clr1;
    fixed_t         frac, fracstep;
    byte            thresh0[4], thresh1[4];
    int             bayer_y;
    int             fb_x0, fb_x1;
    const lighttable_t *colormap;

    count = dc_yh - dc_yl;
    if (count < 0)
        return;

    /* In low-detail mode each logical column is 2 screen pixels wide.
     * Use a local variable to avoid clobbering dc_x — the sprite loop in
     * R_DrawVisSprite uses dc_x as its iterator: for (dc_x=x1; dc_x<=x2; dc_x++)
     * so modifying it here would cause all but the first column to be skipped. */
    int lx  = (dc_x << 1) + viewwindowx;

    fb_x0   = lx + fb_mono_xoff;
    fb_x1   = fb_x0 + 1;

    bit_set0 = (unsigned char)(0x80 >> (fb_x0 & 7));
    bit_clr0 = (unsigned char)(~bit_set0);
    bit_set1 = (unsigned char)(0x80 >> (fb_x1 & 7));
    bit_clr1 = (unsigned char)(~bit_set1);

    thresh0[0] = bayer4x4[0][fb_x0 & 3];
    thresh0[1] = bayer4x4[1][fb_x0 & 3];
    thresh0[2] = bayer4x4[2][fb_x0 & 3];
    thresh0[3] = bayer4x4[3][fb_x0 & 3];

    thresh1[0] = bayer4x4[0][fb_x1 & 3];
    thresh1[1] = bayer4x4[1][fb_x1 & 3];
    thresh1[2] = bayer4x4[2][fb_x1 & 3];
    thresh1[3] = bayer4x4[3][fb_x1 & 3];

    dst0 = (unsigned char *)fb_mono_base
           + (dc_yl + viewwindowy + fb_mono_yoff) * fb_mono_rowbytes
           + (fb_x0 >> 3);
    /* fb_x0 and fb_x1 differ by 1; they may share the same byte or not */
    dst1 = (unsigned char *)fb_mono_base
           + (dc_yl + viewwindowy + fb_mono_yoff) * fb_mono_rowbytes
           + (fb_x1 >> 3);

    {
        const byte *mono_cm = no_lighting ? mono_colormaps
                                          : mono_colormaps + (dc_colormap - colormaps);
        if (opt_halfline) {
            int start_y = dc_yl;
            int nrows;
            byte th0_a, th0_b, th1_a, th1_b;
            int bp0 = 7 - (fb_x0 & 7);   /* bit NUMBER for BSET/BCLR, pixel 0 */
            int bp1 = 7 - (fb_x1 & 7);   /* bit NUMBER for BSET/BCLR, pixel 1 */
            unsigned int gray;
            if ((start_y + viewwindowy) & 1) { start_y++; count--; }
            if (count < 0) return;
            bayer_y  = (start_y + viewwindowy) & 3;  /* 0 or 2 */
            /* Precompute both alternating threshold pairs — eliminates the
             * bayer_y index update and thresh[][bayer_y] stack loads from the loop. */
            th0_a = thresh0[bayer_y];           th1_a = thresh1[bayer_y];
            th0_b = thresh0[(bayer_y + 2) & 3]; th1_b = thresh1[(bayer_y + 2) & 3];
            fracstep = dc_iscale << 1;
            frac     = dc_texturemid + (start_y - centery) * dc_iscale;
            dst0 = (unsigned char *)fb_mono_base
                   + (start_y + viewwindowy + fb_mono_yoff) * fb_mono_rowbytes
                   + (fb_x0 >> 3);
            dst1 = (unsigned char *)fb_mono_base
                   + (start_y + viewwindowy + fb_mono_yoff) * fb_mono_rowbytes
                   + (fb_x1 >> 3);
            nrows = (count >> 1) + 1;
            /* Unrolled 2×: row A uses _a thresholds, row B uses _b thresholds. */
            while (nrows >= 2) {
                COLMONO_GRAY(frac, dc_source, mono_cm, gray);
                COLMONO_BIT(gray, th0_a, bp0, dst0);
                COLMONO_BIT(gray, th1_a, bp1, dst1);
                dst0 += fb_mono_rowbytes << 1; dst1 += fb_mono_rowbytes << 1;
                frac += fracstep;

                COLMONO_GRAY(frac, dc_source, mono_cm, gray);
                COLMONO_BIT(gray, th0_b, bp0, dst0);
                COLMONO_BIT(gray, th1_b, bp1, dst1);
                dst0 += fb_mono_rowbytes << 1; dst1 += fb_mono_rowbytes << 1;
                frac += fracstep;

                nrows -= 2;
            }
            if (nrows > 0) {
                COLMONO_GRAY(frac, dc_source, mono_cm, gray);
                COLMONO_BIT(gray, th0_a, bp0, dst0);
                COLMONO_BIT(gray, th1_a, bp1, dst1);
            }
        } else {
            fracstep = dc_iscale;
            frac     = dc_texturemid + (dc_yl - centery) * fracstep;
            bayer_y  = (dc_yl + viewwindowy) & 3;
            do {
                byte gray = mono_cm[dc_source[((unsigned int)frac >> FRACBITS) & 127]];
                if (gray < thresh0[bayer_y]) *dst0 |= bit_set0; else *dst0 &= bit_clr0;
                if (gray < thresh1[bayer_y]) *dst1 |= bit_set1; else *dst1 &= bit_clr1;
                dst0    += fb_mono_rowbytes;
                dst1    += fb_mono_rowbytes;
                frac    += fracstep;
                bayer_y  = (bayer_y + 1) & 3;
            } while (count--);
        }
    }
}


/*
 * R_DrawFuzzColumn_Mono
 * Spectre/invisibility effect → direct 1-bit framebuffer.
 * Simplified: uses a darkened colormap rather than reading neighbour pixels.
 */
void R_DrawFuzzColumn_Mono(void)
{
    int             count;
    unsigned char  *dst;
    unsigned char   bit_set, bit_clr;
    fixed_t         frac, fracstep;
    byte            thresh[4];
    int             bayer_y;
    int             fb_x;

    if (!dc_yl) dc_yl = 1;
    if (dc_yh == viewheight - 1) dc_yh = viewheight - 2;

    count = dc_yh - dc_yl;
    if (count < 0)
        return;

    fb_x    = dc_x + viewwindowx + fb_mono_xoff;
    bit_set = (unsigned char)(0x80 >> (fb_x & 7));
    bit_clr = (unsigned char)(~bit_set);

    thresh[0] = bayer4x4[0][fb_x & 3];
    thresh[1] = bayer4x4[1][fb_x & 3];
    thresh[2] = bayer4x4[2][fb_x & 3];
    thresh[3] = bayer4x4[3][fb_x & 3];

    dst = (unsigned char *)fb_mono_base
          + (dc_yl + viewwindowy + fb_mono_yoff) * fb_mono_rowbytes
          + (fb_x >> 3);

    /* Fuzz uses colormap 6 (slightly darkened) — pre-merge via mono_colormaps */
    {
        const byte *mono_cm = mono_colormaps + 6 * 256;
        if (opt_halfline) {
            int start_y = dc_yl;
            int nrows;
            if ((start_y + viewwindowy) & 1) { start_y++; count--; }
            if (count < 0) return;
            dst = (unsigned char *)fb_mono_base
                  + (start_y + viewwindowy + fb_mono_yoff) * fb_mono_rowbytes
                  + (fb_x >> 3);
            bayer_y  = (start_y + viewwindowy) & 3;
            fracstep = dc_iscale << 1;
            frac     = dc_texturemid + (start_y - centery) * dc_iscale;
            nrows    = (count >> 1) + 1;
            do {
                byte gray = mono_cm[dc_source[((unsigned int)frac >> FRACBITS) & 127]];
                if (gray < thresh[bayer_y]) *dst |= bit_set; else *dst &= bit_clr;
                dst     += fb_mono_rowbytes << 1;
                frac    += fracstep;
                bayer_y  = (bayer_y + 2) & 3;
            } while (--nrows > 0);
        } else {
            fracstep = dc_iscale;
            frac     = dc_texturemid + (dc_yl - centery) * fracstep;
            bayer_y  = (dc_yl + viewwindowy) & 3;
            do {
                byte gray = mono_cm[dc_source[((unsigned int)frac >> FRACBITS) & 127]];
                if (gray < thresh[bayer_y]) *dst |= bit_set; else *dst &= bit_clr;
                dst     += fb_mono_rowbytes;
                frac    += fracstep;
                bayer_y  = (bayer_y + 1) & 3;
            } while (count--);
        }
    }
}


/*
 * R_DrawTranslatedColumn_Mono
 * Player sprite color remapping → direct 1-bit framebuffer.
 */
void R_DrawTranslatedColumn_Mono(void)
{
    int             count;
    unsigned char  *dst;
    unsigned char   bit_set, bit_clr;
    fixed_t         frac, fracstep;
    byte            thresh[4];
    int             bayer_y;
    int             fb_x;
    const lighttable_t *colormap;

    count = dc_yh - dc_yl;
    if (count < 0)
        return;

    fb_x    = dc_x + viewwindowx + fb_mono_xoff;
    bit_set = (unsigned char)(0x80 >> (fb_x & 7));
    bit_clr = (unsigned char)(~bit_set);

    thresh[0] = bayer4x4[0][fb_x & 3];
    thresh[1] = bayer4x4[1][fb_x & 3];
    thresh[2] = bayer4x4[2][fb_x & 3];
    thresh[3] = bayer4x4[3][fb_x & 3];

    dst = (unsigned char *)fb_mono_base
          + (dc_yl + viewwindowy + fb_mono_yoff) * fb_mono_rowbytes
          + (fb_x >> 3);

    {
        const byte *mono_cm = no_lighting ? mono_colormaps
                                          : mono_colormaps + (dc_colormap - colormaps);
        if (opt_halfline) {
            int start_y = dc_yl;
            int nrows;
            if ((start_y + viewwindowy) & 1) { start_y++; count--; }
            if (count < 0) return;
            dst = (unsigned char *)fb_mono_base
                  + (start_y + viewwindowy + fb_mono_yoff) * fb_mono_rowbytes
                  + (fb_x >> 3);
            bayer_y  = (start_y + viewwindowy) & 3;
            fracstep = dc_iscale << 1;
            frac     = dc_texturemid + (start_y - centery) * dc_iscale;
            nrows    = (count >> 1) + 1;
            do {
                byte gray = mono_cm[dc_translation[dc_source[((unsigned int)frac >> FRACBITS) & 127]]];
                if (gray < thresh[bayer_y]) *dst |= bit_set; else *dst &= bit_clr;
                dst     += fb_mono_rowbytes << 1;
                frac    += fracstep;
                bayer_y  = (bayer_y + 2) & 3;
            } while (--nrows > 0);
        } else {
            fracstep = dc_iscale;
            frac     = dc_texturemid + (dc_yl - centery) * fracstep;
            bayer_y  = (dc_yl + viewwindowy) & 3;
            do {
                byte gray = mono_cm[dc_translation[dc_source[((unsigned int)frac >> FRACBITS) & 127]]];
                if (gray < thresh[bayer_y]) *dst |= bit_set; else *dst &= bit_clr;
                dst     += fb_mono_rowbytes;
                frac    += fracstep;
                bayer_y  = (bayer_y + 1) & 3;
            } while (count--);
        }
    }
}


/*
 * R_DrawSpan_Mono
 * Floor/ceiling span renderer → direct 1-bit framebuffer.
 * fb_mono_xoff is a multiple of 8, so the Doom screen left edge is
 * byte-aligned in the framebuffer.
 */
void R_DrawSpan_Mono(void)
{
    fixed_t            xfrac, yfrac;
    int                total;        /* total pixels remaining */
    unsigned char     *dst_row;
    int                x, fb_x_base;
    const byte        *mono_cm;
    const byte        *bayer_row;
    int                fb_y;

    /* Half-line: skip odd screen rows */
    if (opt_halfline && ((ds_y + viewwindowy) & 1))
        return;

    fb_y = ds_y + viewwindowy + fb_mono_yoff;
    {
        dst_row  = (unsigned char *)fb_mono_base + fb_y * fb_mono_rowbytes;
    }

    mono_cm   = no_lighting ? mono_colormaps
                            : mono_colormaps + (ds_colormap - colormaps);
    bayer_row = bayer4x4[(ds_y + viewwindowy) & 3];
    fb_x_base = viewwindowx + fb_mono_xoff;

    /* Solid floor: sample one texel and fill the span with a constant colour.
     * fb_x_base is a multiple of 8, and 8 mod 4 == 0, so every aligned byte in
     * the view area starts at Bayer column 0 — the fill byte is the same for all
     * fully-aligned bytes.  Partial edge bytes use the same pattern masked. */
    if (opt_solidfloor) {
        byte          gray  = mono_cm[ds_source[32*64 + 32]];
        int           x1_fb = ds_x1 + fb_x_base;
        int           x2_fb = ds_x2 + fb_x_base;
        int           b1    = x1_fb >> 3;
        int           b2    = x2_fb >> 3;
        unsigned char fill  = 0;
        unsigned char mask;

        if (gray < bayer_row[0]) fill |= 0x88;
        if (gray < bayer_row[1]) fill |= 0x44;
        if (gray < bayer_row[2]) fill |= 0x22;
        if (gray < bayer_row[3]) fill |= 0x11;

        if (b1 == b2) {
            /* Entire span within one byte */
            mask = (unsigned char)(0xFF >> (x1_fb & 7))
                 & (unsigned char)(0xFF << (7 - (x2_fb & 7)));
            dst_row[b1] = (dst_row[b1] & ~mask) | (fill & mask);
        } else {
            /* Leading partial byte */
            if (x1_fb & 7) {
                mask = (unsigned char)(0xFF >> (x1_fb & 7));
                dst_row[b1] = (dst_row[b1] & ~mask) | (fill & mask);
                b1++;
            }
            /* Trailing partial byte */
            if ((x2_fb & 7) != 7) {
                mask = (unsigned char)(0xFF << (7 - (x2_fb & 7)));
                dst_row[b2] = (dst_row[b2] & ~mask) | (fill & mask);
                b2--;
            }
            /* Aligned middle: single memset */
            if (b2 >= b1)
                memset(dst_row + b1, fill, (size_t)(b2 - b1 + 1));
        }
        return;
    }

    xfrac = ds_xfrac;
    yfrac = ds_yfrac;
    total = ds_x2 - ds_x1 + 1;
    x     = ds_x1;

    /* ---- Leading pixels: advance until fb_x is byte-aligned (fb_x & 7 == 0) ----
     * Handled one pixel at a time with read-modify-write.                          */
    while (total > 0 && ((x + fb_x_base) & 7) != 0) {
        int           fb_x  = x + fb_x_base;
        int           spot  = (((unsigned int)yfrac >> (16-6)) & (63*64))
                            + (((unsigned int)xfrac >> 16) & 63);
        byte          gray  = mono_cm[ds_source[spot]];
        unsigned char bmask = (unsigned char)(0x80 >> (fb_x & 7));
        if (gray < bayer_row[fb_x & 3]) dst_row[fb_x >> 3] |= bmask;
        else                             dst_row[fb_x >> 3] &= (unsigned char)(~bmask);
        xfrac += ds_xstep; yfrac += ds_ystep;
        x++; total--;
    }

    /* ---- Main loop: 8 pixels → 1 byte write ----------------------------------------
     * fb_x is byte-aligned here.  Since 8 is a multiple of 4 (the Bayer period),
     * every aligned group of 8 pixels starts at Bayer column 0:
     *   pixel 0..3 → bayer_row[0..3],  pixel 4..7 → bayer_row[0..3] (repeat).
     * This lets us use direct constants for the bit positions and Bayer columns.
     * Single *dst_byte = b write replaces 8 read-modify-write operations.           */
    while (total >= 8) {
        unsigned char *db = dst_row + ((x + fb_x_base) >> 3);
        unsigned char  b  = 0;
        int spot;

#define SPIX(BIT, BCOL) \
        spot = (((unsigned int)yfrac>>(16-6))&(63*64))+(((unsigned int)xfrac>>16)&63); \
        if (mono_cm[ds_source[spot]] < bayer_row[BCOL]) b |= (BIT); \
        xfrac += ds_xstep; yfrac += ds_ystep;

        SPIX(0x80, 0)  SPIX(0x40, 1)  SPIX(0x20, 2)  SPIX(0x10, 3)
        SPIX(0x08, 0)  SPIX(0x04, 1)  SPIX(0x02, 2)  SPIX(0x01, 3)
#undef SPIX

        *db = b;
        x += 8; total -= 8;
    }

    /* ---- Trailing pixels: same as leading ---- */
    while (total-- > 0) {
        int           fb_x  = x + fb_x_base;
        int           spot  = (((unsigned int)yfrac >> (16-6)) & (63*64))
                            + (((unsigned int)xfrac >> 16) & 63);
        byte          gray  = mono_cm[ds_source[spot]];
        unsigned char bmask = (unsigned char)(0x80 >> (fb_x & 7));
        if (gray < bayer_row[fb_x & 3]) dst_row[fb_x >> 3] |= bmask;
        else                             dst_row[fb_x >> 3] &= (unsigned char)(~bmask);
        xfrac += ds_xstep; yfrac += ds_ystep;
        x++;
    }
}


/*
 * R_DrawSpanLow_Mono
 * Low-detail floor/ceiling span renderer → direct 1-bit framebuffer.
 * ds_x1/ds_x2 arrive in low-detail coords (0..viewwidth-1).
 * Each logical pixel maps to 2 consecutive screen pixels.
 */
void R_DrawSpanLow_Mono(void)
{
    fixed_t            xfrac, yfrac;
    int                count;
    unsigned char     *dst_row;
    int                half_x, fb_x_base;
    const byte        *mono_cm;
    const byte        *bayer_row;   /* loop-invariant for this span y */

    /* Half-line: skip odd screen rows */
    if (opt_halfline && ((ds_y + viewwindowy) & 1))
        return;

    {
        int fb_y = ds_y + viewwindowy + fb_mono_yoff;
        dst_row  = (unsigned char *)fb_mono_base + fb_y * fb_mono_rowbytes;
    }

    mono_cm   = no_lighting ? mono_colormaps
                            : mono_colormaps + (ds_colormap - colormaps);
    bayer_row = bayer4x4[(ds_y + viewwindowy) & 3];
    fb_x_base = viewwindowx + fb_mono_xoff;

    /* Solid floor (low-detail): same logic as high-detail but each logical pixel
     * maps to 2 screen pixels — expand ds_x1/x2 to screen coords first. */
    if (opt_solidfloor) {
        byte          gray  = mono_cm[ds_source[32*64 + 32]];
        int           x1_fb = (ds_x1 << 1) + fb_x_base;
        int           x2_fb = (ds_x2 << 1) + 1 + fb_x_base;
        int           b1    = x1_fb >> 3;
        int           b2    = x2_fb >> 3;
        unsigned char fill  = 0;
        unsigned char mask;

        if (gray < bayer_row[0]) fill |= 0x88;
        if (gray < bayer_row[1]) fill |= 0x44;
        if (gray < bayer_row[2]) fill |= 0x22;
        if (gray < bayer_row[3]) fill |= 0x11;

        if (b1 == b2) {
            mask = (unsigned char)(0xFF >> (x1_fb & 7))
                 & (unsigned char)(0xFF << (7 - (x2_fb & 7)));
            dst_row[b1] = (dst_row[b1] & ~mask) | (fill & mask);
        } else {
            if (x1_fb & 7) {
                mask = (unsigned char)(0xFF >> (x1_fb & 7));
                dst_row[b1] = (dst_row[b1] & ~mask) | (fill & mask);
                b1++;
            }
            if ((x2_fb & 7) != 7) {
                mask = (unsigned char)(0xFF << (7 - (x2_fb & 7)));
                dst_row[b2] = (dst_row[b2] & ~mask) | (fill & mask);
                b2--;
            }
            if (b2 >= b1)
                memset(dst_row + b1, fill, (size_t)(b2 - b1 + 1));
        }
        return;
    }

    xfrac  = ds_xfrac;
    yfrac  = ds_yfrac;
    count  = ds_x2 - ds_x1;
    half_x = ds_x1;

    do {
        int           spot  = (((unsigned int)yfrac >> (16-6)) & (63*64))
                            + (((unsigned int)xfrac >> 16) & 63);
        byte          gray  = mono_cm[ds_source[spot]];
        int           fb_x0 = (half_x << 1) + fb_x_base;
        int           fb_x1 = fb_x0 + 1;
        int           b0    = fb_x0 >> 3;
        int           b1    = fb_x1 >> 3;
        unsigned char m0    = (unsigned char)(0x80 >> (fb_x0 & 7));
        unsigned char m1    = (unsigned char)(0x80 >> (fb_x1 & 7));

        if (gray < bayer_row[fb_x0 & 3]) dst_row[b0] |= m0; else dst_row[b0] &= (unsigned char)(~m0);
        if (gray < bayer_row[fb_x1 & 3]) dst_row[b1] |= m1; else dst_row[b1] &= (unsigned char)(~m1);

        xfrac  += ds_xstep;
        yfrac  += ds_ystep;
        half_x++;
    } while (count--);
}
