#include <tcl.h>

/*
** msTclLayerInfo
**
** Specific information needed for managing this layer.
*/
typedef struct {
    Tcl_Interp *interp;     /* Tcl interpreter to use */
    long        rownum;      /* What row is the next to be read (for random access) */
    long        nrows;
    Tcl_Obj    *layerOpenCommandObj;
    Tcl_Obj    *getItemsCommandObj;
    Tcl_Obj    *getShapeCommandObj;
    Tcl_Obj    *geoQueryCommandObj;
    Tcl_Obj    *getAttributesCommandObj;
    Tcl_Obj    *getGeometryCommandObj;
    Tcl_Obj    *layerCloseCommandObj;
}
msTclPlugLayerInfo;

/*
** Prototypes
*/
void msTclFreeLayerInfo(layerObj *layer);
msTclPlugLayerInfo *msTclCreateLayerInfo(void);
int msTclParseData(layerObj *layer);

