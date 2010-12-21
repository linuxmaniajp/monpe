/* Dia -- an diagram creation/manipulation program
 * Copyright (C) 1998 Alexander Larsson
 * Association updates Copyright(c) 2004 David Klotzbach
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 *
 */

/*--------------------------------------------------------------------------**
** In the fall of 2004, I started to use the UML portion of this dia        **
** program in earnest. I had been using several commercial programs in my   **
** work and wanted something I could use for my "home" projects. Even       **
** though Dia was advertised has having complete support for the UML static **
** structures, I found that I was used to having a design tool that was     **
** much closer to the language as described by the OMG and the Three        **
** Amigos.                                                                  **
** Always willing to give back to the culture that has supported me for the **
** last 30+ years, I have endeavored to make the UML portion of Dia more    **
** compliant with the standard as is is currently described.                **
** My changes do not include any of the enhancements as described in        **
** UML 2.0.                                                                 **
**                                                                          **
** Association change Oct 31, 2004 - Dave Klotzbach                         **
** dklotzbach@foxvalley.net                                                 **
**                                                                          **
** Now for a description of what I have done here in Association. To begin  **
** with, the implementation of Association is darn near complete. However,  **
** as described in "The Unified Modeling Language Users Guide", the roles   **
** that an association may have do have "visibility" attributes, and these  **
** attributes apply independently to each of the roles.                     **
**                                                                          **
** What is still missing from this version are the concepts of              **
** "Qualification", "Interface Specifier" and "Association Classes"         **
**--------------------------------------------------------------------------*/

/* DO NOT USE THIS OBJECT AS A BASIS FOR A NEW OBJECT. */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <assert.h>
#include <math.h>
#include <string.h>

#include "intl.h"
#include "object.h"
#include "orth_conn.h"
#include "diarenderer.h"
#include "attributes.h"
#include "arrows.h"
#include "uml.h"

#include "properties.h"

#include "pixmaps/association.xpm"

extern char visible_char[];	/* The definitions are in UML.c. Used here to avoid getting out of sync */
extern PropEnumData _uml_visibilities[];

typedef struct _Association Association;
typedef struct _AssociationState AssociationState;
typedef struct _AssociationPropertiesDialog AssociationPropertiesDialog;

typedef enum {
  ASSOC_NODIR,
  ASSOC_RIGHT, /* the diamond is on the left side and the arrow points right */
  ASSOC_LEFT
} AssociationDirection;

typedef enum {
  AGGREGATE_NONE,
  AGGREGATE_NORMAL, /* filled diamond */
  AGGREGATE_COMPOSITION /* hollow diamond */
} AggregateType;

typedef struct _AssociationEnd {
  gchar *role; /* Can be NULL */
  gchar *multiplicity; /* Can be NULL */
  Point text_pos;
  real text_width;
  real role_ascent;
  real role_descent;
  real multi_ascent;
  real multi_descent;
  Alignment text_align;
  UMLVisibility visibility;	/* This value is only relevant if role is not null */
  
  int arrow;
  AggregateType aggregate; /* Note: Can only be != NONE on ONE side! */
} AssociationEnd;

struct _AssociationState {
  ObjectState obj_state;

  gchar *name;
  AssociationDirection direction;

  struct {
    gchar *role;
    gchar *multiplicity;
    UMLVisibility visibility;	/* This value is only relevant if role is not null */

    int arrow;
    AggregateType aggregate;
  } end[2];
};


struct _Association {
  OrthConn orth;

  Point text_pos;
  Alignment text_align;
  real text_width;
  real ascent;
  real descent;
    
  gchar *name;
  AssociationDirection direction;
  AggregateType assoc_type;
  
  gboolean show_direction;

  AssociationEnd end[2];
  
  Color text_color;
  Color line_color;
};

#define ASSOCIATION_WIDTH 0.1
#define ASSOCIATION_TRIANGLESIZE 0.8
#define ASSOCIATION_DIAMONDLEN 1.4
#define ASSOCIATION_DIAMONDWIDTH 0.7
#define ASSOCIATION_FONTHEIGHT 0.8
#define ASSOCIATION_END_SPACE 0.2
static DiaFont *assoc_font = NULL;

static real association_distance_from(Association *assoc, Point *point);
static void association_select(Association *assoc, Point *clicked_point,
			       DiaRenderer *interactive_renderer);
static ObjectChange* association_move_handle(Association *assoc, Handle *handle,
					     Point *to, ConnectionPoint *cp,
					     HandleMoveReason reason, ModifierKeys modifiers);
static ObjectChange* association_move(Association *assoc, Point *to);
static void association_draw(Association *assoc, DiaRenderer *renderer);
static DiaObject *association_create(Point *startpoint,
				  void *user_data,
				  Handle **handle1,
				  Handle **handle2);
static void association_destroy(Association *assoc);
static DiaObject *association_copy(Association *assoc);
static DiaMenu *association_get_object_menu(Association *assoc,
					    Point *clickedpoint);
static PropDescription *association_describe_props(Association *assoc);
static void association_get_props(Association *assoc, GPtrArray *props);
static void association_set_props(Association *assoc, GPtrArray *props);

static AssociationState *association_get_state(Association *assoc);
static void association_set_state(Association *assoc,
				  AssociationState *state);

static DiaObject *association_load(ObjectNode obj_node, int version,
				const char *filename);

static void association_update_data(Association *assoc);
static coord get_aggregate_pos_diff(AssociationEnd *end);

static ObjectTypeOps association_type_ops =
{
  (CreateFunc) association_create,
  (LoadFunc)   association_load,
  (SaveFunc)   object_save_using_properties
};

DiaObjectType association_type =
{
  "UML - Association",   /* name */
  /* Version 0 had no autorouting and so shouldn't have it set by default. */
  /* Version 1 was saving both ends separately without using StdProps */
  2,                      /* version */
  (char **) association_xpm,  /* pixmap */
  
  &association_type_ops,      /* ops */
  NULL,                 /* pixmap_file */
  0                     /* default_user_data */
};

static ObjectOps association_ops = {
  (DestroyFunc)         association_destroy,
  (DrawFunc)            association_draw,
  (DistanceFunc)        association_distance_from,
  (SelectFunc)          association_select,
  (CopyFunc)            association_copy,
  (MoveFunc)            association_move,
  (MoveHandleFunc)      association_move_handle,
  (GetPropertiesFunc)   object_create_props_dialog,
  (ApplyPropertiesDialogFunc) object_apply_props_from_dialog,
  (ObjectMenuFunc)      association_get_object_menu,
  (DescribePropsFunc)   association_describe_props,
  (GetPropsFunc)        association_get_props,
  (SetPropsFunc)        association_set_props,
  (TextEditFunc) 0,
  (ApplyPropertiesListFunc) object_apply_props,
};

static PropEnumData prop_assoc_direction_data[] = {
  { N_("None"), ASSOC_NODIR },
  { N_("From A to B"), ASSOC_RIGHT },
  { N_("From B to A"), ASSOC_LEFT },
  { NULL, 0 }
};
static PropEnumData prop_assoc_type_data[] = {
  { N_("None"), AGGREGATE_NONE },
  { N_("Aggregation"), AGGREGATE_NORMAL },
  { N_("Composition"), AGGREGATE_COMPOSITION },
  { NULL, 0 }
};

static PropDescription association_props[] = {
  { "name", PROP_TYPE_STRING, PROP_FLAG_VISIBLE, N_("Name"), NULL, NULL },
  { "direction", PROP_TYPE_ENUM, PROP_FLAG_VISIBLE, 
    N_("Direction"), NULL, prop_assoc_direction_data },
  { "show_direction", PROP_TYPE_BOOL, PROP_FLAG_VISIBLE|PROP_FLAG_OPTIONAL|PROP_FLAG_NO_DEFAULTS, 
    N_("Show direction"), N_("Show the small arrow denoting the reading direction"), 0 },
  { "assoc_type", PROP_TYPE_ENUM, PROP_FLAG_VISIBLE|PROP_FLAG_OPTIONAL|PROP_FLAG_NO_DEFAULTS, 
    N_("Type"), NULL, prop_assoc_type_data },

  PROP_MULTICOL_BEGIN("sides"),
  PROP_MULTICOL_COLUMN("side_a"),
  { "help", PROP_TYPE_STATIC, PROP_FLAG_VISIBLE|PROP_FLAG_DONT_SAVE|PROP_FLAG_DONT_MERGE,
    N_(" "), N_("Side A") },
  { "role_a", PROP_TYPE_STRING, PROP_FLAG_VISIBLE|PROP_FLAG_OPTIONAL, 
    N_("Role"), NULL, NULL },
  { "multipicity_a", PROP_TYPE_STRING, PROP_FLAG_VISIBLE|PROP_FLAG_OPTIONAL, 
    N_("Multiplicity"), NULL, NULL },
  { "visibility_a", PROP_TYPE_ENUM, PROP_FLAG_VISIBLE|PROP_FLAG_OPTIONAL, 
    N_("Visibility"), NULL, _uml_visibilities },
  { "show_arrow_a", PROP_TYPE_BOOL, PROP_FLAG_VISIBLE|PROP_FLAG_OPTIONAL, 
    N_("Show arrow"), NULL, 0 },
  PROP_MULTICOL_COLUMN("side_b"),
  { "help", PROP_TYPE_STATIC, PROP_FLAG_VISIBLE|PROP_FLAG_DONT_SAVE|PROP_FLAG_DONT_MERGE,
    N_(" "), N_("Side B") },
  { "role_b", PROP_TYPE_STRING, PROP_FLAG_VISIBLE|PROP_FLAG_OPTIONAL, 
    N_(" "), NULL, NULL },
  { "multipicity_b", PROP_TYPE_STRING, PROP_FLAG_VISIBLE|PROP_FLAG_OPTIONAL, 
    N_(" "), NULL, NULL },
  { "visibility_b", PROP_TYPE_ENUM, PROP_FLAG_VISIBLE|PROP_FLAG_OPTIONAL, 
    N_(" "), NULL, _uml_visibilities },
  { "show_arrow_b", PROP_TYPE_BOOL, PROP_FLAG_VISIBLE|PROP_FLAG_OPTIONAL, 
    N_(" "), NULL, 0 },
  PROP_MULTICOL_END("sides"),

  ORTHCONN_COMMON_PROPERTIES,
  /* can't use PROP_STD_TEXT_COLOUR_OPTIONAL cause it has PROP_FLAG_DONT_SAVE. It is designed to fill the Text object - not some subset */
  PROP_STD_TEXT_COLOUR_OPTIONS(PROP_FLAG_VISIBLE|PROP_FLAG_STANDARD|PROP_FLAG_OPTIONAL),
  PROP_STD_LINE_COLOUR_OPTIONAL, 
  
  PROP_DESC_END
};

static PropOffset association_offsets[] = {
  { "name", PROP_TYPE_STRING, offsetof(Association, name) },
  { "direction", PROP_TYPE_ENUM, offsetof(Association, direction) },
  { "assoc_type", PROP_TYPE_ENUM, offsetof(Association, assoc_type) },
  { "show_direction", PROP_TYPE_BOOL, offsetof(Association, show_direction) },

  PROP_OFFSET_MULTICOL_BEGIN("sides"),
  PROP_OFFSET_MULTICOL_COLUMN("side_a"),
  { "role_a", PROP_TYPE_STRING, offsetof(Association, end[0].role) },
  { "multipicity_a", PROP_TYPE_STRING, offsetof(Association, end[0].multiplicity) }, 
  { "visibility_a", PROP_TYPE_ENUM, offsetof(Association, end[0].visibility) },
  { "show_arrow_a", PROP_TYPE_BOOL, offsetof(Association, end[0].arrow) },

  PROP_OFFSET_MULTICOL_COLUMN("side_a"),
  { "role_b", PROP_TYPE_STRING, offsetof(Association, end[1].role) },
  { "multipicity_b", PROP_TYPE_STRING, offsetof(Association, end[1].multiplicity) }, 
  { "visibility_b", PROP_TYPE_ENUM, offsetof(Association, end[1].visibility) },
  { "show_arrow_b", PROP_TYPE_BOOL, offsetof(Association, end[1].arrow) },
  PROP_OFFSET_MULTICOL_END("sides"),

  ORTHCONN_COMMON_PROPERTIES_OFFSETS,
  { "line_colour",PROP_TYPE_COLOUR,offsetof(Association, line_color) },
  { "text_colour", PROP_TYPE_COLOUR, offsetof(Association, text_color) },
  { NULL, 0, 0 }
};

static PropDescription *
association_describe_props(Association *assoc)
{
  if (association_props[0].quark == 0) {
    prop_desc_list_calculate_quarks(association_props);
  }
  return association_props;
}

static void
association_get_props(Association *assoc, GPtrArray *props)
{
  object_get_props_from_offsets(&assoc->orth.object,
                                association_offsets,props);
}

static void
association_set_props(Association *assoc, GPtrArray *props)
{
  object_set_props_from_offsets(&assoc->orth.object, 
                                association_offsets, props);
  /* force an internal state update after changing properties */
  association_set_state(assoc, association_get_state(assoc));
  association_update_data(assoc);
}

static real
association_distance_from(Association *assoc, Point *point)
{
  OrthConn *orth = &assoc->orth;
  return orthconn_distance_from(orth, point, ASSOCIATION_WIDTH);
}

static void
association_select(Association *assoc, Point *clicked_point,
		  DiaRenderer *interactive_renderer)
{
  orthconn_update_data(&assoc->orth);
}

static ObjectChange*
association_move_handle(Association *assoc, Handle *handle,
			Point *to, ConnectionPoint *cp,
			HandleMoveReason reason, ModifierKeys modifiers)
{
  ObjectChange *change;
  assert(assoc!=NULL);
  assert(handle!=NULL);
  assert(to!=NULL);
  
  change = orthconn_move_handle(&assoc->orth, handle, to, cp, reason, modifiers);
  association_update_data(assoc);

  return change;
}

static ObjectChange*
association_move(Association *assoc, Point *to)
{
  ObjectChange *change;

  change = orthconn_move(&assoc->orth, to);
  association_update_data(assoc);

  return change;
}

/** calculate the point of the small triangle for show_direction  */
static gboolean
assoc_get_direction_poly (Association *assoc, Point* poly)
{
  if (assoc->show_direction) {
    if (assoc->direction == ASSOC_RIGHT) {
      poly[0].x = assoc->text_pos.x + assoc->text_width + 0.1;
      if (assoc->text_align == ALIGN_CENTER)
        poly[0].x -= assoc->text_width/2.0;
      poly[0].y = assoc->text_pos.y;
      poly[1].x = poly[0].x;
      poly[1].y = poly[0].y - ASSOCIATION_FONTHEIGHT*0.5;
      poly[2].x = poly[0].x + ASSOCIATION_FONTHEIGHT*0.5;
      poly[2].y = poly[0].y - ASSOCIATION_FONTHEIGHT*0.5*0.5;
      return TRUE;
    } else if (assoc->direction == ASSOC_LEFT) {
      poly[0].x = assoc->text_pos.x - 0.2;
      if (assoc->text_align == ALIGN_CENTER)
        poly[0].x -= assoc->text_width/2.0;
      poly[0].y = assoc->text_pos.y;
      poly[1].x = poly[0].x;
      poly[1].y = poly[0].y - ASSOCIATION_FONTHEIGHT*0.5;
      poly[2].x = poly[0].x - ASSOCIATION_FONTHEIGHT*0.5;
      poly[2].y = poly[0].y - ASSOCIATION_FONTHEIGHT*0.5*0.5;
      return TRUE;
    }
  }
  return FALSE;
}

static void
association_draw(Association *assoc, DiaRenderer *renderer)
{
  DiaRendererClass *renderer_ops = DIA_RENDERER_GET_CLASS (renderer);
  OrthConn *orth = &assoc->orth;
  Point *points;
  Point poly[3];
  int n,i;
  Point pos;
  Arrow startarrow, endarrow;
  
  points = &orth->points[0];
  n = orth->numpoints;
  
  renderer_ops->set_linewidth(renderer, ASSOCIATION_WIDTH);
  renderer_ops->set_linestyle(renderer, LINESTYLE_SOLID);
  renderer_ops->set_linejoin(renderer, LINEJOIN_MITER);
  renderer_ops->set_linecaps(renderer, LINECAPS_BUTT);
  
  startarrow.length = ASSOCIATION_TRIANGLESIZE;
  startarrow.width = ASSOCIATION_TRIANGLESIZE;
  if (assoc->end[0].arrow) {
    startarrow.type = ARROW_LINES;
  } else if (assoc->end[0].aggregate != AGGREGATE_NONE) {
    startarrow.length = ASSOCIATION_DIAMONDLEN;
    startarrow.width = ASSOCIATION_TRIANGLESIZE*0.6;
    startarrow.type = assoc->end[0].aggregate == AGGREGATE_NORMAL ?
      ARROW_HOLLOW_DIAMOND : ARROW_FILLED_DIAMOND;
  } else {
    startarrow.type = ARROW_NONE;
  }
  endarrow.length = ASSOCIATION_TRIANGLESIZE;
  endarrow.width = ASSOCIATION_TRIANGLESIZE;
  if (assoc->end[1].arrow) {
    endarrow.type = ARROW_LINES;
  } else if (assoc->end[1].aggregate != AGGREGATE_NONE) {
    endarrow.length = ASSOCIATION_DIAMONDLEN;
    endarrow.width = ASSOCIATION_TRIANGLESIZE*0.6;
    endarrow.type = assoc->end[1].aggregate == AGGREGATE_NORMAL ?
      ARROW_HOLLOW_DIAMOND : ARROW_FILLED_DIAMOND;
  } else {
    endarrow.type = ARROW_NONE;
  }
  renderer_ops->draw_polyline_with_arrows(renderer, points, n,
					   ASSOCIATION_WIDTH,
					   &assoc->line_color,
					   &startarrow, &endarrow);

  /* Name: */
  renderer_ops->set_font(renderer, assoc_font, ASSOCIATION_FONTHEIGHT);
 
  if (assoc->name != NULL) {
    pos = assoc->text_pos;
    renderer_ops->draw_string(renderer, assoc->name,
			       &pos, assoc->text_align,
			       &assoc->text_color);
  }

  /* Direction: */
  renderer_ops->set_fillstyle(renderer, FILLSTYLE_SOLID);

  if (assoc_get_direction_poly (assoc, poly))
    renderer_ops->fill_polygon(renderer, poly, 3, &assoc->line_color);

  for (i=0;i<2;i++) {
    AssociationEnd *end = &assoc->end[i];
    pos = end->text_pos;

    if (end->role != NULL && *end->role) {
      gchar *role_name = g_strdup_printf ("%c%s", visible_char[(int) end->visibility], end->role);
      renderer_ops->draw_string(renderer, 
                                role_name,
				&pos, 
				end->text_align,
				&assoc->text_color);
      g_free (role_name);
      pos.y += ASSOCIATION_FONTHEIGHT;
    }
    if (end->multiplicity != NULL) {
      renderer_ops->draw_string(renderer, end->multiplicity,
				 &pos, end->text_align,
				 &assoc->text_color);
    }
  }
}

static void
association_state_free(ObjectState *ostate)
{
  AssociationState *state = (AssociationState *)ostate;
  int i;
  g_free(state->name);

  for (i=0;i<2;i++) {
    g_free(state->end[i].role);
    g_free(state->end[i].multiplicity);
  }
}

static AssociationState *
association_get_state(Association *assoc)
{
  int i;
  AssociationEnd *end;

  AssociationState *state = g_new0(AssociationState, 1);

  state->obj_state.free = association_state_free;

  state->name = g_strdup(assoc->name);
  state->direction = assoc->direction;
  
  for (i=0;i<2;i++) {
    end = &assoc->end[i];
    state->end[i].role = g_strdup(end->role);
    state->end[i].multiplicity = g_strdup(end->multiplicity);
    state->end[i].arrow = end->arrow;
    state->end[i].aggregate = end->aggregate;
	state->end[i].visibility = end->visibility;
  }

  return state;
}

static void
association_set_state(Association *assoc, AssociationState *state)
{
  int i;
  AssociationEnd *end;
  
  g_free(assoc->name);
  assoc->name = state->name;
  assoc->text_width = 0.0;
  assoc->ascent = 0.0;
  assoc->descent = 0.0;
  if (assoc->name != NULL) {
    assoc->text_width =
      dia_font_string_width(assoc->name, assoc_font, ASSOCIATION_FONTHEIGHT);
    assoc->ascent = 
      dia_font_ascent(assoc->name, assoc_font, ASSOCIATION_FONTHEIGHT);
    assoc->descent =     
      dia_font_descent(assoc->name, assoc_font, ASSOCIATION_FONTHEIGHT);
  } 
  
  assoc->direction = state->direction;
  
  for (i=0;i<2;i++) {
    end = &assoc->end[i];
    g_free(end->role);
    g_free(end->multiplicity);
    end->role = state->end[i].role;
    end->multiplicity = state->end[i].multiplicity;
    end->arrow = state->end[i].arrow;
    end->aggregate = state->end[i].aggregate;
	end->visibility = state->end[i].visibility;

    end->text_width = 0.0;
    end->role_ascent = 0.0;
    end->role_descent = 0.0;
    end->multi_ascent = 0.0;
    end->multi_descent = 0.0;
    if (end->role != NULL && *end->role) {
      end->text_width = 
          dia_font_string_width(end->role, assoc_font, ASSOCIATION_FONTHEIGHT);
      end->role_ascent =
          dia_font_ascent(end->role, assoc_font, ASSOCIATION_FONTHEIGHT);
      end->role_descent =
          dia_font_ascent(end->role, assoc_font, ASSOCIATION_FONTHEIGHT);          
    }
    if (end->multiplicity != NULL) {
      end->text_width = MAX(end->text_width,
                            dia_font_string_width(end->multiplicity,
                                                  assoc_font,
                                                  ASSOCIATION_FONTHEIGHT) );
      end->role_ascent = dia_font_ascent(end->multiplicity,
                                         assoc_font,ASSOCIATION_FONTHEIGHT);
      end->role_descent = dia_font_descent(end->multiplicity,
                                           assoc_font,ASSOCIATION_FONTHEIGHT);
    }
  }

  g_free(state);
  
  association_update_data(assoc);
}

static void
association_update_data_end(Association *assoc, int endnum)
{
  OrthConn *orth = &assoc->orth;
  DiaObject *obj = &orth->object;
  Point *points  = orth->points;
  Rectangle rect;
  AssociationEnd *end;
  Orientation dir;
  int n = orth->numpoints - 1, fp, sp;
  Point dir_poly[3];

  /* Find the first and second points depending on which end: */
  if (endnum) {
      fp = n;
      sp = n-1;
      dir = assoc->orth.orientation[n-1];
  } else {
      fp = 0;
      sp = 1;
      dir = assoc->orth.orientation[0];
  }

  /* If the points are the same, find a better candidate: */
  if (points[fp].x == points[sp].x && points[fp].y == points[sp].y) {
      sp += (endnum ? -1 : 1);
      if (sp < 0)
	  sp = 0;
      if (sp > n)
	  sp = n;
      if (points[fp].y != points[sp].y)
	  dir = VERTICAL;
      else      
	  dir = HORIZONTAL;
  }

  /* Update the text-points of the ends: */
  end = &assoc->end[endnum];
  end->text_pos = points[fp];
  switch (dir) {
  case HORIZONTAL:
    end->text_pos.y -= end->role_descent;
    if (points[fp].x < points[sp].x) {
      end->text_align = ALIGN_LEFT;
      end->text_pos.x += (get_aggregate_pos_diff(end) + ASSOCIATION_END_SPACE);
    } else {
      end->text_align = ALIGN_RIGHT;    
      end->text_pos.x -= (get_aggregate_pos_diff(end) + ASSOCIATION_END_SPACE);
    }
    break;
  case VERTICAL:
    if (end->arrow || end->aggregate != AGGREGATE_NONE)
	end->text_pos.x += ASSOCIATION_DIAMONDWIDTH / 2;
    end->text_pos.x += ASSOCIATION_END_SPACE;

    end->text_pos.y += end->role_ascent;
    if (points[fp].y > points[sp].y) {
      if (end->role!=NULL && *end->role)
          end->text_pos.y -= ASSOCIATION_FONTHEIGHT;
      if (end->multiplicity!=NULL)
          end->text_pos.y -= ASSOCIATION_FONTHEIGHT;
    }

    end->text_align = ALIGN_LEFT;
    break;
  }
  /* Add the text recangle to the bounding box: */
  rect.left = end->text_pos.x
      - (end->text_align == ALIGN_LEFT ? 0 : end->text_width);
  rect.right = rect.left + end->text_width;
  rect.top = end->text_pos.y - end->role_ascent;
  rect.bottom = rect.top + 2*ASSOCIATION_FONTHEIGHT;
  
  rectangle_union(&obj->bounding_box, &rect);
  
  if (assoc_get_direction_poly (assoc, dir_poly)) {
    int i;
    for (i = 0; i < 3; ++i)
      rectangle_add_point (&obj->bounding_box, &dir_poly[i]);
  }
}

static void
association_update_data(Association *assoc)
{
        /* FIXME: The ascent and descent computation logic here is
           fundamentally slow. */

  OrthConn *orth = &assoc->orth;
  DiaObject *obj = &orth->object;
  PolyBBExtras *extra = &orth->extra_spacing;
  int num_segm, i;
  Point *points;
  Rectangle rect;
  Orientation dir;
  
  orthconn_update_data(orth);  

  /* translate new assoc state to old assoc ends */
  if (assoc->direction == ASSOC_NODIR) {
    assoc->end[0].aggregate = AGGREGATE_NONE;
    assoc->end[1].aggregate = AGGREGATE_NONE;
  } else if (assoc->direction == ASSOC_RIGHT) {
    /* the diamond is  at the start of the line */
    assoc->end[0].aggregate = assoc->assoc_type;
    assoc->end[1].aggregate = AGGREGATE_NONE;
  } else {
    assoc->end[1].aggregate = assoc->assoc_type;
    assoc->end[0].aggregate = AGGREGATE_NONE;
  }
  
  extra->start_trans = 
    extra->start_long = (assoc->end[0].aggregate == AGGREGATE_NONE?
                         ASSOCIATION_WIDTH/2.0:
                         (ASSOCIATION_WIDTH + ASSOCIATION_DIAMONDLEN)/2.0);
  extra->middle_trans = ASSOCIATION_WIDTH/2.0;
  extra->end_trans = 
    extra->end_long = (assoc->end[1].aggregate == AGGREGATE_NONE?
                         ASSOCIATION_WIDTH/2.0:
                         (ASSOCIATION_WIDTH + ASSOCIATION_DIAMONDLEN)/2.0);

  if (assoc->end[0].arrow)
    extra->start_trans = MAX(extra->start_trans, ASSOCIATION_TRIANGLESIZE);
  if (assoc->end[1].arrow)
    extra->end_trans = MAX(extra->end_trans, ASSOCIATION_TRIANGLESIZE);

  orthconn_update_boundingbox(orth);
  
  /* Calc text pos: */
  num_segm = assoc->orth.numpoints - 1;
  points = assoc->orth.points;
  i = num_segm / 2;
  
  if ((num_segm % 2) == 0) { /* If no middle segment, use horizontal */
    if (assoc->orth.orientation[i]==VERTICAL)
      i--;
  }
  dir = assoc->orth.orientation[i];
  /* also adapt for degenerated segement */
  if (VERTICAL == dir && points[i].y == points[i+1].y)
    dir = HORIZONTAL;
  else if (HORIZONTAL == dir && points[i].x == points[i+1].x)
    dir = VERTICAL;

  switch (dir) {
  case HORIZONTAL:
    assoc->text_align = ALIGN_CENTER;
    assoc->text_pos.x = 0.5*(points[i].x+points[i+1].x);
    assoc->text_pos.y = points[i].y - assoc->descent;
    break;
  case VERTICAL:
    assoc->text_align = ALIGN_LEFT;
    assoc->text_pos.x = points[i].x + 0.1;
    assoc->text_pos.y = 0.5*(points[i].y+points[i+1].y) - assoc->descent;
    break;
  }

  /* Add the text recangle to the bounding box: */
  rect.left = assoc->text_pos.x;
  if (assoc->text_align == ALIGN_CENTER)
    rect.left -= assoc->text_width/2.0;
  rect.right = rect.left + assoc->text_width;
  rect.top = assoc->text_pos.y - assoc->ascent;
  rect.bottom = rect.top + ASSOCIATION_FONTHEIGHT;

  rectangle_union(&obj->bounding_box, &rect);

  association_update_data_end(assoc, 0);
  association_update_data_end(assoc, 1);
}

static coord get_aggregate_pos_diff(AssociationEnd *end)
{
  coord width=0;
  if(end->arrow){
    width = ASSOCIATION_TRIANGLESIZE;
  }
  switch(end->aggregate){
  case AGGREGATE_COMPOSITION:
  case AGGREGATE_NORMAL:
    if(width!=0) width = MAX(ASSOCIATION_TRIANGLESIZE, ASSOCIATION_DIAMONDLEN);
    else width = ASSOCIATION_DIAMONDLEN;
  case AGGREGATE_NONE:
    break;
  }
  return width;
}

static DiaObject *
association_create(Point *startpoint,
	       void *user_data,
  	       Handle **handle1,
	       Handle **handle2)
{
  Association *assoc;
  OrthConn *orth;
  DiaObject *obj;
  int i;
  int user_d;

  if (assoc_font == NULL)
    assoc_font = dia_font_new_from_style(DIA_FONT_MONOSPACE, ASSOCIATION_FONTHEIGHT);
  
  assoc = g_malloc0(sizeof(Association));
  orth = &assoc->orth;
  obj = &orth->object;

  obj->type = &association_type;

  obj->ops = &association_ops;

  orthconn_init(orth, startpoint);
  
  assoc->text_color = color_black;
  assoc->line_color = attributes_get_foreground();
  assoc->name = NULL;
  assoc->assoc_type = AGGREGATE_NORMAL;
  assoc->direction = ASSOC_RIGHT;
  assoc->show_direction = FALSE;
  for (i=0;i<2;i++) {
    assoc->end[i].role = NULL;
    assoc->end[i].multiplicity = NULL;
    assoc->end[i].arrow = FALSE;
    assoc->end[i].aggregate = AGGREGATE_NONE;
    assoc->end[i].text_width = 0.0;
    assoc->end[i].visibility = UML_IMPLEMENTATION;
  }
  
  assoc->text_width = 0.0;

  user_d = GPOINTER_TO_INT(user_data);
  switch (user_d) {
  case 0:
    assoc->assoc_type = AGGREGATE_NONE;
    assoc->show_direction = TRUE;
    break;
  case 1:
    assoc->assoc_type = AGGREGATE_NORMAL;
    assoc->show_direction = FALSE;
    break;
  }

  association_update_data(assoc);
  
  *handle1 = orth->handles[0];
  *handle2 = orth->handles[orth->numpoints-2];

  return &assoc->orth.object;
}

static ObjectChange *
association_add_segment_callback(DiaObject *obj, Point *clicked, gpointer data)
{
  ObjectChange *change;
  change = orthconn_add_segment((OrthConn *)obj, clicked);
  association_update_data((Association *)obj);
  return change;
}

static ObjectChange *
association_delete_segment_callback(DiaObject *obj, Point *clicked, gpointer data)
{
  ObjectChange *change;
  change = orthconn_delete_segment((OrthConn *)obj, clicked);
  association_update_data((Association *)obj);
  return change;
}


static DiaMenuItem object_menu_items[] = {
  { N_("Add segment"), association_add_segment_callback, NULL, 1 },
  { N_("Delete segment"), association_delete_segment_callback, NULL, 1 },
  ORTHCONN_COMMON_MENUS,
};

static DiaMenu object_menu = {
  "Association",
  sizeof(object_menu_items)/sizeof(DiaMenuItem),
  object_menu_items,
  NULL
};

static DiaMenu *
association_get_object_menu(Association *assoc, Point *clickedpoint)
{
  OrthConn *orth;

  orth = &assoc->orth;
  /* Set entries sensitive/selected etc here */
  object_menu_items[0].active = orthconn_can_add_segment(orth, clickedpoint);
  object_menu_items[1].active = orthconn_can_delete_segment(orth, clickedpoint);
  orthconn_update_object_menu(orth, clickedpoint, &object_menu_items[2]);
  return &object_menu;
}

static void
association_destroy(Association *assoc)
{
  int i;
  
  orthconn_destroy(&assoc->orth);

  g_free(assoc->name);

  for (i=0;i<2;i++) {
    g_free(assoc->end[i].role);
    g_free(assoc->end[i].multiplicity);
  }
}

static DiaObject *
association_copy(Association *assoc)
{
  Association *newassoc;
  OrthConn *orth, *neworth;
  DiaObject *newobj;
  int i;
  
  orth = &assoc->orth;
  
  newassoc = g_malloc0(sizeof(Association));
  neworth = &newassoc->orth;
  newobj = &neworth->object;

  orthconn_copy(orth, neworth);

  newassoc->name = g_strdup(assoc->name);
  newassoc->direction = assoc->direction;
  newassoc->show_direction = assoc->show_direction;
  newassoc->assoc_type = assoc->assoc_type;
  newassoc->text_color = assoc->text_color;
  newassoc->line_color = assoc->line_color;
  for (i=0;i<2;i++) {
    newassoc->end[i] = assoc->end[i];
    newassoc->end[i].role =
      (assoc->end[i].role != NULL)?g_strdup(assoc->end[i].role):NULL;
    newassoc->end[i].multiplicity =
      (assoc->end[i].multiplicity != NULL)?g_strdup(assoc->end[i].multiplicity):NULL;
  }

  newassoc->text_width = assoc->text_width;
  
  association_update_data(newassoc);
  
  return &newassoc->orth.object;
}

static DiaObject *
association_load(ObjectNode obj_node, int version, const char *filename)
{
  Association *assoc;
  AttributeNode attr;
  DataNode composite;
  OrthConn *orth;
  DiaObject *obj;
  int i;
  
  /* first calls our _create() method */
  obj = object_load_using_properties(&association_type, obj_node, version, filename);
  assoc = (Association *)obj;
  orth = &assoc->orth;
  /* ... butnot orthconn_load()  */
  if (version < 1)
    orth->autorouting = FALSE;

  if (version < 2) {
    /* vesrion 1 used to name it differently */
    attr = object_find_attribute(obj_node, "autorouting");
    if (attr != NULL)
      orth->autorouting = data_boolean(attribute_first_data(attr));

    attr = object_find_attribute(obj_node, "ends");
    composite = attribute_first_data(attr);
    for (i=0;i<2;i++) {

      assoc->end[i].role = NULL;
      attr = composite_find_attribute(composite, "role");
      if (attr != NULL) {
        assoc->end[i].role = data_string(attribute_first_data(attr));
      }
      if (   assoc->end[i].role != NULL 
          && 0 == strcmp(assoc->end[i].role, "")) {
        g_free(assoc->end[i].role);
        assoc->end[i].role = NULL;
      }
    
      assoc->end[i].multiplicity = NULL;
      attr = composite_find_attribute(composite, "multiplicity");
      if (attr != NULL) {
        assoc->end[i].multiplicity = data_string(attribute_first_data(attr));
      }
      if (   assoc->end[i].multiplicity != NULL
	  && 0 == strcmp(assoc->end[i].multiplicity, "")) {
        g_free(assoc->end[i].multiplicity);
        assoc->end[i].multiplicity = NULL;
      }
    
      assoc->end[i].arrow = FALSE;
      attr = composite_find_attribute(composite, "arrow");
      if (attr != NULL)
        assoc->end[i].arrow = data_boolean(attribute_first_data(attr));

      assoc->end[i].aggregate = AGGREGATE_NONE;
      attr = composite_find_attribute(composite, "aggregate");
      if (attr != NULL)
        assoc->end[i].aggregate = data_enum(attribute_first_data(attr));
  
      assoc->end[i].visibility = FALSE;
      attr = composite_find_attribute(composite, "visibility");
      if (attr != NULL)
        assoc->end[i].visibility =  data_enum( attribute_first_data(attr) );

      assoc->end[i].text_width = 0.0;
      if (assoc->end[i].role != NULL) {
        assoc->end[i].text_width = 
          dia_font_string_width(assoc->end[i].role, assoc_font,
                                ASSOCIATION_FONTHEIGHT);
      }
      if (assoc->end[i].multiplicity != NULL) {
        assoc->end[i].text_width =
          MAX(assoc->end[i].text_width,
              dia_font_string_width(assoc->end[i].multiplicity,
                                    assoc_font, ASSOCIATION_FONTHEIGHT) );
      }
      composite = data_next(composite);
    }
    /* derive new members state from ends */
    assoc->show_direction = (assoc->direction != ASSOC_NODIR);
    if (assoc->end[0].aggregate == AGGREGATE_NORMAL) {
      assoc->assoc_type = AGGREGATE_NORMAL;
      assoc->direction = ASSOC_RIGHT;
    } else if (assoc->end[0].aggregate == AGGREGATE_COMPOSITION) {
      assoc->assoc_type = AGGREGATE_COMPOSITION;
      assoc->direction = ASSOC_RIGHT;
    } else if (assoc->end[1].aggregate == AGGREGATE_NORMAL) {
      assoc->assoc_type = AGGREGATE_NORMAL;
      assoc->direction = ASSOC_LEFT;
    } else if (assoc->end[1].aggregate == AGGREGATE_COMPOSITION) {
      assoc->assoc_type = AGGREGATE_COMPOSITION;
      assoc->direction = ASSOC_LEFT;
    }
  } /* version < 2 */
  
  association_set_state(assoc, association_get_state(assoc));

  return &assoc->orth.object;
}

