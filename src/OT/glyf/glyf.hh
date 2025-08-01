#ifndef OT_GLYF_GLYF_HH
#define OT_GLYF_GLYF_HH


#include "../../hb-open-type.hh"
#include "../../hb-ot-head-table.hh"
#include "../../hb-ot-hmtx-table.hh"
#include "../../hb-ot-var-gvar-table.hh"
#include "../../hb-draw.hh"
#include "../../hb-paint.hh"

#include "glyf-helpers.hh"
#include "Glyph.hh"
#include "SubsetGlyph.hh"
#include "loca.hh"
#include "path-builder.hh"


namespace OT {


/*
 * glyf -- TrueType Glyph Data
 * https://docs.microsoft.com/en-us/typography/opentype/spec/glyf
 */
#define HB_OT_TAG_glyf HB_TAG('g','l','y','f')

struct glyf
{
  friend struct glyf_accelerator_t;

  static constexpr hb_tag_t tableTag = HB_OT_TAG_glyf;

  static bool has_valid_glyf_format(const hb_face_t* face)
  {
    const OT::head &head = *face->table.head;
    return head.indexToLocFormat <= 1 && head.glyphDataFormat <= 1;
  }

  bool sanitize (hb_sanitize_context_t *c HB_UNUSED) const
  {
    TRACE_SANITIZE (this);
    /* Runtime checks as eager sanitizing each glyph is costy */
    return_trace (true);
  }

  /* requires source of SubsetGlyph complains the identifier isn't declared */
  template <typename Iterator>
  bool serialize (hb_serialize_context_t *c,
		  Iterator it,
                  bool use_short_loca,
		  const hb_subset_plan_t *plan)
  {
    TRACE_SERIALIZE (this);

    unsigned init_len = c->length ();
    for (auto &_ : it)
      if (unlikely (!_.serialize (c, use_short_loca, plan)))
        return false;

    /* As a special case when all glyph in the font are empty, add a zero byte
     * to the table, so that OTS doesn’t reject it, and to make the table work
     * on Windows as well.
     * See https://github.com/khaledhosny/ots/issues/52 */
    if (init_len == c->length ())
    {
      HBUINT8 empty_byte;
      empty_byte = 0;
      c->copy (empty_byte);
    }
    return_trace (true);
  }

  /* Byte region(s) per glyph to output
     unpadded, hints removed if so requested
     If we fail to process a glyph we produce an empty (0-length) glyph */
  bool subset (hb_subset_context_t *c) const
  {
    TRACE_SUBSET (this);

    if (!has_valid_glyf_format (c->plan->source)) {
      // glyf format is unknown don't attempt to subset it.
      DEBUG_MSG (SUBSET, nullptr,
                 "unkown glyf format, dropping from subset.");
      return_trace (false);
    }

    hb_font_t *font = nullptr;
    if (c->plan->normalized_coords)
    {
      font = _create_font_for_instancing (c->plan);
      if (unlikely (!font))
	return_trace (false);
    }

    hb_vector_t<unsigned> padded_offsets;
    if (unlikely (!padded_offsets.alloc_exact (c->plan->new_to_old_gid_list.length)))
      return_trace (false);

    hb_vector_t<glyf_impl::SubsetGlyph> glyphs;
    if (!_populate_subset_glyphs (c->plan, font, glyphs))
    {
      hb_font_destroy (font);
      return_trace (false);
    }

    if (font)
      hb_font_destroy (font);

    unsigned max_offset = 0;
    for (auto &g : glyphs)
    {
      unsigned size = g.padded_size ();
      padded_offsets.push (size);
      max_offset += size;
    }

    bool use_short_loca = false;
    if (likely (!c->plan->force_long_loca))
      use_short_loca = max_offset < 0x1FFFF;

    if (!use_short_loca)
    {
      padded_offsets.resize (0);
      for (auto &g : glyphs)
	padded_offsets.push (g.length ());
    }

    auto *glyf_prime = c->serializer->start_embed <glyf> ();
    bool result = glyf_prime->serialize (c->serializer, hb_iter (glyphs), use_short_loca, c->plan);
    if (c->plan->normalized_coords && !c->plan->pinned_at_default)
      _free_compiled_subset_glyphs (glyphs);

    if (unlikely (!c->serializer->check_success (glyf_impl::_add_loca_and_head (c,
						 padded_offsets.iter (),
						 use_short_loca))))
      return_trace (false);

    return result;
  }

  bool
  _populate_subset_glyphs (const hb_subset_plan_t   *plan,
			   hb_font_t                *font,
			   hb_vector_t<glyf_impl::SubsetGlyph>& glyphs /* OUT */) const;

  hb_font_t *
  _create_font_for_instancing (const hb_subset_plan_t *plan) const;

  void _free_compiled_subset_glyphs (hb_vector_t<glyf_impl::SubsetGlyph> &glyphs) const
  {
    for (auto &g : glyphs)
      g.free_compiled_bytes ();
  }

  protected:
  UnsizedArrayOf<HBUINT8>
		dataZ;	/* Glyphs data. */
  public:
  DEFINE_SIZE_MIN (0);	/* In reality, this is UNBOUNDED() type; but since we always
			 * check the size externally, allow Null() object of it by
			 * defining it _MIN instead. */
};

struct glyf_accelerator_t
{
  glyf_accelerator_t (hb_face_t *face)
  {
    short_offset = false;
    num_glyphs = 0;
    loca_table = nullptr;
    glyf_table = nullptr;
#ifndef HB_NO_VAR
    gvar = nullptr;
#ifndef HB_NO_BEYOND_64K
    GVAR = nullptr;
#endif
#endif
    hmtx = nullptr;
#ifndef HB_NO_VERTICAL
    vmtx = nullptr;
#endif
    const OT::head &head = *face->table.head;
    if (!glyf::has_valid_glyf_format (face))
      /* Unknown format.  Leave num_glyphs=0, that takes care of disabling us. */
      return;
    short_offset = 0 == head.indexToLocFormat;

    loca_table = face->table.loca.get_blob (); // Needs no destruct!
    glyf_table = hb_sanitize_context_t ().reference_table<glyf> (face);
#ifndef HB_NO_VAR
    gvar = face->table.gvar;
#ifndef HB_NO_BEYOND_64K
    GVAR = face->table.GVAR;
#endif
#endif
    hmtx = face->table.hmtx;
#ifndef HB_NO_VERTICAL
    vmtx = face->table.vmtx;
#endif

    num_glyphs = hb_max (1u, loca_table.get_length () / (short_offset ? 2 : 4)) - 1;
    num_glyphs = hb_min (num_glyphs, face->get_num_glyphs ());
  }
  ~glyf_accelerator_t ()
  {
    auto *scratch = cached_scratch.get_relaxed ();
    if (scratch)
    {
      scratch->~hb_glyf_scratch_t ();
      hb_free (scratch);
    }

    glyf_table.destroy ();
  }

  bool has_data () const { return num_glyphs; }

  protected:
  template<typename T>
  bool get_points (hb_font_t *font, hb_codepoint_t gid, T consumer,
		   hb_array_t<const int> coords,
		   hb_glyf_scratch_t &scratch,
		   hb_scalar_cache_t *gvar_cache = nullptr) const
  {
    if (gid >= num_glyphs) return false;

    auto &all_points = scratch.all_points;
    all_points.resize (0);

    bool phantom_only = !consumer.is_consuming_contour_points ();
    if (unlikely (!glyph_for_gid (gid).get_points (font, *this, all_points, scratch, nullptr, nullptr, nullptr, true, true, phantom_only, coords, gvar_cache)))
      return false;

    unsigned count = all_points.length;
    assert (count >= glyf_impl::PHANTOM_COUNT);
    count -= glyf_impl::PHANTOM_COUNT;

    if (consumer.is_consuming_contour_points ())
    {
      auto *points = all_points.arrayZ;

      if (false)
      {
	/* Our path-builder was designed to work with this simple loop.
	 * But FreeType and CoreText do it differently, so we match those
	 * with the other, more complicated, code branch below. */
	for (unsigned i = 0; i < count; i++)
	{
	  consumer.consume_point (points[i]);
	  if (points[i].is_end_point)
	    consumer.contour_end ();
	}
      }
      else
      {
	for (unsigned i = 0; i < count; i++)
	{
	  // Start of a contour.
	  if (points[i].flag & glyf_impl::SimpleGlyph::FLAG_ON_CURVE)
	  {
	    // First point is on-curve. Draw the contour.
	    for (; i < count; i++)
	    {
	      consumer.consume_point (points[i]);
	      if (points[i].is_end_point)
	      {
		consumer.contour_end ();
		break;
	      }
	    }
	  }
	  else
	  {
	    unsigned start = i;

	    // Find end of the contour.
	    for (; i < count; i++)
	      if (points[i].is_end_point)
		break;

	    unsigned end = i;

	    // Enough to start from the end. Our path-builder takes care of the rest.
	    if (likely (end < count)) // Can only fail in case of alloc failure *maybe*.
	      consumer.consume_point (points[end]);

	    for (i = start; i < end; i++)
	      consumer.consume_point (points[i]);

	    consumer.contour_end ();
	  }
	}
      }

      consumer.points_end ();
    }

    /* Where to write phantoms, nullptr if not requested */
    contour_point_t *phantoms = consumer.get_phantoms_sink ();
    if (phantoms)
      for (unsigned i = 0; i < glyf_impl::PHANTOM_COUNT; ++i)
	phantoms[i] = all_points.arrayZ[count + i];

    return true;
  }

  public:

#ifndef HB_NO_VAR
  struct points_aggregator_t
  {
    hb_font_t *font;
    hb_glyph_extents_t *extents;
    contour_point_t *phantoms;
    bool scaled;

    struct contour_bounds_t
    {
      contour_bounds_t () { min_x = min_y = FLT_MAX; max_x = max_y = -FLT_MAX; }

      void add (const contour_point_t &p)
      {
	min_x = hb_min (min_x, p.x);
	min_y = hb_min (min_y, p.y);
	max_x = hb_max (max_x, p.x);
	max_y = hb_max (max_y, p.y);
      }

      bool empty () const { return (min_x >= max_x) || (min_y >= max_y); }

      void get_extents (hb_font_t *font, hb_glyph_extents_t *extents, bool scaled)
      {
	if (unlikely (empty ()))
	{
	  extents->width = 0;
	  extents->x_bearing = 0;
	  extents->height = 0;
	  extents->y_bearing = 0;
	  return;
	}
	{
	  extents->x_bearing = roundf (min_x);
	  extents->width = roundf (max_x - extents->x_bearing);
	  extents->y_bearing = roundf (max_y);
	  extents->height = roundf (min_y - extents->y_bearing);

	  if (scaled)
	    font->scale_glyph_extents (extents);
	}
      }

      protected:
      float min_x, min_y, max_x, max_y;
    } bounds;

    points_aggregator_t (hb_font_t *font_, hb_glyph_extents_t *extents_, contour_point_t *phantoms_, bool scaled_)
    {
      font = font_;
      extents = extents_;
      phantoms = phantoms_;
      scaled = scaled_;
      if (extents) bounds = contour_bounds_t ();
    }

    HB_ALWAYS_INLINE
    void consume_point (const contour_point_t &point) { bounds.add (point); }
    void contour_end () {}
    void points_end () { bounds.get_extents (font, extents, scaled); }

    bool is_consuming_contour_points () { return extents; }
    contour_point_t *get_phantoms_sink () { return phantoms; }
  };

#ifndef HB_NO_VAR
  unsigned
  get_advance_with_var_unscaled (hb_codepoint_t gid,
				 hb_font_t *font,
				 bool is_vertical,
				  hb_glyf_scratch_t &scratch,
				 hb_scalar_cache_t *gvar_cache = nullptr) const
  {
    if (unlikely (gid >= num_glyphs)) return 0;

    bool success = false;

    contour_point_t phantoms[glyf_impl::PHANTOM_COUNT];
    success = get_points (font, gid, points_aggregator_t (font, nullptr, phantoms, false),
			  hb_array (font->coords,
				    font->has_nonzero_coords ? font->num_coords : 0),
			  scratch, gvar_cache);
    if (unlikely (!success))
    {
      unsigned upem = font->face->get_upem ();
      return is_vertical ? upem : upem / 2;
    }

    float result = is_vertical
		 ? phantoms[glyf_impl::PHANTOM_TOP].y - phantoms[glyf_impl::PHANTOM_BOTTOM].y
		 : phantoms[glyf_impl::PHANTOM_RIGHT].x - phantoms[glyf_impl::PHANTOM_LEFT].x;
    return hb_clamp (roundf (result), 0.f, (float) UINT_MAX / 2);
  }

  float
  get_v_origin_with_var_unscaled (hb_codepoint_t gid,
				  hb_font_t *font,
				  hb_glyf_scratch_t &scratch,
				  hb_scalar_cache_t *gvar_cache = nullptr) const
  {
    if (unlikely (gid >= num_glyphs)) return 0;

    bool success = false;

    contour_point_t phantoms[glyf_impl::PHANTOM_COUNT];
    success = get_points (font, gid, points_aggregator_t (font, nullptr, phantoms, false),
			  hb_array (font->coords,
				    font->has_nonzero_coords ? font->num_coords : 0),
			  scratch, gvar_cache);
    if (unlikely (!success))
    {
      return font->face->get_upem ();
    }

    return phantoms[glyf_impl::PHANTOM_TOP].y;
  }
#endif
#endif

  public:

  bool get_extents (hb_font_t *font,
		    hb_codepoint_t gid,
		    hb_glyph_extents_t *extents) const
  { return get_extents_at (font, gid, extents, hb_array (font->coords,
							 font->has_nonzero_coords ? font->num_coords : 0)); }

  bool get_extents_at (hb_font_t *font,
		       hb_codepoint_t gid,
		       hb_glyph_extents_t *extents,
		       hb_array_t<const int> coords) const
  {
    if (unlikely (gid >= num_glyphs)) return false;

#ifndef HB_NO_VAR
    if (coords)
    {
      hb_glyf_scratch_t *scratch = acquire_scratch ();
      if (unlikely (!scratch)) return false;
      bool ret = get_points (font,
			     gid,
			     points_aggregator_t (font, extents, nullptr, true),
			     coords,
			     *scratch);
      release_scratch (scratch);
      return ret;
    }
#endif
    return glyph_for_gid (gid).get_extents_without_var_scaled (font, *this, extents);
  }

  const glyf_impl::Glyph
  glyph_for_gid (hb_codepoint_t gid, bool needs_padding_removal = false) const
  {
    if (unlikely (gid >= num_glyphs)) return glyf_impl::Glyph ();

    unsigned int start_offset, end_offset;

    if (short_offset)
    {
      const HBUINT16 *offsets = (const HBUINT16 *) loca_table->dataZ.arrayZ;
      start_offset = 2 * offsets[gid];
      end_offset   = 2 * offsets[gid + 1];
    }
    else
    {
      const HBUINT32 *offsets = (const HBUINT32 *) loca_table->dataZ.arrayZ;
      start_offset = offsets[gid];
      end_offset   = offsets[gid + 1];
    }

    if (unlikely (start_offset > end_offset || end_offset > glyf_table.get_length ()))
      return glyf_impl::Glyph ();

    glyf_impl::Glyph glyph (hb_bytes_t ((const char *) this->glyf_table + start_offset,
			     end_offset - start_offset), gid);
    return needs_padding_removal ? glyf_impl::Glyph (glyph.trim_padding (), gid) : glyph;
  }

  bool
  get_path (hb_font_t *font, hb_codepoint_t gid, hb_draw_session_t &draw_session, hb_scalar_cache_t *gvar_cache = nullptr) const
  {
    if (!has_data ()) return false;

    hb_glyf_scratch_t *scratch = acquire_scratch ();
    if (unlikely (!scratch)) return true;

    bool ret = get_points (font, gid, glyf_impl::path_builder_t (font, draw_session),
			   hb_array (font->coords,
				     font->has_nonzero_coords ? font->num_coords : 0),
			   *scratch,
			    gvar_cache);

    release_scratch (scratch);

    return ret;
  }

  bool
  get_path_at (hb_font_t *font, hb_codepoint_t gid, hb_draw_session_t &draw_session,
	       hb_array_t<const int> coords,
	       hb_glyf_scratch_t &scratch,
	       hb_scalar_cache_t *gvar_cache = nullptr) const
  {
    if (!has_data ()) return false;
    return get_points (font, gid, glyf_impl::path_builder_t (font, draw_session),
		       coords,
		       scratch,
		       gvar_cache);
  }


  hb_glyf_scratch_t *acquire_scratch () const
  {
    if (!has_data ()) return nullptr;
    hb_glyf_scratch_t *scratch = cached_scratch.get_acquire ();
    if (!scratch || unlikely (!cached_scratch.cmpexch (scratch, nullptr)))
    {
      scratch = (hb_glyf_scratch_t *) hb_calloc (1, sizeof (hb_glyf_scratch_t));
      if (unlikely (!scratch))
	return nullptr;
    }
    return scratch;
  }
  void release_scratch (hb_glyf_scratch_t *scratch) const
  {
    if (!scratch)
      return;
    if (!cached_scratch.cmpexch (nullptr, scratch))
    {
      scratch->~hb_glyf_scratch_t ();
      hb_free (scratch);
    }
  }

#ifndef HB_NO_VAR
  const gvar_accelerator_t *gvar;
#ifndef HB_NO_BEYOND_64K
  const GVAR_accelerator_t *GVAR;
#endif
#endif
  const hmtx_accelerator_t *hmtx;
#ifndef HB_NO_VERTICAL
  const vmtx_accelerator_t *vmtx;
#endif

  private:
  bool short_offset;
  unsigned int num_glyphs;
  hb_blob_ptr_t<loca> loca_table;
  hb_blob_ptr_t<glyf> glyf_table;
  mutable hb_atomic_t<hb_glyf_scratch_t *> cached_scratch;
};


inline bool
glyf::_populate_subset_glyphs (const hb_subset_plan_t   *plan,
			       hb_font_t *font,
			       hb_vector_t<glyf_impl::SubsetGlyph>& glyphs /* OUT */) const
{
  OT::glyf_accelerator_t glyf (plan->source);
  if (!glyphs.alloc_exact (plan->new_to_old_gid_list.length)) return false;

  for (const auto &pair : plan->new_to_old_gid_list)
  {
    hb_codepoint_t new_gid = pair.first;
    hb_codepoint_t old_gid = pair.second;
    glyf_impl::SubsetGlyph *p = glyphs.push ();
    glyf_impl::SubsetGlyph& subset_glyph = *p;
    subset_glyph.old_gid = old_gid;

    if (unlikely (old_gid == 0 && new_gid == 0 &&
                  !(plan->flags & HB_SUBSET_FLAGS_NOTDEF_OUTLINE)) &&
                  !plan->normalized_coords)
      subset_glyph.source_glyph = glyf_impl::Glyph ();
    else
    {
      /* If plan has an accelerator, the preprocessing step already trimmed glyphs.
       * Don't trim them again! */
      subset_glyph.source_glyph = glyf.glyph_for_gid (subset_glyph.old_gid, !plan->accelerator);
    }

    if (plan->flags & HB_SUBSET_FLAGS_NO_HINTING)
      subset_glyph.drop_hints_bytes ();
    else
      subset_glyph.dest_start = subset_glyph.source_glyph.get_bytes ();

    if (font)
    {
      if (unlikely (!subset_glyph.compile_bytes_with_deltas (plan, font, glyf)))
      {
        // when pinned at default, only bounds are updated, thus no need to free
        if (!plan->pinned_at_default)
          _free_compiled_subset_glyphs (glyphs);
        return false;
      }
    }
  }
  return true;
}

inline hb_font_t *
glyf::_create_font_for_instancing (const hb_subset_plan_t *plan) const
{
  hb_font_t *font = hb_font_create (plan->source);
  if (unlikely (font == hb_font_get_empty ())) return nullptr;

  hb_vector_t<hb_variation_t> vars;
  if (unlikely (!vars.alloc (plan->user_axes_location.get_population (), true)))
  {
    hb_font_destroy (font);
    return nullptr;
  }

  for (auto _ : plan->user_axes_location)
  {
    hb_variation_t var;
    var.tag = _.first;
    var.value = _.second.middle;
    vars.push (var);
  }

#ifndef HB_NO_VAR
  hb_font_set_variations (font, vars.arrayZ, plan->user_axes_location.get_population ());
#endif
  return font;
}


} /* namespace OT */


#endif /* OT_GLYF_GLYF_HH */
