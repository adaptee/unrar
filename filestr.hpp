#ifndef _RAR_FILESTR_
#define _RAR_FILESTR_

#include "rartypes.hpp"
#include "options.hpp"

class StringList;


bool ReadTextFile(
  const char *Name,
  const wchar *NameW,
  StringList *List,
  bool Config,
  bool AbortOnError=false,
  RAR_CHARSET SrcCharset=RCH_DEFAULT,
  bool Unquote=false,
  bool SkipComments=false,
  bool ExpandEnvStr=false
);

#endif
