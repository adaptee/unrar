#ifndef _RAR_RECVOL_
#define _RAR_RECVOL_

#include "array.hpp"

class File;
class RAROptions;

class RecVolumes
{
  private:
    File *SrcFile[256];
    Array<byte> Buf;
  public:
    RecVolumes();
    ~RecVolumes();
    void Make(RAROptions *Cmd, char *ArcName, wchar *ArcNameW);
    bool Restore(RAROptions *Cmd, const char *Name, const wchar *NameW, bool Silent);
};

#endif
