/* Dia -- an diagram creation/manipulation program
 * Copyright (C) 1998 Alexander Larsson
 *
 * Objects for drawing task and resources in i* diagrams
 * Copyright (C) 2002 Christophe Ponsard
 *
 * Based on SADT box object
 * Copyright (C) 2000, 2001 Cyrille Chepelov
 *
 * Forked from Flowchart toolbox -- objects for drawing flowcharts.
 * Copyright (C) 1999 James Henstridge.
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <assert.h>
#include <math.h>
#include <string.h>
#include <glib.h>

#include "intl.h"
#include "object.h"
#include "element.h"
#include "connectionpoint.h"
#include "diarenderer.h"
#include "attributes.h"
#include "text.h"
#include "widgets.h"
#include "message.h"
#include "connpoint_line.h"
#include "color.h"
#include "properties.h"

#include "pixmaps/resource.xpm"

#define DEFAULT_WIDTH 3.0
#define DEFAULT_HEIGHT 1
#define DEFAULT_BORDER 0.2
#define DEFAULT_PADDING 0.4
#define DEFAULT_FONT 0.7
#define OTHER_LINE_WIDTH 0.12
#define OTHER_FG_COLOR color_black
#define OTHER_BG_COLOR color_white

typedef enum {
  ANCHOR_MIDDLE,
  ANCHOR_START,
  ANCHOR_END
} AnchorShape;

typedef enum {
  RESOURCE,
  TASK
} OtherType;

static PropEnumData prop_other_type_data[] = {
  { N_("Resource"), RESOURCE },
  { N_("Task"),     TASK },
  { NULL, 0}
};

typedef struct _Other {
  Element element;
  ConnPointLine *north,*south,*east,*west;

  Text *text;
  real padding;
  OtherType type;

  TextAttributes attrs;
  int init;
} Other;

static real other_distance_from(Other *other, Point *point);
static void other_select(Other *other, Point *clicked_point,
		       DiaRenderer *interactive_renderer);
static ObjectChange* other_move_handle(Other *other, Handle *handle,
			    Point *to, ConnectionPoint *cp,
			    HandleMoveReason reason, ModifierKeys modifiers);
static ObjectChange* other_move(Other *other, Point *to);
static void other_draw(Other *other, DiaRenderer *renderer);
static void other_update_data(Other *other, AnchorShape horix, AnchorShape vert);
static DiaObject *other_create(Point *startpoint,
			  void *user_data,
			  Handle **handle1,
			  Handle **handle2);
static void other_destroy(Other *other);
static DiaObject *other_load(ObjectNode obj_node, int version,
                            const char *filename);
static DiaMenu *other_get_object_menu(Other *other, Point *clickedpoint);

static PropDescription *other_describe_props(Other *other);
static void other_get_props(Other *other, GPtrArray *props);
static void other_set_props(Other *other, GPtrArray *props);

static ObjectTypeOps istar_other_type_ops =
{
  (CreateFunc) other_create,
  (LoadFunc)   other_load/*using_properties*/,
  (SaveFunc)   object_save_using_properties,
  (GetDefaultsFunc)   NULL,
  (ApplyDefaultsFunc) NULL,
};

DiaObjectType istar_other_type =
{
  "Istar - other",          /* name */
  0,                        /* version */
  (char **) istar_resource_xpm, /* pixmap */
  &istar_other_type_ops      /* ops */
};

static ObjectOps other_ops = {
  (DestroyFunc)         other_destroy,
  (DrawFunc)            other_draw,
  (DistanceFunc)        other_distance_from,
  (SelectFunc)          other_select,
  (CopyFunc)            object_copy_using_properties,
  (MoveFunc)            other_move,
  (MoveHandleFunc)      other_move_handle,
  (GetPropertiesFunc)   object_create_props_dialog,
  (ApplyPropertiesDialogFunc) object_apply_props_from_dialog,
  (ObjectMenuFunc)      other_get_object_menu,
  (DescribePropsFunc)   other_describe_props,
  (GetPropsFunc)        other_get_props,
  (SetPropsFunc)        other_set_props,
  (TextEditFunc) 0,
  (ApplyPropertiesListFunc) object_apply_props,
};

static PropDescription other_props[] = {
  ELEMENT_COMMON_PROPERTIES,
  { "type", PROP_TYPE_ENUM, PROP_FLAG_VISIBLE|PROP_FLAG_NO_DEFAULTS,
    N_("Type"),
    N_("Type"),
    prop_other_type_data},

  { "text", PROP_TYPE_TEXT, 0,NULL,NULL},
  { "cpl_north",PROP_TYPE_CONNPOINT_LINE, 0, NULL, NULL},
  { "cpl_west",PROP_TYPE_CONNPOINT_LINE, 0, NULL, NULL},
  { "cpl_south",PROP_TYPE_CONNPOINT_LINE, 0, NULL, NULL},
  { "cpl_east",PROP_TYPE_CONNPOINT_LINE, 0, NULL, NULL},
  PROP_DESC_END
};

static PropDescription *
other_describe_props(Other *other)
{
  if (other_props[0].quark == 0) {
    prop_desc_list_calculate_quarks(other_props);
  }
  return other_props;
}

static PropOffset other_offsets[] = {
  ELEMENT_COMMON_PROPERTIES_OFFSETS,
  { "type", PROP_TYPE_ENUM, offsetof(Other,type)},
  { "text", PROP_TYPE_TEXT, offsetof(Other,text)},
  { "cpl_north",PROP_TYPE_CONNPOINT_LINE, offsetof(Other,north)},
  { "cpl_west",PROP_TYPE_CONNPOINT_LINE, offsetof(Other,west)},
  { "cpl_south",PROP_TYPE_CONNPOINT_LINE, offsetof(Other,south)},
  { "cpl_east",PROP_TYPE_CONNPOINT_LINE, offsetof(Other,east)},
  {NULL}
};

static void
other_get_props(Other *other, GPtrArray *props)
{
  text_get_attributes(other->text,&other->attrs);
  object_get_props_from_offsets(&other->element.object,
                                other_offsets,props);
}

static void
other_set_props(Other *other, GPtrArray *props)
{
  if (other->init==-1) { other->init++; return; }    /* workaround init bug */

  object_set_props_from_offsets(&other->element.object,
                                other_offsets,props);
  apply_textattr_properties(props,other->text,"text",&other->attrs);
  other_update_data(other, ANCHOR_MIDDLE, ANCHOR_MIDDLE);
}

static real
other_distance_from(Other *other, Point *point)
{
  Element *elem = &other->element;
  Rectangle rect;

  rect.left = elem->corner.x - OTHER_LINE_WIDTH/2;
  rect.right = elem->corner.x + elem->width + OTHER_LINE_WIDTH/2;
  rect.top = elem->corner.y - OTHER_LINE_WIDTH/2;
  rect.bottom = elem->corner.y + elem->height + OTHER_LINE_WIDTH/2;
  return distance_rectangle_point(&rect, point);
}

static void
other_select(Other *other, Point *clicked_point,
	   DiaRenderer *interactive_renderer)
{
  text_set_cursor(other->text, clicked_point, interactive_renderer);
  text_grab_focus(other->text, &other->element.object);
  element_update_handles(&other->element);
}

static ObjectChange*
other_move_handle(Other *other, Handle *handle,
		Point *to, ConnectionPoint *cp,
		HandleMoveReason reason, ModifierKeys modifiers)
{
  AnchorShape horiz = ANCHOR_MIDDLE, vert = ANCHOR_MIDDLE;

  assert(other!=NULL);
  assert(handle!=NULL);
  assert(to!=NULL);

  element_move_handle(&other->element, handle->id, to, cp, reason, modifiers);

  switch (handle->id) {
  case HANDLE_RESIZE_NW:
    horiz = ANCHOR_END; vert = ANCHOR_END; break;
  case HANDLE_RESIZE_N:
    vert = ANCHOR_END; break;
  case HANDLE_RESIZE_NE:
    horiz = ANCHOR_START; vert = ANCHOR_END; break;
  case HANDLE_RESIZE_E:
    horiz = ANCHOR_START; break;
  case HANDLE_RESIZE_SE:
    horiz = ANCHOR_START; vert = ANCHOR_START; break;
  case HANDLE_RESIZE_S:
    vert = ANCHOR_START; break;
  case HANDLE_RESIZE_SW:
    horiz = ANCHOR_END; vert = ANCHOR_START; break;
  case HANDLE_RESIZE_W:
    horiz = ANCHOR_END; break;
  default:
    break;
  }
  other_update_data(other, horiz, vert);
  return NULL;
}

static ObjectChange*
other_move(Other *other, Point *to)
{
  other->element.corner = *to;

  other_update_data(other, ANCHOR_MIDDLE, ANCHOR_MIDDLE);
  return NULL;
}

static void compute_task(Other *other, Point *pl) {
     double rx,ry,w,h,h2;
     Element *elem;

     elem=&other->element;
     w=elem->width;
     h=elem->height;
     rx=elem->corner.x;
     ry=elem->corner.y;
     h2=h/2;

     pl[0].x=rx;
     pl[0].y=ry+h2;
     pl[1].x=rx+h2;
     pl[1].y=ry;
     pl[2].x=rx+w-h2;
     pl[2].y=ry;
     pl[3].x=rx+w;
     pl[3].y=ry+h2;
     pl[4].x=rx+w-h2;
     pl[4].y=ry+h;
     pl[5].x=rx+h2;
     pl[5].y=ry+h;
}

/* drawing stuff */
static void
other_draw(Other *other, DiaRenderer *renderer)
{
  DiaRendererClass *renderer_ops = DIA_RENDERER_GET_CLASS (renderer);
  Point pl[6],p1,p2;
  Element *elem;

  /* some asserts */
  assert(other != NULL);
  assert(renderer != NULL);

  elem = &other->element;

  renderer_ops->set_linestyle(renderer, LINESTYLE_SOLID);
  renderer_ops->set_linejoin(renderer, LINEJOIN_MITER);

  switch (other->type) {
    case RESOURCE:
      p1.x=elem->corner.x;
      p1.y= elem->corner.y;
      p2.x=p1.x+elem->width;
      p2.y=p1.y+elem->height;
      renderer_ops->fill_rect(renderer,&p1,&p2, &OTHER_BG_COLOR);
      renderer_ops->set_linewidth(renderer, OTHER_LINE_WIDTH);
      renderer_ops->draw_rect(renderer,&p1,&p2, &OTHER_FG_COLOR);
      break;
    case TASK:
      compute_task(other,pl);
      renderer_ops->set_fillstyle(renderer, FILLSTYLE_SOLID);
      renderer_ops->fill_polygon(renderer, pl, 6, &OTHER_BG_COLOR);
      renderer_ops->set_linewidth(renderer, OTHER_LINE_WIDTH);
      renderer_ops->draw_polygon(renderer, pl, 6, &OTHER_FG_COLOR);
      break;
  }

  /* drawing text */
  text_draw(other->text, renderer);
}

static void
other_update_data(Other *other, AnchorShape horiz, AnchorShape vert)
{
  Element *elem = &other->element;
  ElementBBExtras *extra = &elem->extra_spacing;
  DiaObject *obj = &elem->object;
  Point center, bottom_right;
  Point p;
  real width, height;
  Point nw,ne,se,sw;

  /* save starting points */
  center = bottom_right = elem->corner;
  center.x += elem->width/2;
  bottom_right.x += elem->width;
  center.y += elem->height/2;
  bottom_right.y += elem->height;

  text_calc_boundingbox(other->text, NULL);
  width = other->text->max_width + other->padding*2;
  height = other->text->height * other->text->numlines + other->padding*2;

  /* autoscale here */
  if (width > elem->width) elem->width = width;
  if (height > elem->height) elem->height = height;
  if (elem->width<elem->height*1.5) elem->width=elem->height*1.5;

  /* move shape if necessary ... */
  switch (horiz) {
  case ANCHOR_MIDDLE:
    elem->corner.x = center.x - elem->width/2; break;
  case ANCHOR_END:
    elem->corner.x = bottom_right.x - elem->width; break;
  default:
    break;
  }
  switch (vert) {
  case ANCHOR_MIDDLE:
    elem->corner.y = center.y - elem->height/2; break;
  case ANCHOR_END:
    elem->corner.y = bottom_right.y - elem->height; break;
  default:
    break;
  }

  p = elem->corner;
  p.x += elem->width / 2.0;

  p.y += elem->height / 2.0 - other->text->height * other->text->numlines / 2 +
    other->text->ascent;
  text_set_position(other->text, &p);

  extra->border_trans = OTHER_LINE_WIDTH / 2.0;
  element_update_boundingbox(elem);

  obj->position = elem->corner;

  element_update_handles(elem);

  /* Update connections: */
  nw = elem->corner;
  se.x = nw.x + elem->width;
  se.y = nw.y + elem->height;
  ne.x = se.x;
  ne.y = nw.y;
  sw.y = se.y;
  sw.x = nw.x;

  connpointline_update(other->north);
  connpointline_putonaline(other->north,&ne,&nw);
  connpointline_update(other->west);
  connpointline_putonaline(other->west,&nw,&sw);
  connpointline_update(other->south);
  connpointline_putonaline(other->south,&sw,&se);
  connpointline_update(other->east);
  connpointline_putonaline(other->east,&se,&ne);
}

static ConnPointLine *
other_get_clicked_border(Other *other, Point *clicked)
{
  ConnPointLine *cpl;
  real dist,dist2;

  cpl = other->north;
  dist = distance_line_point(&other->north->start,&other->north->end,0,clicked);

  dist2 = distance_line_point(&other->west->start,&other->west->end,0,clicked);
  if (dist2 < dist) {
    cpl = other->west;
    dist = dist2;
  }
  dist2 = distance_line_point(&other->south->start,&other->south->end,0,clicked);
  if (dist2 < dist) {
    cpl = other->south;
    dist = dist2;
  }
  dist2 = distance_line_point(&other->east->start,&other->east->end,0,clicked);
  if (dist2 < dist) {
    cpl = other->east;
    /*dist = dist2;*/
  }
  return cpl;
}

inline static ObjectChange *
other_create_change(Other *other, ObjectChange *inner, ConnPointLine *cpl) {
  return (ObjectChange *)inner;
}

static ObjectChange *
other_add_connpoint_callback(DiaObject *obj, Point *clicked, gpointer data)
{
  ObjectChange *change;
  ConnPointLine *cpl;
  Other *other = (Other *)obj;

  cpl = other_get_clicked_border(other,clicked);
  change = connpointline_add_point(cpl, clicked);
  other_update_data((Other *)obj,ANCHOR_MIDDLE, ANCHOR_MIDDLE);
  return other_create_change(other,change,cpl);
}

static ObjectChange *
other_remove_connpoint_callback(DiaObject *obj, Point *clicked, gpointer data)
{
  ObjectChange *change;
  ConnPointLine *cpl;
  Other *other = (Other *)obj;

  cpl = other_get_clicked_border(other,clicked);
  change = connpointline_remove_point(cpl, clicked);
  other_update_data((Other *)obj,ANCHOR_MIDDLE, ANCHOR_MIDDLE);
  return other_create_change(other,change,cpl);
}

static DiaMenuItem object_menu_items[] = {
  { N_("Add connection point"), other_add_connpoint_callback, NULL, 1 },
  { N_("Delete connection point"), other_remove_connpoint_callback,
    NULL, 1 },
};

static DiaMenu object_menu = {
  N_("i* other"),
  sizeof(object_menu_items)/sizeof(DiaMenuItem),
  object_menu_items,
  NULL
};

static DiaMenu *
other_get_object_menu(Other *other, Point *clickedpoint)
{
  ConnPointLine *cpl;

  cpl = other_get_clicked_border(other,clickedpoint);
  /* Set entries sensitive/selected etc here */
  object_menu_items[0].active = connpointline_can_add_point(cpl, clickedpoint);
  object_menu_items[1].active = connpointline_can_remove_point(cpl, clickedpoint);
  return &object_menu;
}

/* creating stuff */
static DiaObject *
other_create(Point *startpoint,
	   void *user_data,
	   Handle **handle1,
	   Handle **handle2)
{
  Other *other;
  Element *elem;
  DiaObject *obj;
  Point p;
  DiaFont* font;

  other = g_malloc0(sizeof(Other));
  elem = &other->element;
  obj = &elem->object;

  obj->type = &istar_other_type;

  obj->ops = &other_ops;

  elem->corner = *startpoint;
  elem->width = DEFAULT_WIDTH;
  elem->height = DEFAULT_HEIGHT;

  other->padding = DEFAULT_PADDING;

  p = *startpoint;
  p.x += elem->width / 2.0;
  p.y += elem->height / 2.0 + DEFAULT_FONT / 2;

  font = dia_font_new_from_style( DIA_FONT_SANS, DEFAULT_FONT);

  other->text = new_text("", font,
                       DEFAULT_FONT, &p,
                       &color_black,
                       ALIGN_CENTER);
  dia_font_unref(font);
  text_get_attributes(other->text,&other->attrs);

  element_init(elem, 8, 0);

  other->north = connpointline_create(obj,3);
  other->west = connpointline_create(obj,1);
  other->south = connpointline_create(obj,3);
  other->east = connpointline_create(obj,1);

  other->element.extra_spacing.border_trans = OTHER_LINE_WIDTH/2.0;
  other_update_data(other, ANCHOR_MIDDLE, ANCHOR_MIDDLE);

  *handle1 = NULL;
  *handle2 = obj->handles[7];

  /* handling type info */
  switch (GPOINTER_TO_INT(user_data)) {
    case 1:  other->type=RESOURCE; break;
    case 2:  other->type=TASK;     break;
    default: other->type=RESOURCE; break;
  }

  if (GPOINTER_TO_INT(user_data)!=0) other->init=-1; else other->init=0;

  return &other->element.object;
}

static void
other_destroy(Other *other)
{
  text_destroy(other->text);

  connpointline_destroy(other->east);
  connpointline_destroy(other->south);
  connpointline_destroy(other->west);
  connpointline_destroy(other->north);

  element_destroy(&other->element);
}


static DiaObject *
other_load(ObjectNode obj_node, int version, const char *filename)
{
  return object_load_using_properties(&istar_other_type,
                                      obj_node,version,filename);
}


