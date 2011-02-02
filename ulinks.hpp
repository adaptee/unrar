#ifndef _RAR_ULINKS_
#define _RAR_ULINKS_

#include "rdwrfn.hpp"
#include "archive.hpp"

void SaveLinkData(ComprDataIO &DataIO, Archive &TempArc, FileHeader &hd,
                  const char *Name);
bool ExtractLink(ComprDataIO &DataIO, Archive &Arc, const char *LinkName,
                 uint &LinkCRC, bool Create);

#endif
