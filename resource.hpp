#ifndef _RAR_RESOURCE_
#define _RAR_RESOURCE_

#include "raros.hpp"
#include "os.hpp"
#include "rartypes.hpp"

#ifdef RARDLL
#define St(x)  ( "")
#define StW(x) (L"")
#else
const char  *St  (MSGID StringId);
const wchar *StW (MSGID StringId);
#endif


#endif
