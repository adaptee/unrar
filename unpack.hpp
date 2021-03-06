#ifndef _RAR_UNPACK_
#define _RAR_UNPACK_

#include "rartypes.hpp"
#include "array.hpp"
#include "rarvm.hpp"
#include "model.hpp"
#include "compress.hpp"



enum BLOCK_TYPES {BLOCK_LZ, BLOCK_PPM};

// Maximum allowed number of compressed bits processed in quick mode.
#define MAX_QUICK_DECODE_BITS 10

// Decode compressed bit fields to alphabet numbers.
struct DecodeTable
{
    // Real size of DecodeNum table.
    uint MaxNum;

    // Left aligned start and upper limit codes defining code space
    // ranges for bit lengths. DecodeLen[BitLength-1] defines the start of
    // range for bit length and DecodeLen[BitLength] defines next code
    // after the end of range or in other words the upper limit code
    // for specified bit length.
    uint DecodeLen[16];

    // Every item of this array contains the sum of all preceding items.
    // So it contains the start position in code list for every bit length.
    uint DecodePos[16];

    // Number of compressed bits processed in quick mode.
    // Must not exceed MAX_QUICK_DECODE_BITS.
    uint QuickBits;

    // Translates compressed bits (up to QuickBits length)
    // to bit length in quick mode.
    byte QuickLen[1<<MAX_QUICK_DECODE_BITS];

    // Translates compressed bits (up to QuickBits length)
    // to position in alphabet in quick mode.
    uint QuickNum[1<<MAX_QUICK_DECODE_BITS];

    // Translate the position in code list to position in alphabet.
    // We do not allocate it dynamically to avoid performance overhead
    // introduced by pointer, so we use the largest possible table size
    // as array dimension. Real size of this array is defined in MaxNum.
    // We use this array if compressed bit field is too lengthy
    // for QuickLen based translation.
    uint DecodeNum[LARGEST_TABLE_SIZE];
};

struct UnpackFilter
{
    unsigned int BlockStart;
    unsigned int BlockLength;
    unsigned int ExecCount;
    bool NextWindow;

    // position of parent filter in Filters array used as prototype for filter
    // in m_progStack array. Not defined for filters in m_filters array.
    unsigned int ParentFilter;

    VM_PreparedProgram Prg;
};


class Unpack:private BitInput
{

public:
    Unpack(ComprDataIO *DataIO);
    ~Unpack();
    void Init(byte *Window=NULL);
    void DoUnpack(int Method, bool Solid);
    void SetDestSize(int64 DestSize) {m_destUnpSize=DestSize;m_hasExtractFile=false;}

    unsigned int GetChar()
    {
        if (InAddr>BitInput::MAX_SIZE-30)
            UnpReadBuf();
        return(InBuf[InAddr++]);
    }


private:
    friend class Pack;

    void Unpack29(bool Solid);
    bool UnpReadBuf();
    void UnpWriteBuf();
    void ExecuteCode(VM_PreparedProgram *Prg);
    void UnpWriteArea(unsigned int StartPtr, unsigned int EndPtr);
    void UnpWriteData(byte *Data, size_t Size);
    bool ReadTables();
    void MakeDecodeTables(byte *LengthTable, DecodeTable *Dec, uint Size);
    uint DecodeNumber(DecodeTable *Dec);
    int SafePPMDecodeChar();
    void CopyString();
    void InsertOldDist(unsigned int Distance);
    void InsertLastMatch(unsigned int Length, unsigned int Distance);
    void UnpInitData(int Solid);
    void CopyString(uint Length, uint Distance);
    bool ReadEndOfBlock();
    bool ReadVMCode();
    bool ReadVMCodePPM();
    bool AddVMCode(unsigned int FirstByte, byte *Code, int CodeSize);
    void ResetFilters();

    ComprDataIO * m_io;

    ModelPPM m_ppm;
    int m_EscCharOfPPM;

    RarVM m_vm;

    /* Filters code, one entry per filter */
    Array<UnpackFilter*> m_filters;

    /* Filters stack, several entrances of same filter are possible */
    Array<UnpackFilter*> m_progStack;

    /* lengths of preceding blocks, one length per filter. Used to reduce
       size required to write block length if lengths are repeating */
    Array<int> m_oldFilterLengths;

    int m_lastfilter;


    DecodeTable m_LD;  // Decode literals.
    DecodeTable m_DD;  // Decode distances.
    DecodeTable m_LDD; // Decode lower bits of distances.
    DecodeTable m_RD;  // Decode repeating distances.
    DecodeTable m_BD;  // Decod bit lengths in Huffman table.

    unsigned int m_oldDistances[4];
    unsigned int m_oldDistancePtr;

    unsigned int m_lastDistance;
    unsigned int m_lastLength;

    unsigned int m_unpackPtr;
    unsigned int m_writePtr;

    // Top border of read packed data.
    int m_readtop;

    // Border to call UnpReadBuf. We use it instead of (m_readtop-C)
    // for optimization reasons. Ensures that we have C bytes in buffer
    // unless we are at the end of file.
    int m_readborder;

    unsigned char m_oldUnpackTable[HUFF_TABLE_SIZE];

    int m_blocktype;

    byte * m_window;
    bool m_useExternalWindow;


    int64 m_destUnpSize;
    int64 m_writtenSize;

    bool m_hasReadTables;
    bool m_hasExtractFile;

    int m_prevLowDistance;
    int m_lowDistanceRepeatCount;

};

#endif
