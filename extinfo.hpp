#ifndef _RAR_EXTINFO_
#define _RAR_EXTINFO_

#include "rartypes.hpp"

class CommandData;
class Archive;

void SetExtraInfo(CommandData *Cmd, Archive &Arc, char *Name, wchar *NameW);
void SetExtraInfoNew(CommandData *Cmd, Archive &Arc, char *Name, wchar *NameW);

#endif
