/******************************************************************************
 *
 * Project:  MapServer
 * Purpose:  TclPlug CONNECTIONTYPE support.
 * Author:   Karl Lehenbauer <karllehenbauer@gmail.com>
 *
 * based on POSTGIS CONNECTIONTYPE support:
 * Author:   Paul Ramsey <pramsey@cleverelephant.ca>
 *           Dave Blasby <dblasby@gmail.com>
 *
 ******************************************************************************
 * Copyright (c) 2009 FlightAware LLC
 * Copyright (c) 2009 Karl Lehenbauer
 * Copyright (c) 2008 Paul Ramsey
 * Copyright (c) 2002 Refractions Research
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies of this Software or works derived from this Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 ****************************************************************************/

/*
** Some theory of operation:
**
** In your map file, define layers that are of CONNECTIONTYPE plugin
**  and a plugin of the maptclplug.so, maptclplug.dylib or maptclplug.dll
**  file.
**
** A Tcl interpreter is created and initialized when the first tclplugin
** layer is rendered.  It will remain until the mapscript library is disposed
** of.
**
** A global array, layer, is filled with information about layer, including:
**
**  * data - the contents of the DATA string in the map file layer definition.
**
**  * template - 
**
**  * name - name of the layer
**
**  * connection - string from the CONNECTION definition.
**
**  * plugin_library - 
**
**  * filter_item -
**
**  * style_item -
**
**  * requires -
**
**  * label_requires -
**
**  * classgroup
**
**
**  The CONNECTION string is evaluated by the Tcl interpreter at startup
**  of processing of this layer.  It should do something like a package
**  require or source a file that defines three procs: geo_query,
**  get_geometry, and get_attributes.
**
**  geo_query is called with the names of all the attribute fields
**  aka binding variables that are to be substituted.  It is expected
**  to in some way determine some geometry and attributes to be displayed
**  and to return the number of rows.
**
**  For each row from 0 to the number of rows - 1, get_geometry is called
**  with the row number requested.  It should return an empty list if there
**  is no geometry to be rendered (for some reason you decide that), or
**  with a list of lists of lon, lat (x, y) values.
**
**  If the layer is a point layer, the values will be drawn as points.
**
**  If it's a line layer, the pairs in each sublist will be drawn as distinct
**  lines.
**
**  If it's a polygon layer, the pairs in each sublist will be drawn as
**  (guess what) polygons.
**
**  If get_geometry returned any geometry, the mapsript tcl plugin will call
**  get_attributes with the row number to get the attributes.  Attributes
**  should be returned as a list of elements equal in length to and 
**  corresponding to the field names passed to geo_query.
**
**
*/

/* GNU needs this for strcasestr */
#define _GNU_SOURCE

#include <assert.h>
#include <string.h>
#include <mapserver.h>
#include <maptime.h>
#include "maptclplug.h"

#ifndef FLT_MAX
#define FLT_MAX 25000000.0
#endif

static Tcl_Interp *tclplug_global_interp = (Tcl_Interp *)NULL;
static int iCreatedTheTclInterpreter = 0;

#ifdef USE_TCLPLUG

// MS_CVSID("$Id$")

/*
** set_tcl_var_string - set a string into the global layer array
*/
static void
set_tcl_var_string (Tcl_Interp *interp, char *name, char *value) {
    if (value == NULL) {
        return;
    }

    if (Tcl_SetVar2 (interp, "layer", name, value, (TCL_GLOBAL_ONLY|TCL_LEAVE_ERR_MSG)) == NULL) {
        /* NB do something */
    }
    return;
}

/*
** set_tcl_var_int - set an integer into the global layer array
*/
static void
set_tcl_var_int (Tcl_Interp *interp, char *name, int value) {
    if (Tcl_SetVar2Ex (interp, "layer", name, Tcl_NewIntObj (value), (TCL_GLOBAL_ONLY|TCL_LEAVE_ERR_MSG)) == NULL) {
        /* NB do something */
    }
    return;
}

/*
** set_tcl_var_long - set a long into the global layer array
*/
static void
set_tcl_var_long (Tcl_Interp *interp, char *name, long value) {
    if (Tcl_SetVar2Ex (interp, "layer", name, Tcl_NewLongObj (value), (TCL_GLOBAL_ONLY|TCL_LEAVE_ERR_MSG)) == NULL) {
        /* NB do something */
    }
    return;
}

/*
** set_tcl_var_obj - set a tcl object into the global layer array
*/
static void
set_tcl_var_obj (Tcl_Interp *interp, char *name, Tcl_Obj *value) {
    if (Tcl_SetVar2Ex (interp, "layer", name, value, (TCL_GLOBAL_ONLY|TCL_LEAVE_ERR_MSG)) == NULL) {
        /* NB do something */
    }
    return;
}

/*
** log_traceback - log a Tcl traceback
*/
static void
log_traceback (Tcl_Interp *interp, int tclReturnCode)
{
    const char *errorInfo;

    errorInfo = Tcl_GetVar (interp, "errorInfo", TCL_GLOBAL_ONLY);
    if (errorInfo != NULL) {
	msDebug("tcl traceback: %s.\n", errorInfo);
    }
}

/* 
 *----------------------------------------------------------------------
 *
 * metaObjCmd --
 *
 *      This procedure is invoked to process the "meta" command.
 *
 *        meta names
 *        meta get var
 *        meta set var value
 *        meta unset var
 *        meta unset
 *
 *      See the user documentation for details on what it does.
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side effects:
 *      See the user documentation.
 *
 *----------------------------------------------------------------------
 */
        
    /* ARGSUSED */

static int
metaObjCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    int optIndex;
    layerObj *layer;

    static CONST char *options[] = {
	"get", "set", "exists", "unset", "names", (char *)NULL
    };

    enum options
    {
	OPT_GET, OPT_SET, OPT_EXISTS, OPT_UNSET, OPT_NAMES
    };

    if (objc < 2 || objc > 4) {
	Tcl_WrongNumArgs (interp, 1, objv, "option ?var?");
	return TCL_ERROR;
    }

    if (Tcl_GetIndexFromObj (interp, objv[1], options, "option", TCL_EXACT, &optIndex) != TCL_OK) {
	return TCL_ERROR;
    }

    layer = (layerObj *)Tcl_GetAssocData (interp, "pluglayer", NULL);
    if (layer == NULL) {
	Tcl_AddErrorInfo (interp, "tcl plugin software failure: associated data \"pluglayer\" not set in interpreter.");
	return TCL_ERROR;
    }

    switch ((enum options) optIndex) {
      case OPT_GET: {
	  char *key;
	  char *value;

	  if (objc != 3) {
	    Tcl_WrongNumArgs (interp, 2, objv, "var");
	    return TCL_ERROR;
	  }

	  key = Tcl_GetString (objv[2]);
	  value = msLookupHashTable (&layer->metadata, key);
	  if (value == NULL) {
	      /* lookup failed, return an empty string.
	       * they can use exists to see if it really exists if they want.
	       */
	      return TCL_OK;
	  }

	  Tcl_SetObjResult (interp, Tcl_NewStringObj (value, -1));
	  return TCL_OK;
      }

      case OPT_SET: {
	  char *key;
	  char *value;

	  if (objc != 4) {
	    Tcl_WrongNumArgs (interp, 2, objv, "var value");
	    return TCL_ERROR;
	  }

	  key = Tcl_GetString (objv[2]);
	  value = Tcl_GetString (objv[3]);

	  msInsertHashTable (&layer->metadata, key, value);
	  return TCL_OK;
      }

      case OPT_EXISTS: {
	  char *key;
	  char *value;

	  if (objc != 3) {
	    Tcl_WrongNumArgs (interp, 2, objv, "var");
	    return TCL_ERROR;
	  }

	  key = Tcl_GetString (objv[2]);
	  value = msLookupHashTable (&layer->metadata, key);

	  Tcl_SetObjResult (interp, Tcl_NewBooleanObj (value == NULL ? 0 : 1));
	  return TCL_OK;
      }

      case OPT_UNSET: {
	  char *key;

	  if (objc != 3) {
	    Tcl_WrongNumArgs (interp, 2, objv, "var");
	    return TCL_ERROR;
	  }

	  key = Tcl_GetString (objv[2]);
	  if (msLookupHashTable (&layer->metadata, key) == NULL) {
	      return TCL_OK;
	  }

	  msRemoveHashTable (&layer->metadata, key);
	  return TCL_OK;
      }

      case OPT_NAMES: {
	  const char *key;
	  Tcl_Obj *list = Tcl_NewObj ();

	  if (objc != 2) {
	    Tcl_WrongNumArgs (interp, 2, objv, "");
	    return TCL_ERROR;
	  }

	  for (key = msFirstKeyFromHashTable (&layer->metadata); key != NULL; key = msNextKeyFromHashTable (&layer->metadata, key)) {
	    if (Tcl_ListObjAppendElement (interp, list, Tcl_NewStringObj (key, -1)) == TCL_ERROR) {
		return TCL_ERROR;
	    }
	  }

	  Tcl_SetObjResult (interp, list);
	  return TCL_OK;
      }
    }

    return TCL_OK;
}

/*
** msTclPlugCloseConnection()
**
** Handler so we can clean up during a shutdown.
*/
void msTclPlugCloseConnection(void *pgconn) {
    if (iCreatedTheTclInterpreter) {
	Tcl_DeleteInterp (tclplug_global_interp);
    }
    return;
}

/*
** msTclPlugCreateLayerInfo()
**
** allocate and initialize an empty layerinfo (client data for the plugin)
**
*/
msTclPlugLayerInfo *msTclPlugCreateLayerInfo(void) {
    msTclPlugLayerInfo *layerinfo = malloc(sizeof(msTclPlugLayerInfo));

    layerinfo->interp = NULL;
    layerinfo->rownum = 0;
    layerinfo->nrows = 0;

    layerinfo->layerOpenCommandObj = NULL;
    layerinfo->getItemsCommandObj = NULL;
    layerinfo->getShapeCommandObj = NULL;
    layerinfo->getGeometryCommandObj = NULL;
    layerinfo->getAttributesCommandObj = NULL;
    layerinfo->layerCloseCommandObj = NULL;

    return layerinfo;
}

/*
** msTclPlugFreeLayerInfo() - shutdown callback
**
** Delete the Tcl interpreter and free the layerinfo (layer data
** that's private to the tcl plugin).
** 
**/
void msTclPlugFreeLayerInfo(layerObj *layer) {
    msTclPlugLayerInfo *layerinfo = NULL;

    layerinfo = (msTclPlugLayerInfo*)layer->layerinfo;

    if (layerinfo->layerOpenCommandObj != NULL) {
        Tcl_DecrRefCount (layerinfo->layerOpenCommandObj);
    }

    if (layerinfo->getItemsCommandObj != NULL) {
        Tcl_DecrRefCount (layerinfo->getItemsCommandObj);
    }

    if (layerinfo->getShapeCommandObj != NULL) {
        Tcl_DecrRefCount (layerinfo->getShapeCommandObj);
    }

    if (layerinfo->getGeometryCommandObj != NULL) {
        Tcl_DecrRefCount (layerinfo->getGeometryCommandObj);
    }

    if (layerinfo->getAttributesCommandObj != NULL) {
        Tcl_DecrRefCount (layerinfo->getAttributesCommandObj);
    }

    if (layerinfo->layerCloseCommandObj != NULL) {
        Tcl_DecrRefCount (layerinfo->layerCloseCommandObj);
    }


    if ( layerinfo->interp ) {
	/* only delete the interpreter if it's not the global one */
	if (layerinfo->interp != tclplug_global_interp) {
	    Tcl_DeleteInterp (layerinfo->interp);
	}
    }

    free(layerinfo);
    layer->layerinfo = NULL;
}

/*
** tclplugNoticeHandler()
**
** 
**
*/
void tclplugNoticeHandler(void *arg, const char *message) {
    layerObj *lp;
    lp = (layerObj*)arg;

    if (lp->debug) {
        msDebug("%s\n", message);
    }
}

/* find the bounds of the shape */
static void find_bounds(shapeObj *shape)
{
    int     t, u;
    int     first_one = 1;

    for(t = 0; t < shape->numlines; t++) {
        for(u = 0; u < shape->line[t].numpoints; u++) {
            if(first_one) {
                shape->bounds.minx = shape->line[t].point[u].x;
                shape->bounds.maxx = shape->line[t].point[u].x;

                shape->bounds.miny = shape->line[t].point[u].y;
                shape->bounds.maxy = shape->line[t].point[u].y;
                first_one = 0;
            } else {
                if(shape->line[t].point[u].x < shape->bounds.minx) {
                    shape->bounds.minx = shape->line[t].point[u].x;
                }
                if(shape->line[t].point[u].x > shape->bounds.maxx) {
                    shape->bounds.maxx = shape->line[t].point[u].x;
                }

                if(shape->line[t].point[u].y < shape->bounds.miny) {
                    shape->bounds.miny = shape->line[t].point[u].y;
                }
                if(shape->line[t].point[u].y > shape->bounds.maxy) {
                    shape->bounds.maxy = shape->line[t].point[u].y;
                }
            }
        }
    }
}

/*
** msTclPlugParseData()
**
** set up some values in a layer array in the tcl interpreter:
**    data, name, connection, plugin_library
**
*/
int msTclPlugParseData(layerObj *layer) {
    msTclPlugLayerInfo *layerinfo;
    Tcl_Interp *interp;

    assert(layer != NULL);
    assert(layer->layerinfo != NULL);

    if (layer->debug) {
        msDebug("msTclPlugParseData called.\n");
    }

    layerinfo = (msTclPlugLayerInfo*)(layer->layerinfo);

    assert(layerinfo->interp);
    interp = layerinfo->interp;

    set_tcl_var_string (interp, "data", layer->data);
    set_tcl_var_string (interp, "template", layer->template);
    set_tcl_var_string (interp, "name", layer->name);
    set_tcl_var_string (interp, "connection", layer->connection);
    set_tcl_var_string (interp, "plugin_library", layer->plugin_library);
    set_tcl_var_string (interp, "filter_item", layer->filteritem);
    set_tcl_var_string (interp, "style_item", layer->styleitem);
    set_tcl_var_string (interp, "requires", layer->requires);
    set_tcl_var_string (interp, "label_requires", layer->labelrequires);
    set_tcl_var_string (interp, "classgroup", layer->classgroup);

    /* make the type symbolic rather than numeric */
    switch (layer->type) {
      case MS_LAYER_POINT: {
	set_tcl_var_string (interp, "type", "point");
	break;
      }

      case MS_LAYER_LINE: {
	set_tcl_var_string (interp, "type", "line");
	break;
      }

      case MS_LAYER_POLYGON: {
	set_tcl_var_string (interp, "type", "polygon");
	break;
      }

      case MS_LAYER_ANNOTATION: {
	set_tcl_var_string (interp, "type", "annotation");
	break;
      }

      case MS_LAYER_QUERY: {
	set_tcl_var_string (interp, "type", "query");
	break;
      }

      case MS_LAYER_CHART: {
	set_tcl_var_string (interp, "type", "chart");
	break;
      }

      default: {
	set_tcl_var_string (interp, "type", "unknown");
	break;
      }
    }

    return MS_SUCCESS;
}

/*
** msTclPlugReadShape()
**
** invoke "get_geometry" in the Tcl interpreter with the row number being
** requested as an argument.
** 
** The result is a list of zero or more lists of zero or more coordinate pairs
**
*/
int msTclPlugReadShape(layerObj *layer, shapeObj *shape) {

    msTclPlugLayerInfo *layerinfo = NULL;
    lineObj line = {0, NULL};

    Tcl_Obj *getCommand[2];
    Tcl_Interp *interp;
    int tclResult;
    Tcl_Obj **geoListObjv;
    int       geoListObjc;
    int       geoset;
    int       v;
    int       haveGeometry = 0;

    if (layer->debug) {
        msDebug("msTclPlugReadShape called.\n");
    }

    assert(layer->layerinfo != NULL);
    layerinfo = (msTclPlugLayerInfo*) layer->layerinfo;

    assert (layerinfo->interp);
    interp = layerinfo->interp;

    getCommand[0] = layerinfo->getGeometryCommandObj;
    Tcl_IncrRefCount (layerinfo->getGeometryCommandObj);
    getCommand[1] = Tcl_NewIntObj (layerinfo->rownum);
    Tcl_IncrRefCount (getCommand[1]);

    tclResult =  Tcl_EvalObjv (interp, 2, getCommand, 0);
    Tcl_DecrRefCount (getCommand[1]);

    if ( layer->debug > 1 ) {
        msDebug("msTclPlugLayerReadShape eval status: %d\n", tclResult);
    }

    /* Something went wrong. */
    if (tclResult == TCL_ERROR) {
	Tcl_AddErrorInfo (interp, " while executing layer plugin");

        msSetError(MS_QUERYERR, "Error executing get_geometry: %s.", 
	    "msTclPlugLayerReadShape()", 
	    Tcl_GetString (Tcl_GetObjResult (interp)));
	if (layer->debug) {
	    log_traceback (interp, tclResult);
	}

	Tcl_BackgroundError (interp);
        return MS_FAILURE;
    }

    /* Retrieve the geometry lists. */

    if (Tcl_ListObjGetElements (interp, Tcl_GetObjResult (interp), &geoListObjc, &geoListObjv) == TCL_ERROR) {
	Tcl_AddErrorInfo (interp, " while cracking geometry lists");
        msSetError(MS_QUERYERR, "failed to obtain geometry lists: %s.", 
	    "msTclPlugReadShape()",
	    Tcl_GetString (Tcl_GetObjResult (interp)));

	if (layer->debug) {
	    log_traceback (interp, tclResult);
	}

	Tcl_BackgroundError (interp);
	return MS_FAILURE;
    }

    shape->type = MS_SHAPE_NULL;

    if (geoListObjc > 0) {
	switch (layer->type) {
	  case MS_LAYER_POINT: {
	    shape->type = MS_SHAPE_POINT;
	    break;
	  }

	  case MS_LAYER_LINE: {
	    shape->type = MS_SHAPE_LINE;
	    break;
	  }

	  case MS_LAYER_POLYGON: {
	    shape->type = MS_SHAPE_POLYGON;
	    break;
	  }

	  default: {
	    if (layer->debug > 1) {
		msDebug( "Ignoring unknown layer type in msTclPlugReadShape.\n" );
	    }
	    break;
	  }
	}
    }

    if( layer->debug > 1 ) {
	msDebug("msTclPlugReadShape: %d geometry lists.\n", geoListObjc);
    }

    for (geoset = 0; geoset < geoListObjc; geoset++) {
	Tcl_Obj **pairsObjv;
	int       pairsObjc;

	if (Tcl_ListObjGetElements (interp, geoListObjv[geoset], &pairsObjc, &pairsObjv) == TCL_ERROR) {
	    Tcl_AddErrorInfo (interp, " while cracking point pairs from geometry list");
	    msSetError(MS_QUERYERR, "failed to obtain geometry sublist: %s.", 
	        "msTclPlugReadShape()",
		Tcl_GetString (Tcl_GetObjResult (interp)));
	    Tcl_BackgroundError (interp);
	    return MS_FAILURE;
	}

	if (pairsObjc == 0) continue;

	if (pairsObjc % 2 == 1) {
	    Tcl_SetObjResult (interp, Tcl_NewStringObj ("odd number of elements in geometry sublist: '", -1));
	    Tcl_AddErrorInfo (interp, Tcl_GetString(geoListObjv[geoset]));
	    Tcl_AddErrorInfo (interp, "', returned from get_geometry");
	    msSetError(MS_QUERYERR, "odd number of elements in geometry sublist.", "msTclPlugReadShape()");
	    Tcl_BackgroundError (interp);
	    return MS_FAILURE;
	}

	if( layer->debug > 1 ) {
	    msDebug("msTclPlugReadShape: %d elements in geometry list %d.\n", pairsObjc, geoListObjc);
	}

	line.numpoints = pairsObjc / 2;
	line.point = (pointObj*) malloc(sizeof(pointObj) * line.numpoints);

	for (v = 0; v < line.numpoints; v++) {
	    haveGeometry = 1;
	    if (Tcl_GetDoubleFromObj (interp, pairsObjv[v * 2], &line.point[v].x) == TCL_ERROR) {
		Tcl_AddErrorInfo (interp, " while reading point x coordinate");
		msSetError(MS_QUERYERR, "Failed to obtain x value from list: %s.", 
		    "msTclPlugLayerReadShape()", 
		    Tcl_GetString (Tcl_GetObjResult (interp)));
		Tcl_BackgroundError (interp);
		return MS_FAILURE;
	    }

	    if (Tcl_GetDoubleFromObj (interp, pairsObjv[v * 2 + 1], &line.point[v].y) == TCL_ERROR) {
		Tcl_AddErrorInfo (interp, " while reading point y coordinate");
		msSetError(MS_QUERYERR, "Failed to obtain y value from list: %s.", 
		    "msTclPlugLayerReadShape()", 
		    Tcl_GetString (Tcl_GetObjResult (interp)));
		Tcl_BackgroundError (interp);
		return MS_FAILURE;
	    }
	}

	msAddLine(shape, &line);
	free(line.point);
    }

    if (!haveGeometry) {
	shape->type = MS_SHAPE_NULL;
    }

    if (shape->type != MS_SHAPE_NULL) {
        int t;
        long uid;

	int listObjc;
	Tcl_Obj **listObjv;
        /* Found a drawable shape, so now retreive the attributes. */

        shape->values = (char**) malloc(sizeof(char*) * layer->numitems);

	getCommand[0] = layerinfo->getAttributesCommandObj;
	Tcl_IncrRefCount (layerinfo->getAttributesCommandObj);
	getCommand[1] = Tcl_NewIntObj (layerinfo->rownum);
	Tcl_IncrRefCount (getCommand[1]);

	tclResult =  Tcl_EvalObjv (interp, 2, getCommand, 0);
	Tcl_DecrRefCount (getCommand[1]);

	/* Something went wrong. */
	if (tclResult == TCL_ERROR) {
	    Tcl_AddErrorInfo (interp, " while executing layer plugin");
	    msSetError(MS_QUERYERR, "Error executing get_attributes: %s.", 
	        "msTclPlugLayerReadShape()", 
		Tcl_GetString (Tcl_GetObjResult (interp)));
	    if (layer->debug) {
		log_traceback (interp, tclResult);
	    }
	    Tcl_BackgroundError (interp);
	    return MS_FAILURE;
	}

	if (Tcl_ListObjGetElements (interp, Tcl_GetObjResult (interp), &listObjc, &listObjv) == TCL_ERROR) {
	    Tcl_AddErrorInfo (interp, " while cracking attribute list");
	    msSetError(MS_QUERYERR, "error getting list elements: %s.", 
	        "msTclPlugReadShape()", 
		Tcl_GetString (Tcl_GetObjResult (interp)));
	    Tcl_BackgroundError (interp);
	    return MS_FAILURE;
	}

	if (layer->numitems != listObjc) {
	    Tcl_AddErrorInfo (interp, "number of attributes didn't match what was expected");
	    msSetError(MS_QUERYERR, "get_attributes mismatch, wanted %d, got %d.", 
	        "msTclPlugLayerReadShape()", 
		layer->numitems, 
		listObjc);
	    Tcl_BackgroundError (interp);
	    return MS_FAILURE;
	}

        for ( t = 0; t < layer->numitems; t++) {
	    shape->values[t] = strdup (Tcl_GetString (listObjv[t]));
            if( layer->debug > 1 ) {
                msDebug("msTclPlugReadShape: [%s] \"%s\"\n", layer->items[t], shape->values[t]);
            }
        }

	/* NB do more to make the uid here */
	uid = layerinfo->rownum;
        shape->index = uid;

        if( layer->debug > 2 ) {
            msDebug("msTclPlugReadShape: [index] %d\n",  shape->index);
        }

        shape->numvalues = layer->numitems;

        find_bounds(shape);
    }
    
    return MS_SUCCESS;
}

#endif /* USE_TCLPLUG */


/*
** msTclPlugLayerOpen()
**
** Registered vtable->LayerOpen function.
**
** Construct the tclplugin-private layerinfo
**
** Create and initialize the Tcl interpreter if it doesn't already exist.
*/
int msTclPlugLayerOpen(layerObj *layer) {
#ifdef USE_TCLPLUG
    msTclPlugLayerInfo  *layerinfo;
    Tcl_Interp *interp = NULL;
    Tcl_Obj    *openCommand[2];
    int         tclResult;
    char *namespace;

    assert(layer != NULL);

    if (layer->debug) {
        msDebug("msTclPlugLayerOpen called: layer '%s', data '%s'.\n", 
	    layer->name,
	    layer->data);
    }

    if (layer->layerinfo) {
        if (layer->debug) {
            msDebug("msTclPlugLayerOpen: Layer is already open!\n");
        }
        return MS_SUCCESS;  /* already open */
    }

    if (!layer->data) {
        msSetError(MS_QUERYERR, "Nothing specified in DATA statement.", "msTclPlugLayerOpen()");
        return MS_FAILURE;
    }

    /*
    ** Initialize the layerinfo 
    **/
    layerinfo = msTclPlugCreateLayerInfo();

    /*
    ** create or attach to a tcl interpreter
    **/
    if (tclplug_global_interp != (Tcl_Interp *)NULL) {
        interp = tclplug_global_interp;
    } else {
	char *interpreterString;

        // if we're running as Tcl mapscript, we probably want to call back
        // into our existing interpreter.  if you were using something else,
        // but you wanted the Tcl plugin, you'd probably want to create the Tcl
        // interpreter here

	// see if we have an interpreter key in the layer metadata.
	// god this is gross and i've gone to extreme measures in the
	// past to never pass a pointer through tcl as a string but
	// i see no way out until mapscript has a mechanism to move
	// an opaque pointer from a SWIG-based interface to a layer plugin

	interpreterString = msLookupHashTable (&layer->metadata, "interpreter");
	if (interpreterString != NULL) {
	    char *endPtr;
	    long long ll;

	    ll = strtoll (interpreterString, &endPtr, 16);
	    if (*endPtr == '\0') {
		// found the interpreter string and converted it successfully
		interp = (Tcl_Interp *) ll;
	    }
	}

	// still no interpreter?  create one
	if (interp == NULL) {
	    interp = Tcl_CreateInterp();
	    if (Tcl_Init (interp) == TCL_ERROR) {
		msSetError(MS_QUERYERR, "tcl init failed: %s.",
			"msTclJoinConnect()",
			Tcl_GetStringResult (interp));
		return MS_FAILURE;
	    }

	    iCreatedTheTclInterpreter = 1;
	}

	if (Tcl_LinkVar (interp, "layerDebug", (char *)&layer->debug, TCL_LINK_INT) == TCL_ERROR) {
            msSetError(MS_QUERYERR, "Error linking tcl init debug var: %s.",
                    "msTclJoinConnect()",
                    Tcl_GetStringResult (interp));
            return MS_FAILURE;
	}

        tclplug_global_interp = interp;

	Tcl_CreateObjCommand (interp, "meta", (Tcl_ObjCmdProc *)metaObjCmd, NULL, NULL);
    }

    layerinfo->interp = interp;

    /* Save the layerinfo in the layerObj. */
    layer->layerinfo = (void*)layerinfo;

    /* stash a pointer to the layer with Tcl so "meta" can find it */
    Tcl_SetAssocData (interp, "pluglayer", NULL, (ClientData)layer);

    namespace = msLookupHashTable (&layer->metadata, "namespace");
    if (namespace == NULL) {
	layerinfo->layerOpenCommandObj = Tcl_NewObj();
	layerinfo->geoQueryCommandObj = Tcl_NewObj();
	layerinfo->getItemsCommandObj = Tcl_NewObj();
	layerinfo->getShapeCommandObj = Tcl_NewObj();
	layerinfo->getGeometryCommandObj = Tcl_NewObj();
	layerinfo->getAttributesCommandObj = Tcl_NewObj();
	layerinfo->layerCloseCommandObj = Tcl_NewObj();
    } else {
	layerinfo->layerOpenCommandObj = Tcl_NewStringObj (namespace, -1);
	layerinfo->geoQueryCommandObj = Tcl_NewStringObj (namespace, -1);
	layerinfo->getItemsCommandObj = Tcl_NewStringObj (namespace, -1);
	layerinfo->getShapeCommandObj = Tcl_NewStringObj (namespace, -1);
	layerinfo->getGeometryCommandObj = Tcl_NewStringObj (namespace, -1);
	layerinfo->getAttributesCommandObj = Tcl_NewStringObj (namespace, -1);
	layerinfo->layerCloseCommandObj = Tcl_NewStringObj (namespace, -1);
    }

    Tcl_AppendToObj (layerinfo->layerOpenCommandObj, "::layer_open", -1);
    Tcl_IncrRefCount (layerinfo->layerOpenCommandObj);

    Tcl_AppendToObj (layerinfo->getItemsCommandObj, "::get_items", -1);
    Tcl_IncrRefCount (layerinfo->getItemsCommandObj);

    Tcl_AppendToObj (layerinfo->getShapeCommandObj, "::get_shape", -1);
    Tcl_IncrRefCount (layerinfo->getShapeCommandObj);

    Tcl_AppendToObj (layerinfo->geoQueryCommandObj, "::geo_query", -1);
    Tcl_IncrRefCount (layerinfo->geoQueryCommandObj);

    Tcl_AppendToObj (layerinfo->getGeometryCommandObj, "::get_geometry", -1);
    Tcl_IncrRefCount (layerinfo->getGeometryCommandObj);

    Tcl_AppendToObj (layerinfo->getAttributesCommandObj, 
        "::get_attributes", -1);
    Tcl_IncrRefCount (layerinfo->getAttributesCommandObj);

    Tcl_AppendToObj (layerinfo->layerCloseCommandObj, "::layer_close", -1);
    Tcl_IncrRefCount (layerinfo->layerCloseCommandObj);

    /* Fill out layerinfo with our current DATA state (and force it). */
    if ( msTclPlugParseData(layer) != MS_SUCCESS) {
	msSetError( MS_MISCERR,
		    "msTclPlugParseData() failed.",
		    "msTclPlugLayerOpen()");
        return MS_FAILURE;
    }

    openCommand[0] = layerinfo->layerOpenCommandObj;
    Tcl_IncrRefCount (openCommand[0]);
    openCommand[1] = Tcl_NewStringObj (layer->name, -1);
    Tcl_IncrRefCount (openCommand[1]);

    tclResult =  Tcl_EvalObjv (interp, 2, openCommand, 0);
    if (tclResult == TCL_ERROR) {
	Tcl_AddErrorInfo (interp, " while executing layer plugin");

        msSetError(MS_QUERYERR, "Error opening layer: %s.", 
	"msTclPlugLayerLayerOpen()", 
	Tcl_GetString (Tcl_GetObjResult (interp)));

	if (layer->debug) {
	    msDebug("msTclPlugLayerOpen layer_open callout failed: %s (ignored).\n", 
	        Tcl_GetString (Tcl_GetObjResult (interp)));
	    log_traceback (interp, TCL_ERROR);
	}

	Tcl_BackgroundError (interp);
	return MS_FAILURE;
    }
    Tcl_DecrRefCount (openCommand[1]);

    return MS_SUCCESS;
#else
    msSetError( MS_MISCERR,
                "TclPlug support is not available.",
                "msTclPlugLayerOpen()");
    return MS_FAILURE;
#endif
}

/*
** msTclPlugLayerClose()
**
** Registered vtable->LayerClose function.
*/
int msTclPlugLayerClose(layerObj *layer) {
#ifdef USE_TCLPLUG
    msTclPlugLayerInfo *layerinfo;
    Tcl_Interp *interp;

    if (layer->debug) {
        msDebug("msTclPlugLayerClose called.\n");
    }

    assert(layer->layerinfo != NULL);
    layerinfo = (msTclPlugLayerInfo*) layer->layerinfo;

    assert (layerinfo->interp);
    interp = layerinfo->interp;

    /* try to invoke a layer_close proc but don't get super upset if it isn't
     * there
     */
    if (Tcl_EvalObjEx (interp, layerinfo->layerCloseCommandObj, TCL_GLOBAL_ONLY) == TCL_ERROR) {
	Tcl_AddErrorInfo (interp, " while executing layer plugin");

        msSetError(MS_QUERYERR, "Error closing layer: %s.", 
	"msTclPlugLayerLayerClose()", 
	Tcl_GetString (Tcl_GetObjResult (interp)));

	if (layer->debug) {
	    msDebug("msTclPlugLayerClose layer_close callout failed: %s (ignored).\n", Tcl_GetString (Tcl_GetObjResult (interp)));
	    log_traceback (interp, TCL_ERROR);
	}

	Tcl_BackgroundError (interp);
    }
    
    if( layer->layerinfo ) {
        msTclPlugFreeLayerInfo(layer); 
    }

    return MS_SUCCESS;
#else
    msSetError( MS_MISCERR,
                "TclPlug support is not available.",
                "msTclPlugLayerClose()");
    return MS_FAILURE;
#endif
}


/*
** msTclPlugLayerIsOpen()
**
** Registered vtable->LayerIsOpen function.
*/
int msTclPlugLayerIsOpen(layerObj *layer) {
#ifdef USE_TCLPLUG

    if (layer->debug) {
        msDebug("msTclPlugLayerIsOpen called.\n");
    }

    if (layer->layerinfo)
        return MS_TRUE;
    else
        return MS_FALSE;
#else
    msSetError( MS_MISCERR,
                "TclPlug support is not available.",
                "msTclPlugLayerIsOpen()");
    return MS_FAILURE;
#endif
}


/*
** msTclPlugLayerFreeItemInfo()
**
** Registered vtable->LayerFreeItemInfo function.
*/
void msTclPlugLayerFreeItemInfo(layerObj *layer) {
#ifdef USE_TCLPLUG
    if (layer->debug) {
        msDebug("msTclPlugLayerFreeItemInfo called.\n");
    }

    if (layer->iteminfo) {
        free(layer->iteminfo);
    }
    layer->iteminfo = NULL;
#endif
}

/*
** msTclPlugLayerInitItemInfo()
**
** Registered vtable->LayerInitItemInfo function.
** Our iteminfo is list of indexes from 1..numitems.
*/
int msTclPlugLayerInitItemInfo(layerObj *layer) {
#ifdef USE_TCLPLUG
    int i;
    int *itemindexes ;

    if (layer->debug) {
        msDebug("msTclPlugLayerInitItemInfo called.\n");
    }

    if (layer->numitems == 0) {
        return MS_SUCCESS;
    }

    if (layer->iteminfo) {
        free(layer->iteminfo);
    }

    layer->iteminfo = malloc(sizeof(int) * layer->numitems);
    if (!layer->iteminfo) {
        msSetError(MS_MEMERR, "Out of memory.", "msTclPlugLayerInitItemInfo()");
        return MS_FAILURE;
    }
	
    itemindexes = (int*)layer->iteminfo;
    for (i = 0; i < layer->numitems; i++) {
        itemindexes[i] = i; /* Last item is always the geometry. The rest are non-geometry. */
    }

    return MS_SUCCESS;
#else
    msSetError( MS_MISCERR,
                "TclPlug support is not available.",
                "msTclPlugLayerInitItemInfo()");
    return MS_FAILURE;
#endif
}

/*
** msTclPlugSnagRectangleAndUID()
**
** Returns malloc'ed char* that must be freed by caller.
*/
void msTclPlugSnagRectangleAndUID (layerObj *layer, rectObj *rect, long *uid) {
    msTclPlugLayerInfo *layerinfo = 0;

    Tcl_Interp *interp;
    Tcl_Obj *listObjv[4];

    if (layer->debug) {
        msDebug("msTclPlugSnagRectangleAndUID called.\n");
    }

    assert( layer->layerinfo != NULL);
        
    layerinfo = (msTclPlugLayerInfo *)layer->layerinfo;

    assert (layerinfo->interp);
    interp = layerinfo->interp;

    if (rect != NULL) {
	listObjv[0] = Tcl_NewDoubleObj (rect->minx);
	listObjv[1] = Tcl_NewDoubleObj (rect->miny);
	listObjv[2] = Tcl_NewDoubleObj (rect->maxx);
	listObjv[3] = Tcl_NewDoubleObj (rect->maxy);

	set_tcl_var_obj (interp, "rectangle", Tcl_NewListObj (4, listObjv));
    }

    if (uid) {
	set_tcl_var_long (interp, "uid", *uid);
    }
}

Tcl_Obj *msTclPlugLayerItemNamesToList(layerObj *layer) {
    Tcl_Obj **listObjv = (Tcl_Obj **)ckalloc (layer->numitems * sizeof (Tcl_Obj *));
    Tcl_Obj *resultList;
    int t;

    for (t = 0; t < layer->numitems; t++) {
	listObjv[t] = Tcl_NewStringObj (layer->items[t], -1);
    }

    resultList = Tcl_NewListObj (layer->numitems, listObjv);
    ckfree ((void *)listObjv);
    return resultList;
}


/*
** msTclPlugLayerWhichShapes()
**
** Invokes "geo_query" in the Tcl interpreter and expects to be
** returned with the number of rows.
**
** Parsing / Preparation can occur in that proc too, of course.
**
** Registered vtable->LayerWhichShapes function.
*/
int msTclPlugLayerWhichShapes(layerObj *layer, rectObj rect) {
#ifdef USE_TCLPLUG
    msTclPlugLayerInfo *layerinfo = NULL;
    int tclResult;
    Tcl_Interp *interp;
    Tcl_Obj *shapeCommand[2];

    assert(layer != NULL);
    assert(layer->layerinfo != NULL);

    if (layer->debug) {
        msDebug("msTclPlugLayerWhichShapes called.\n");
    }

    /* 
    ** This comes *after* parsedata, because parsedata fills in 
    ** layer->layerinfo.
    */
    layerinfo = (msTclPlugLayerInfo*) layer->layerinfo;
    assert (layerinfo->interp);

    interp = layerinfo->interp;

    msTclPlugSnagRectangleAndUID(layer, &rect, NULL);

    /* eval the connection string to source a file or
     * require a package or something
     */
    if (Tcl_Eval (interp, layer->connection) == TCL_ERROR) {
        msSetError(MS_QUERYERR, "Error executing connection script: %s: %s.", "msTclPlugLayerWhichShapes()", layer->connection, Tcl_GetString (Tcl_GetObjResult (interp)));

	Tcl_AddErrorInfo (interp, " while executing connection command '");
	Tcl_AddErrorInfo (interp, layer->connection);
	Tcl_AddErrorInfo (interp, "', while executing layer plugin");
	Tcl_BackgroundError (interp);

	if (layer->debug) {
	    log_traceback (interp, TCL_ERROR);
	}

        return MS_FAILURE;
    }

    shapeCommand[0] = layerinfo->geoQueryCommandObj;
    Tcl_IncrRefCount (shapeCommand[0]);
    shapeCommand[1] = msTclPlugLayerItemNamesToList(layer);
    Tcl_IncrRefCount (shapeCommand[1]);

    tclResult =  Tcl_EvalObjv (interp, 2, shapeCommand, 0);

    Tcl_DecrRefCount (shapeCommand[1]);

    if ( layer->debug > 1 ) {
        msDebug("msTclPlugLayerWhichShapes query status: %d\n", tclResult);
    }

    /* Something went wrong. */
    if (tclResult == TCL_ERROR) {
        msSetError(MS_QUERYERR, "Error executing geo_query: %s.", "msTclPlugLayerWhichShapes()", Tcl_GetString (Tcl_GetObjResult (interp)));

	Tcl_AddErrorInfo (interp, " while executing layer plugin geo_query function");
	Tcl_BackgroundError (interp);
	if (layer->debug) {
	    log_traceback (interp, tclResult);
	}
        return MS_FAILURE;
    }

    if (Tcl_GetLongFromObj (interp, Tcl_GetObjResult (interp), &layerinfo->nrows) == TCL_ERROR) {
        msSetError(MS_QUERYERR, "Error getting row count: %s.", "msTclPlugLayerWhichShapes()", Tcl_GetString (Tcl_GetObjResult (interp)));

	Tcl_AddErrorInfo (interp, " while reading row count returned by layer plugin geo_query function");
	Tcl_BackgroundError (interp);

	return MS_FAILURE;
    }

    if ( layer->debug ) {
        msDebug("msTclPlugLayerWhichShapes got %d rows in result.\n", layerinfo->nrows);
    }

    layerinfo->rownum = 0;

    return MS_SUCCESS;
#else
    msSetError( MS_MISCERR,
                "TclPlug support is not available.",
                "msTclPlugLayerWhichShapes()");
    return MS_FAILURE;
#endif
}

/*
** msTclPlugLayerNextShape()
**
** Registered vtable->LayerNextShape function.
*/
int msTclPlugLayerNextShape(layerObj *layer, shapeObj *shape) {
#ifdef USE_TCLPLUG
    msTclPlugLayerInfo  *layerinfo;

    if (layer->debug) {
        msDebug("msTclPlugLayerNextShape called.\n");
    }

    assert(layer != NULL);
    assert(layer->layerinfo != NULL);

    layerinfo = (msTclPlugLayerInfo*) layer->layerinfo;

    shape->type = MS_SHAPE_NULL;

    /* 
    ** Roll through pgresult until we hit non-null shape (usually right away).
    */
    while (shape->type == MS_SHAPE_NULL) {
        if (layerinfo->rownum < layerinfo->nrows) {
            int rv;
            /* Retrieve this shape. */
            rv = msTclPlugReadShape(layer, shape);
	    if (rv != MS_SUCCESS) {
		return rv;
	    }

            if( shape->type != MS_SHAPE_NULL ) {
                (layerinfo->rownum)++; /* move to next shape */
                return MS_SUCCESS;
            } else {
                (layerinfo->rownum)++; /* move to next shape */
            }
        } else {
            return MS_DONE;
        }
    }

    /* Found nothing, clean up and exit. */
    msFreeShape(shape);

    return MS_FAILURE;
#else
    msSetError( MS_MISCERR,
                "TclPlug support is not available.",
                "msTclPlugLayerNextShape()");
    return MS_FAILURE;
#endif
}

/*
** msTclPlugLayerGetShape()
**
** Registered vtable->LayerGetShape function. We ignore the 'tile' 
** parameter, as it means nothing to us.
*/
int msTclPlugLayerGetShape(layerObj *layer, shapeObj *shape, int tile, long record) {
#ifdef USE_TCLPLUG
    msTclPlugLayerInfo *layerinfo;
    int result, num_tuples;
    Tcl_Interp *interp;

    assert(layer != NULL);
    assert(layer->layerinfo != NULL);

    if (layer->debug) {
        msDebug("msTclPlugLayerGetShape called for record = %i\n", record);
    }

    /* 
    ** This comes *after* parsedata, because parsedata fills in 
    ** layer->layerinfo.
    */
    layerinfo = (msTclPlugLayerInfo*) layer->layerinfo;

    assert (layerinfo->interp);
    interp = layerinfo->interp;

    msTclPlugSnagRectangleAndUID(layer, 0, &record);

    Tcl_IncrRefCount (layerinfo->getShapeCommandObj);
    /* now invoke the call to get the list of available items */
    if (Tcl_EvalObjEx (interp, layerinfo->getShapeCommandObj, TCL_GLOBAL_ONLY) == TCL_ERROR) {
    /* Something went wrong. */
        msSetError(MS_QUERYERR, "Error executing get_shape: %s.", "msTclPlugLayerGetShape()", Tcl_GetString (Tcl_GetObjResult (interp)));
	if (layer->debug) {
	    log_traceback (interp, TCL_ERROR);
	}
        return MS_FAILURE;
    }

    layerinfo->rownum = 0; /* Only return one result. */

    /* We don't know the shape type until we read the geometry. */
    shape->type = MS_SHAPE_NULL;

    num_tuples = layerinfo->nrows;
    if (layer->debug) {
        msDebug("msTclPlugLayerGetShape number of records: %d\n", num_tuples);
    }

    if (num_tuples > 0) {
         result = msTclPlugReadShape(layer, shape);
	 if (result != MS_SUCCESS) {
	     return result;
	 }
    }

    return (shape->type == MS_SHAPE_NULL) ? MS_FAILURE : ( (num_tuples > 0) ? MS_SUCCESS : MS_DONE );
#else
    msSetError( MS_MISCERR,
                "TclPlug support is not available.",
                "msTclPlugLayerGetShape()");
    return MS_FAILURE;
#endif
}

/*
** msTclPlugLayerGetItems()
**
** Registered vtable->LayerGetItems function. Query for
** column information about the requested layer. 
**
** Works by calling a proc 
*/
int msTclPlugLayerGetItems(layerObj *layer) {
#ifdef USE_TCLPLUG
    msTclPlugLayerInfo *layerinfo;
    int item_num;

    Tcl_Interp *interp;
    Tcl_Obj  **listObjv;
    int        listObjc;

    assert(layer != NULL);
    assert(layer->layerinfo != NULL);
    
    layerinfo = (msTclPlugLayerInfo*) layer->layerinfo;
    
    assert(layerinfo->interp);
    interp = layerinfo->interp;

    if (layer->debug) {
        msDebug("msTclPlugLayerGetItems called.\n");
    }

    layerinfo = (msTclPlugLayerInfo*) layer->layerinfo;

    Tcl_IncrRefCount (layerinfo->getItemsCommandObj);
    /* now invoke the call to get the list of available items */
    if (Tcl_EvalObjEx (interp, layerinfo->getItemsCommandObj, TCL_GLOBAL_ONLY) == TCL_ERROR) {
        msSetError(MS_QUERYERR, 
	    "Error invoking %s: %s.",
	    "msTclPlugLayerGetItems()",
	    Tcl_GetString (layerinfo->getItemsCommandObj),
	    Tcl_GetString (Tcl_GetObjResult (interp)));

	if (layer->debug) {
	    log_traceback (interp, TCL_ERROR);
	}
        return MS_FAILURE;
    }

    /* success... now crack the items out of the item list */
    if (Tcl_ListObjGetElements (interp, Tcl_GetObjResult (interp), &listObjc, &listObjv) == TCL_ERROR) {
        msSetError(MS_QUERYERR, "error getting list elements: %s", 
	    "msTclPlugLayerGetItems()", 
	    Tcl_GetString (Tcl_GetObjResult (interp)));

	if (layer->debug) {
	    log_traceback (interp, TCL_ERROR);
	}
        return MS_FAILURE;
    }

    /* success... now copy the names of the available items back to mapscript */
    layer->numitems = listObjc;
    layer->items = malloc(sizeof(char*) * listObjc);

    for (item_num = 0; item_num < listObjc; item_num++) {
	layer->items[item_num] = strdup (Tcl_GetString (listObjv[item_num]));
    }

    return msTclPlugLayerInitItemInfo(layer);
#else
    msSetError( MS_MISCERR,
                "TclPlug support is not available.",
                "msTclPlugLayerGetItems()");
    return MS_FAILURE;
#endif
}

/*
** msTclPlugLayerGetExtent()
**
** Registered vtable->LayerGetExtent function.
**
** TODO: Update to use proper TclPlug functions to pull
** extent quickly and accurately when available.
*/
int msTclPlugLayerGetExtent(layerObj *layer, rectObj *extent) {
    if (layer->debug) {
        msDebug("msTCLPLUGLayerGetExtent called.\n");
    }

    extent->minx = extent->miny = -1.0 * FLT_MAX ;
    extent->maxx = extent->maxy = FLT_MAX;

    return MS_SUCCESS;

}

int msTclPlugLayerSetTimeFilter(layerObj *lp, const char *timestring, const char *timefield)
{
    char *tmpstimestring = NULL;
    char *timeresolution = NULL;
    int timesresol = -1;
    char **atimes, **tokens = NULL;
    int numtimes=0,i=0,ntmp=0,nlength=0;
    char buffer[512];

    buffer[0] = '\0';

    if (!lp || !timestring || !timefield)
      return MS_FALSE;

    if (strstr(timestring, ",") == NULL && 
        strstr(timestring, "/") == NULL) /* discrete time */
      tmpstimestring = strdup(timestring);
    else
    {
        atimes = msStringSplit (timestring, ',', &numtimes);
        if (atimes == NULL || numtimes < 1)
          return MS_FALSE;

        if (numtimes >= 1)
        {
            tokens = msStringSplit(atimes[0],  '/', &ntmp);
            if (ntmp == 2) /* ranges */
            {
                tmpstimestring = strdup(tokens[0]);
                msFreeCharArray(tokens, ntmp);
            }
            else if (ntmp == 1) /* multiple times */
            {
                tmpstimestring = strdup(atimes[0]);
            }
        }
        msFreeCharArray(atimes, numtimes);
    }
    if (!tmpstimestring)
      return MS_FALSE;
        
    timesresol = msTimeGetResolution((const char*)tmpstimestring);
    if (timesresol < 0)
      return MS_FALSE;

    free(tmpstimestring);

    switch (timesresol)
    {
        case (TIME_RESOLUTION_SECOND):
          timeresolution = strdup("second");
          break;

        case (TIME_RESOLUTION_MINUTE):
          timeresolution = strdup("minute");
          break;

        case (TIME_RESOLUTION_HOUR):
          timeresolution = strdup("hour");
          break;

        case (TIME_RESOLUTION_DAY):
          timeresolution = strdup("day");
          break;

        case (TIME_RESOLUTION_MONTH):
          timeresolution = strdup("month");
          break;

        case (TIME_RESOLUTION_YEAR):
          timeresolution = strdup("year");
          break;

        default:
          break;
    }

    if (!timeresolution)
      return MS_FALSE;

    /* where date_trunc('month', _cwctstamp) = '2004-08-01' */
    if (strstr(timestring, ",") == NULL && 
        strstr(timestring, "/") == NULL) /* discrete time */
    {
        if(lp->filteritem) free(lp->filteritem);
        lp->filteritem = strdup(timefield);
        if (&lp->filter)
        {
            /* if the filter is set and it's a string type, concatenate it with
               the time. If not just free it */
            if (lp->filter.type == MS_EXPRESSION)
            {
                strcat(buffer, "(");
                strcat(buffer, lp->filter.string);
                strcat(buffer, ") and ");
            }
            else
              freeExpression(&lp->filter);
        }
        

        strcat(buffer, "(");

        strcat(buffer, "date_trunc('");
        strcat(buffer, timeresolution);
        strcat(buffer, "', ");        
        strcat(buffer, timefield);
        strcat(buffer, ")");        
        
         
        strcat(buffer, " = ");
        strcat(buffer,  "'");
        strcat(buffer, timestring);
        /* make sure that the timestring is complete and acceptable */
        /* to the date_trunc function : */
        /* - if the resolution is year (2004) or month (2004-01),  */
        /* a complete string for time would be 2004-01-01 */
        /* - if the resolluion is hour or minute (2004-01-01 15), a  */
        /* complete time is 2004-01-01 15:00:00 */
        if (strcasecmp(timeresolution, "year")==0)
        {
            nlength = strlen(timestring);
            if (timestring[nlength-1] != '-')
              strcat(buffer,"-01-01");
            else
              strcat(buffer,"01-01");
        }            
        else if (strcasecmp(timeresolution, "month")==0)
        {
            nlength = strlen(timestring);
            if (timestring[nlength-1] != '-')
              strcat(buffer,"-01");
            else
              strcat(buffer,"01");
        }            
        else if (strcasecmp(timeresolution, "hour")==0)
        {
            nlength = strlen(timestring);
            if (timestring[nlength-1] != ':')
              strcat(buffer,":00:00");
            else
              strcat(buffer,"00:00");
        }            
        else if (strcasecmp(timeresolution, "minute")==0)
        {
            nlength = strlen(timestring);
            if (timestring[nlength-1] != ':')
              strcat(buffer,":00");
            else
              strcat(buffer,"00");
        }            
        

        strcat(buffer,  "'");

        strcat(buffer, ")");
        
        /* loadExpressionString(&lp->filter, (char *)timestring); */
        loadExpressionString(&lp->filter, buffer);

        free(timeresolution);
        return MS_TRUE;
    }
    
    atimes = msStringSplit (timestring, ',', &numtimes);
    if (atimes == NULL || numtimes < 1)
      return MS_FALSE;

    if (numtimes >= 1)
    {
        /* check to see if we have ranges by parsing the first entry */
        tokens = msStringSplit(atimes[0],  '/', &ntmp);
        if (ntmp == 2) /* ranges */
        {
            msFreeCharArray(tokens, ntmp);
            for (i=0; i<numtimes; i++)
            {
                tokens = msStringSplit(atimes[i],  '/', &ntmp);
                if (ntmp == 2)
                {
                    if (strlen(buffer) > 0)
                      strcat(buffer, " OR ");
                    else
                      strcat(buffer, "(");

                    strcat(buffer, "(");
                    
                    strcat(buffer, "date_trunc('");
                    strcat(buffer, timeresolution);
                    strcat(buffer, "', ");        
                    strcat(buffer, timefield);
                    strcat(buffer, ")");        
 
                    strcat(buffer, " >= ");
                    
                    strcat(buffer,  "'");

                    strcat(buffer, tokens[0]);
                    /* - if the resolution is year (2004) or month (2004-01),  */
                    /* a complete string for time would be 2004-01-01 */
                    /* - if the resolluion is hour or minute (2004-01-01 15), a  */
                    /* complete time is 2004-01-01 15:00:00 */
                    if (strcasecmp(timeresolution, "year")==0)
                    {
                        nlength = strlen(tokens[0]);
                        if (tokens[0][nlength-1] != '-')
                          strcat(buffer,"-01-01");
                        else
                          strcat(buffer,"01-01");
                    }            
                    else if (strcasecmp(timeresolution, "month")==0)
                    {
                        nlength = strlen(tokens[0]);
                        if (tokens[0][nlength-1] != '-')
                          strcat(buffer,"-01");
                        else
                          strcat(buffer,"01");
                    }            
                    else if (strcasecmp(timeresolution, "hour")==0)
                    {
                        nlength = strlen(tokens[0]);
                        if (tokens[0][nlength-1] != ':')
                          strcat(buffer,":00:00");
                        else
                          strcat(buffer,"00:00");
                    }            
                    else if (strcasecmp(timeresolution, "minute")==0)
                    {
                        nlength = strlen(tokens[0]);
                        if (tokens[0][nlength-1] != ':')
                          strcat(buffer,":00");
                        else
                          strcat(buffer,"00");
                    }            

                    strcat(buffer,  "'");
                    strcat(buffer, " AND ");

                    
                    strcat(buffer, "date_trunc('");
                    strcat(buffer, timeresolution);
                    strcat(buffer, "', ");        
                    strcat(buffer, timefield);
                    strcat(buffer, ")");  

                    strcat(buffer, " <= ");
                    
                    strcat(buffer,  "'");
                    strcat(buffer, tokens[1]);

                    /* - if the resolution is year (2004) or month (2004-01),  */
                    /* a complete string for time would be 2004-01-01 */
                    /* - if the resolluion is hour or minute (2004-01-01 15), a  */
                    /* complete time is 2004-01-01 15:00:00 */
                    if (strcasecmp(timeresolution, "year")==0)
                    {
                        nlength = strlen(tokens[1]);
                        if (tokens[1][nlength-1] != '-')
                          strcat(buffer,"-01-01");
                        else
                          strcat(buffer,"01-01");
                    }            
                    else if (strcasecmp(timeresolution, "month")==0)
                    {
                        nlength = strlen(tokens[1]);
                        if (tokens[1][nlength-1] != '-')
                          strcat(buffer,"-01");
                        else
                          strcat(buffer,"01");
                    }            
                    else if (strcasecmp(timeresolution, "hour")==0)
                    {
                        nlength = strlen(tokens[1]);
                        if (tokens[1][nlength-1] != ':')
                          strcat(buffer,":00:00");
                        else
                          strcat(buffer,"00:00");
                    }            
                    else if (strcasecmp(timeresolution, "minute")==0)
                    {
                        nlength = strlen(tokens[1]);
                        if (tokens[1][nlength-1] != ':')
                          strcat(buffer,":00");
                        else
                          strcat(buffer,"00");
                    }            

                    strcat(buffer,  "'");
                    strcat(buffer, ")");
                }
                 
                msFreeCharArray(tokens, ntmp);
            }
            if (strlen(buffer) > 0)
              strcat(buffer, ")");
        }
        else if (ntmp == 1) /* multiple times */
        {
            msFreeCharArray(tokens, ntmp);
            strcat(buffer, "(");
            for (i=0; i<numtimes; i++)
            {
                if (i > 0)
                  strcat(buffer, " OR ");

                strcat(buffer, "(");
                  
                strcat(buffer, "date_trunc('");
                strcat(buffer, timeresolution);
                strcat(buffer, "', ");        
                strcat(buffer, timefield);
                strcat(buffer, ")");   

                strcat(buffer, " = ");
                  
                strcat(buffer,  "'");

                strcat(buffer, atimes[i]);
                
                /* make sure that the timestring is complete and acceptable */
                /* to the date_trunc function : */
                /* - if the resolution is year (2004) or month (2004-01),  */
                /* a complete string for time would be 2004-01-01 */
                /* - if the resolluion is hour or minute (2004-01-01 15), a  */
                /* complete time is 2004-01-01 15:00:00 */
                if (strcasecmp(timeresolution, "year")==0)
                {
                    nlength = strlen(atimes[i]);
                    if (atimes[i][nlength-1] != '-')
                      strcat(buffer,"-01-01");
                    else
                      strcat(buffer,"01-01");
                }            
                else if (strcasecmp(timeresolution, "month")==0)
                {
                    nlength = strlen(atimes[i]);
                    if (atimes[i][nlength-1] != '-')
                      strcat(buffer,"-01");
                    else
                      strcat(buffer,"01");
                }            
                else if (strcasecmp(timeresolution, "hour")==0)
                {
                    nlength = strlen(atimes[i]);
                    if (atimes[i][nlength-1] != ':')
                      strcat(buffer,":00:00");
                    else
                      strcat(buffer,"00:00");
                }            
                else if (strcasecmp(timeresolution, "minute")==0)
                {
                    nlength = strlen(atimes[i]);
                    if (atimes[i][nlength-1] != ':')
                      strcat(buffer,":00");
                    else
                      strcat(buffer,"00");
                }            

                strcat(buffer,  "'");
                strcat(buffer, ")");
            } 
            strcat(buffer, ")");
        }
        else
        {
            msFreeCharArray(atimes, numtimes);
            return MS_FALSE;
        }

        msFreeCharArray(atimes, numtimes);

        /* load the string to the filter */
        if (strlen(buffer) > 0)
        {
            if(lp->filteritem) 
              free(lp->filteritem);
            lp->filteritem = strdup(timefield);     
            if (&lp->filter)
            {
                if (lp->filter.type == MS_EXPRESSION)
                {
                    strcat(buffer, "(");
                    strcat(buffer, lp->filter.string);
                    strcat(buffer, ") and ");
                }
                else
                  freeExpression(&lp->filter);
            }
            loadExpressionString(&lp->filter, buffer);
        }

        free(timeresolution);
        return MS_TRUE;
                 
    }
    
    return MS_FALSE;
}

int tclPlugPluginInitializeVirtualTable(layerVTableObj* vtable, layerObj *layer) {
    assert(layer != NULL);
    assert(vtable != NULL);

    vtable->LayerInitItemInfo = msTclPlugLayerInitItemInfo;
    vtable->LayerFreeItemInfo = msTclPlugLayerFreeItemInfo;
    vtable->LayerOpen = msTclPlugLayerOpen;
    vtable->LayerIsOpen = msTclPlugLayerIsOpen;
    vtable->LayerWhichShapes = msTclPlugLayerWhichShapes;
    vtable->LayerNextShape = msTclPlugLayerNextShape;
    vtable->LayerGetShape = msTclPlugLayerGetShape;
    vtable->LayerClose = msTclPlugLayerClose;
    vtable->LayerGetItems = msTclPlugLayerGetItems;
    vtable->LayerGetExtent = msTclPlugLayerGetExtent;

    vtable->LayerApplyFilterToLayer = msLayerApplyCondSQLFilterToLayer;

    /* vtable->LayerGetAutoStyle, not supported for this layer; */

    vtable->LayerSetTimeFilter = msTclPlugLayerSetTimeFilter; 
    /* vtable->LayerCreateItems, use default */
    /* vtable->LayerGetNumFeatures, use default */

    return MS_SUCCESS;
}


int tclPlugLayerInitializeVirtualTable(layerObj *layer) {
    assert(layer != NULL);
    assert(layer->vtable != NULL);

    return tclPlugPluginInitializeVirtualTable (layer->vtable, layer);
}

int PluginInitializeVirtualTable(layerVTableObj* vtable, layerObj *layer) {
    return tclPlugPluginInitializeVirtualTable(vtable, layer);
}
