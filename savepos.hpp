#ifndef _RAR_SAVEPOS_
#define _RAR_SAVEPOS_

#include "rartypes.hpp"

class File;

class SaveFilePos
{
  private:
    File *SaveFile;
    int64 SavePos;
    uint CloseCount;
  public:
    SaveFilePos(File &SaveFile);
    ~SaveFilePos();
};

#endif
