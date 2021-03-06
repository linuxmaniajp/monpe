#ifndef __DTREE_H_INCLUDED_
#define __DTREE_H_INCLUDED_

#include <glib.h> 
#include <libxml/tree.h>

#define MONPE_XML_DICTIONARY "dictionarydata"

#define MONPE_XML_DIC_ELEMENT        "element"
#define MONPE_XML_DIC_ELEMENT_NAME   "name"
#define MONPE_XML_DIC_ELEMENT_OCCURS "occurs"

#define MONPE_XML_DIC_APPINFO            "appinfo"

#define MONPE_XML_DIC_EMBED              "embed"
#define MONPE_XML_DIC_EMBED_OBJ          "object"
#define MONPE_XML_DIC_EMBED_OBJ_TXT      "text"
#define MONPE_XML_DIC_EMBED_OBJ_STR      "string"
#define MONPE_XML_DIC_EMBED_OBJ_TXT_LEN  "length"
#define MONPE_XML_DIC_EMBED_OBJ_IMG      "image"

typedef struct _DicNode DicNode;
typedef DicNode DicTree;

typedef enum {
  DIC_NODE_TYPE_NODE = 0,
  DIC_NODE_TYPE_TEXT,
  DIC_NODE_TYPE_IMAGE,
  DIC_NODE_TYPE_END
} DicNodeType;

#define DNODE_MAX_HEIGHT 10

struct _DicNode
{
  /* parent class */
  GNode gnode;
  /* node property  */
  gboolean istop;
  char *name;
  int occurs;
  DicNodeType type;
  /* leaf property */
  int length;
  GList *objects;
};

/* default */

#define DNODE_DEFAULT_NAME_NODE   "NODE"
#define DNODE_DEFAULT_NAME_TEXT   "TEXT"
#define DNODE_DEFAULT_NAME_IMAGE  "IMAGE"
#define DNODE_DEFAULT_NODE_OCCURS 1
#define DNODE_DEFAULT_OCCURS 1
#define DNODE_DEFAULT_LENGTH 10
#define DNODE_IMAGE_PATH_SIZE 1024

#define DNODE_MAX_DEPTH 100

#define G_NODE(x)         ((GNode *)x)
#define DNODE(x)          ((DicNode *)x)
#define DNODE_PARENT(x)   (DNODE(G_NODE(x)->parent))
#define DNODE_CHILDREN(x) (DNODE(G_NODE(x)->children))
#define DNODE_NEXT(x)     (DNODE(G_NODE(x)->next))
#define DNODE_PREV(x)     (DNODE(G_NODE(x)->prev))
#define DNODE_DATA(x)     (DNODE(G_NODE(x)->data))

/***** dnode *****/
DicNode *dnode_new(char *name, 
                   int occurs,
                   DicNodeType type,
                   int length,
                   DicNode *parent,
                   DicNode *sibling);

void dnode_initialize(DicNode *node,
                      char *name, 
                      int occurs,
                      DicNodeType type,
                      int length,
                      DicNode *parent,
                      DicNode *sibling);

void dnode_update(DicNode *node);
void dnode_dump(DicNode *node);

int dnode_get_n_objects(DicNode *node);
int dnode_get_n_used_objects(DicNode *node);
gboolean dnode_data_is_used(DicNode *node);
int dnode_data_get_empty_index(DicNode *node);
int dnode_data_get_empty_nth_index(DicNode *node,int n);
gchar* dnode_data_get_longname(DicNode *node,int i);
void dnode_update_object_name(DicNode *node);
void dnode_update_node_name(DicNode *node,gchar *name);
int dnode_calc_total_occurs(DicNode *node);
int dnode_calc_occurs_upto_parent(DicNode *parent,DicNode *node);
int dnode_calc_occurs_upto_before_parent(DicNode *parent,DicNode *node);

void dnode_reset_objects(DicNode *node);
void dnode_reset_objects_recursive(DicNode *node);
GList * dnode_get_objects_recursive(DicNode *node,GList *list);
void dnode_set_object(DicNode *node,int index,gpointer object);
void dnode_unset_object(DicNode *node,gpointer object);

gboolean dnode_set_occurs(DicNode *node, int occurs);
void dnode_text_set_length(DicNode *node, int length);

/* dtree  */
DicNode* dtree_new(void);

void dtree_update(DicNode *tree);
void dtree_dump(DicNode *node);
void dtree_unlink(DicNode *node);
void dtree_move_before(DicNode *node,DicNode *parent,DicNode *sibling);
void dtree_move_after(DicNode *node,DicNode *parent,DicNode *sibling);

void dtree_new_from_xml(DicNode **dnode,xmlNodePtr xnode);
void dtree_write_to_xml(xmlNodePtr xnode,DicNode  *dnode);

DicNode* dtree_get_node_by_longname(DicNode *node,int *index,gchar *lname);
DicNode* dtree_set_data_by_longname(DicNode *node,gchar *lname,gpointer object);
gboolean dtree_is_valid_node(DicNode *dtree,DicNode *node);
gchar *dtree_conv_longname_to_xml(gchar*);
gchar *dtree_conv_longname_from_xml(gchar*);

#endif 

/*************************************************************
 * Local Variables:
 * mode:c
 * tab-width:2
 * indent-tabs-mode:nil
 * End:
 ************************************************************/
