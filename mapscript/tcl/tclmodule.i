

%typemap(out) gdBuffer {
    Tcl_SetByteArrayObj ($result, $1.data, $1.size);
    gdFree($1.data);
}

%module Mapscript

%{
/* static global copy of Tcl interp */
static Tcl_Interp *SWIG_TCL_INTERP;
static char interpString[32];
%}

%init %{
#define USE_TCL_STUBS

#ifdef USE_TCL_STUBS
  if (Tcl_InitStubs(interp, "8.1", 0) == NULL) {
    return TCL_ERROR;
  }

  // grotesquely store interpreter pointer as a string to facilitate
  // communication with tcl layer plugins
  sprintf(interpString, "0x%llx", (long long)interp);
  Tcl_SetVar (interp, "::mapscript::interpreter", &interpString[0], TCL_GLOBAL_ONLY);

  /* save Tcl interp pointer to be used in getImageToVar() */
  SWIG_TCL_INTERP = interp;
#endif /* USE_TCL_STUBS */
%}
