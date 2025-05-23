/*
 * Copyright © 2013  Google, Inc.
 *
 *  This is part of HarfBuzz, a text shaping library.
 *
 * Permission is hereby granted, without written agreement and without
 * license or royalty fees, to use, copy, modify, and distribute this
 * software and its documentation for any purpose, provided that the
 * above copyright notice and the following two paragraphs appear in
 * all copies of this software.
 *
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES
 * ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN
 * IF THE COPYRIGHT HOLDER HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 * THE COPYRIGHT HOLDER SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING,
 * BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS
 * ON AN "AS IS" BASIS, AND THE COPYRIGHT HOLDER HAS NO OBLIGATION TO
 * PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 *
 * Google Author(s): Behdad Esfahbod
 */

#ifndef HB_BUFFER_DESERIALIZE_JSON_HH
#define HB_BUFFER_DESERIALIZE_JSON_HH

#include "hb.hh"

%%{

machine deserialize_json;
alphtype unsigned char;
write data;

action clear_item {
	hb_memset (&info, 0, sizeof (info));
	hb_memset (&pos , 0, sizeof (pos ));
}

action add_item {
	buffer->add_info (info);
	if (unlikely (!buffer->successful))
	  return false;
	buffer->pos[buffer->len - 1] = pos;
	*end_ptr = p;
}

action tok {
	tok = p;
}

action ensure_glyphs { if (unlikely (!buffer->ensure_glyphs ())) return false; }
action ensure_unicode { if (unlikely (!buffer->ensure_unicode ())) return false; }

action parse_glyph_name {
	/* TODO Unescape \" and \\ if found. */
	if (!hb_font_glyph_from_string (font,
					tok+1, p - tok - 2, /* Skip "" */
					&info.codepoint))
	  return false;
}

action parse_codepoint	{ if (!parse_uint (tok, p, &info.codepoint)) return false; }
action parse_cluster	{ if (!parse_uint (tok, p, &info.cluster )) return false; }
action parse_x_offset	{ if (!parse_int  (tok, p, &pos.x_offset )) return false; }
action parse_y_offset	{ if (!parse_int  (tok, p, &pos.y_offset )) return false; }
action parse_x_advance	{ if (!parse_int  (tok, p, &pos.x_advance)) return false; }
action parse_y_advance	{ if (!parse_int  (tok, p, &pos.y_advance)) return false; }
action parse_glyph_flags{ if (!parse_uint (tok, p, &info.mask    )) return false; }

unum	= '0' | [1-9] digit*;
num	= '-'? unum;

comma = space* ',' space*;
colon = space* ':' space*;

codepoint = unum;
glyph_name = '"' ([^\\"] | '\\' [\\"])* '"';

parse_glyph_name   = (glyph_name >tok %parse_glyph_name);
parse_codepoint = (codepoint >tok %parse_codepoint);

glyph	=  "\"g\""  colon (parse_glyph_name | parse_codepoint);
unicode	=  "\"u\""  colon parse_codepoint;
cluster	=  "\"cl\"" colon (unum >tok %parse_cluster);
xoffset	=  "\"dx\"" colon (num  >tok %parse_x_offset);
yoffset	=  "\"dy\"" colon (num  >tok %parse_y_offset);
xadvance=  "\"ax\"" colon (num  >tok %parse_x_advance);
yadvance=  "\"ay\"" colon (num  >tok %parse_y_advance);
glyphflags="\"fl\"" colon (unum >tok %parse_glyph_flags);
# Not parsed. Ignored.
glyphextents="\""("xb"|"yb"|"w"|"h")"\"" colon (num >tok);

element = glyph @ensure_glyphs
	| unicode @ensure_unicode
	| cluster
	| xoffset
	| yoffset
	| xadvance
	| yadvance
	| glyphflags
	| glyphextents
	;
item	=
	( '{' space* element (comma element)* space* '}' space* (','|']') space* )
	>clear_item
	@add_item
	;

main := space* '['? space* item*;

}%%

static hb_bool_t
_hb_buffer_deserialize_json (hb_buffer_t *buffer,
				    const char *buf,
				    unsigned int buf_len,
				    const char **end_ptr,
				    hb_font_t *font)
{
  const char *p = buf, *pe = buf + buf_len;

  /* Ensure we have positions. */
  (void) hb_buffer_get_glyph_positions (buffer, nullptr);

  const char *tok = nullptr;
  int cs;
  hb_glyph_info_t info = {0};
  hb_glyph_position_t pos = {0};
  %%{
    write init;
    write exec;
  }%%

  *end_ptr = p;

  return p == pe;
}

#endif /* HB_BUFFER_DESERIALIZE_JSON_HH */
