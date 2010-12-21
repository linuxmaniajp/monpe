/* Dia -- an diagram creation/manipulation program
 * Copyright (C) 1998 Alexander Larsson
 *
 * Activity type for UML diagrams
 * Copyright (C) 2002 Alejandro Sierra <asierra@servidor.unam.mx>
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
 */

/** \file objects/UML/activity.c  Implementation of the 'UML - Activity' type */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <assert.h>
#include <math.h>
#include <string.h>

#include "intl.h"
#include "object.h"
#include "element.h"
#include "diarenderer.h"
#include "attributes.h"
#include "text.h"
#include "properties.h"

#include "pixmaps/activity.xpm"

typedef struct _State State;

struct _State {
  Element element;

  ConnectionPoint connections[8];

  Text *text;

  TextAttributes attrs;
  Color line_color;
  Color fill_color;
};


#define STATE_WIDTH  4
#define STATE_HEIGHT 3
#define STATE_LINEWIDTH 0.1
#define STATE_MARGIN_X 0.5
#define STATE_MARGIN_Y 0.5

static real state_distance_from(State *state, Point *point);
static void state_select(State *state, Point *clicked_point,
			DiaRenderer *interactive_renderer);
static ObjectChange* state_move_handle(State *state, Handle *handle,
				       Point *to, ConnectionPoint *cp,
				       HandleMoveReason reason, ModifierKeys modifiers);
static ObjectChange* state_move(State *state, Point *to);
static void state_draw(State *state, DiaRenderer *renderer);
static DiaObject *state_create_activity(Point *startpoint,
			   void *user_data,
			   Handle **handle1,
			   Handle **handle2);
static void state_destroy(State *state);
static DiaObject *state_load(ObjectNode obj_node, int version,
			    const char *filename);
static PropDescription *state_describe_props(State *state);
static void state_get_props(State *state, GPtrArray *props);
static void state_set_props(State *state, GPtrArray *props);
static void state_update_data(State *state);


static ObjectTypeOps activity_type_ops =
{
  (CreateFunc) state_create_activity,
  (LoadFunc)   state_load,/*using_properties*/     /* load */
  (SaveFunc)   object_save_using_properties,      /* save */
  (GetDefaultsFunc)   NULL,
  (ApplyDefaultsFunc) NULL
};

DiaObjectType activity_type =
{
  "UML - Activity",   /* name */
  0,                      /* version */
  (char **) activity_xpm,  /* pixmap */
  
  &activity_type_ops       /* ops */
};


static ObjectOps state_ops = {
  (DestroyFunc)         state_destroy,
  (DrawFunc)            state_draw,
  (DistanceFunc)        state_distance_from,
  (SelectFunc)          state_select,
  (CopyFunc)            object_copy_using_properties,
  (MoveFunc)            state_move,
  (MoveHandleFunc)      state_move_handle,
  (GetPropertiesFunc)   object_create_props_dialog,
  (ApplyPropertiesDialogFunc) object_apply_props_from_dialog,
  (ObjectMenuFunc)      NULL,
  (DescribePropsFunc)   state_describe_props,
  (GetPropsFunc)        state_get_props,
  (SetPropsFunc)        state_set_props,
  (TextEditFunc) 0,
  (ApplyPropertiesListFunc) object_apply_props,
};


static PropDescription activity_props[] = {
  ELEMENT_COMMON_PROPERTIES,

  PROP_STD_LINE_COLOUR_OPTIONAL, 
  PROP_STD_FILL_COLOUR_OPTIONAL, 
  PROP_STD_TEXT_FONT,
  PROP_STD_TEXT_HEIGHT,
  PROP_STD_TEXT_COLOUR_OPTIONAL,
  { "text", PROP_TYPE_TEXT, 0, N_("Text"), NULL, NULL }, 
  
  PROP_DESC_END
};

static PropDescription *
state_describe_props(State *state)
{
  if (activity_props[0].quark == 0) {
    prop_desc_list_calculate_quarks(activity_props);
  }
   return activity_props;
}

static PropOffset state_offsets[] = {
  ELEMENT_COMMON_PROPERTIES_OFFSETS,
  {"line_colour",PROP_TYPE_COLOUR,offsetof(State,line_color)},
  {"fill_colour",PROP_TYPE_COLOUR,offsetof(State,fill_color)},
  {"text",PROP_TYPE_TEXT,offsetof(State,text)},
  {"text_font",PROP_TYPE_FONT,offsetof(State,attrs.font)},
  {PROP_STDNAME_TEXT_HEIGHT,PROP_STDTYPE_TEXT_HEIGHT,offsetof(State,attrs.height)},
  {"text_colour",PROP_TYPE_COLOUR,offsetof(State,attrs.color)},
  { NULL, 0, 0 },
};

static void
state_get_props(State * state, GPtrArray *props)
{
  text_get_attributes(state->text,&state->attrs);
  object_get_props_from_offsets(&state->element.object,
                                state_offsets,props);
}

static void
state_set_props(State *state, GPtrArray *props)
{
  object_set_props_from_offsets(&state->element.object,
                                state_offsets,props);
  apply_textattr_properties(props,state->text,"text",&state->attrs);
  state_update_data(state);
}

static real
state_distance_from(State *state, Point *point)
{
  DiaObject *obj = &state->element.object;
  return distance_rectangle_point(&obj->bounding_box, point);
}

static void
state_select(State *state, Point *clicked_point,
	       DiaRenderer *interactive_renderer)
{
  text_set_cursor(state->text, clicked_point, interactive_renderer);
  text_grab_focus(state->text, &state->element.object);
  element_update_handles(&state->element);
}

static ObjectChange*
state_move_handle(State *state, Handle *handle,
		  Point *to, ConnectionPoint *cp,
		  HandleMoveReason reason, ModifierKeys modifiers)
{
  assert(state!=NULL);
  assert(handle!=NULL);
  assert(to!=NULL);

  assert(handle->id < 8);

  return NULL;
}

static ObjectChange*
state_move(State *state, Point *to)
{
  state->element.corner = *to;
  state_update_data(state);

  return NULL;
}

static void
state_draw(State *state, DiaRenderer *renderer)
{
  DiaRendererClass *renderer_ops = DIA_RENDERER_GET_CLASS (renderer);
  Element *elem;
  real x, y, w, h;
  Point p1, p2;

  assert(state != NULL);
  assert(renderer != NULL);

  elem = &state->element;

  x = elem->corner.x;
  y = elem->corner.y;
  w = elem->width;
  h = elem->height;
  
  renderer_ops->set_fillstyle(renderer, FILLSTYLE_SOLID);
  renderer_ops->set_linewidth(renderer, STATE_LINEWIDTH);
  renderer_ops->set_linestyle(renderer, LINESTYLE_SOLID);

   p1.x = x;
   p1.y = y;
   p2.x = x + w;
   p2.y = y + h;
   renderer_ops->fill_rounded_rect(renderer, &p1, &p2,
				   &state->fill_color, 1.0);
   renderer_ops->draw_rounded_rect(renderer, &p1, &p2,
				   &state->line_color, 1.0);
   
   text_draw(state->text, renderer);
}


static void
state_update_data(State *state)
{
  real w, h;

  Element *elem = &state->element;
  ElementBBExtras *extra = &elem->extra_spacing;
  DiaObject *obj = &elem->object;
  Point p;
  
  text_calc_boundingbox(state->text, NULL);
  w = state->text->max_width + 2*STATE_MARGIN_X;
  h = state->text->height*state->text->numlines +2*STATE_MARGIN_Y;
  if (w < STATE_WIDTH)
    w = STATE_WIDTH;
  p.x = elem->corner.x + w/2.0;
  p.y = elem->corner.y + STATE_MARGIN_Y + state->text->ascent;
  text_set_position(state->text, &p);

  elem->width = w;
  elem->height = h;
  extra->border_trans = STATE_LINEWIDTH / 2.0;

  /* Update connections: */
  state->connections[0].pos = elem->corner;
  state->connections[1].pos.x = elem->corner.x + elem->width / 2.0;
  state->connections[1].pos.y = elem->corner.y;
  state->connections[2].pos.x = elem->corner.x + elem->width;
  state->connections[2].pos.y = elem->corner.y;
  state->connections[3].pos.x = elem->corner.x;
  state->connections[3].pos.y = elem->corner.y + elem->height / 2.0;
  state->connections[4].pos.x = elem->corner.x + elem->width;
  state->connections[4].pos.y = elem->corner.y + elem->height / 2.0;
  state->connections[5].pos.x = elem->corner.x;
  state->connections[5].pos.y = elem->corner.y + elem->height;
  state->connections[6].pos.x = elem->corner.x + elem->width / 2.0;
  state->connections[6].pos.y = elem->corner.y + elem->height;
  state->connections[7].pos.x = elem->corner.x + elem->width;
  state->connections[7].pos.y = elem->corner.y + elem->height;
  
  state->connections[0].directions = DIR_NORTH|DIR_WEST;
  state->connections[1].directions = DIR_NORTH;
  state->connections[2].directions = DIR_NORTH|DIR_EAST;
  state->connections[3].directions = DIR_WEST;
  state->connections[4].directions = DIR_EAST;
  state->connections[5].directions = DIR_SOUTH|DIR_WEST;
  state->connections[6].directions = DIR_SOUTH;
  state->connections[7].directions = DIR_SOUTH|DIR_EAST;
                                                                                          
  element_update_boundingbox(elem);

  obj->position = elem->corner;

  element_update_handles(elem);
}


static DiaObject *
state_create_activity(Point *startpoint,
	       void *user_data,
  	       Handle **handle1,
	       Handle **handle2)
{
  State *state;
  Element *elem;
  DiaObject *obj;
  Point p;
  DiaFont *font;
  int i;
  
  state = g_malloc0(sizeof(State));
  elem = &state->element;
  obj = &elem->object;
  
  obj->type = &activity_type;
  obj->ops = &state_ops;
  elem->corner = *startpoint;
  elem->width = STATE_WIDTH;
  elem->height = STATE_HEIGHT;

  state->line_color = attributes_get_foreground();
  state->fill_color = attributes_get_background();

  font = dia_font_new_from_style (DIA_FONT_SANS, 0.8);
  p = *startpoint;
  p.x += STATE_WIDTH/2.0;
  p.y += STATE_HEIGHT/2.0;
  
  state->text = new_text("", font, 0.8, &p, &color_black, ALIGN_CENTER);
  text_get_attributes(state->text,&state->attrs);
  element_init(elem, 8, 8);
  
  for (i=0;i<8;i++) {
    obj->connections[i] = &state->connections[i];
    state->connections[i].object = obj;
    state->connections[i].connected = NULL;
  }
  elem->extra_spacing.border_trans = 0.0;
  state_update_data(state);

  for (i=0;i<8;i++) {
    obj->handles[i]->type = HANDLE_NON_MOVABLE;
  }

  *handle1 = NULL;
  *handle2 = NULL;
  return &state->element.object;;
}

static void
state_destroy(State *state)
{
  text_destroy(state->text);

  element_destroy(&state->element);
}

static DiaObject *
state_load(ObjectNode obj_node, int version, const char *filename)
{
  return object_load_using_properties(&activity_type,
                                      obj_node,version,filename);
}




