#include "unpack.hpp"

#include "rdwrfn.hpp"

#include "coder.hpp"
#include "suballoc.hpp"
#include "model.hpp"

Unpack::Unpack(ComprDataIO *DataIO)
{
    m_io = DataIO;

    m_window = NULL;
    m_useExternalWindow = false;

}


Unpack::~Unpack()
{
    if ( !m_useExternalWindow)
        delete[] m_window;

    ResetFilters();
}


void Unpack::Init(byte * window)
{
    if (m_window)
    {
        m_window            = window;
        m_useExternalWindow = true;
    }
    else
    {
        Unpack::m_window = new byte[MAXWINSIZE];
    }

    UnpInitData(false);
}


void Unpack::DoUnpack(int Method, bool Solid)
{
    switch(Method)
    {
#ifndef SFX_MODULE
        case 15: // rar 1.5 compression
            //Unpack15(Solid);
            break;
        case 20: // rar 2.x compression
        case 26: // files larger than 2GB
            //Unpack20(Solid);
            break;
#endif
        case 29: // rar 3.x compression
        case 36: // alternative hash
            Unpack29(Solid);
            break;
    }
}


void Unpack::InsertOldDist(unsigned int distance)
{
    m_oldDistances[3] = m_oldDistances[2];
    m_oldDistances[2] = m_oldDistances[1];
    m_oldDistances[1] = m_oldDistances[0];
    m_oldDistances[0] = distance;
}


void Unpack::InsertLastMatch(unsigned int length, unsigned int distance)
{
    m_lastDistance   = distance;
    m_lastLength = length;
}


void Unpack::CopyString(uint length, uint distance)
{
    uint srcPtr = m_unpackPtr - distance;

    if ( (srcPtr < (MAXWINSIZE - MAX_LZ_MATCH)) &&
         (m_unpackPtr < (MAXWINSIZE - MAX_LZ_MATCH))
       )
    {
        // If we are not close to end of window, we do not need to waste time
        // to "& MAXWINMASK" pointer protection.

        byte *src  = m_window + srcPtr;
        byte *dest = m_window + m_unpackPtr;
        m_unpackPtr += length;

        while (length >= 8)
        {
            // Unroll the loop for 8 byte and longer strings.
            dest[0] = src[0];
            dest[1] = src[1];
            dest[2] = src[2];
            dest[3] = src[3];
            dest[4] = src[4];
            dest[5] = src[5];
            dest[6] = src[6];
            dest[7] = src[7];

            src  += 8;
            dest += 8;
            length -= 8;
        }

        // Unroll the loop for 0 - 7 bytes left. Note that we use nested "if"s.
        if (length>0) { dest[0]=src[0];
            if (length>1) { dest[1]=src[1];
                if (length>2) { dest[2]=src[2];
                    if (length>3) { dest[3]=src[3];
                        if (length>4) { dest[4]=src[4];
                            if (length>5) { dest[5]=src[5];
                                if (length>6) { dest[6]=src[6]; } } } } } } } // Close all nested "if"s.
    }
    else
        while (length--) // Slow copying with all possible precautions.
        {
            m_window[m_unpackPtr] = m_window[srcPtr++ & MAXWINMASK];
            m_unpackPtr = (m_unpackPtr+1) & MAXWINMASK;
        }
}


uint Unpack::DecodeNumber(DecodeTable *Dec)
{
    // Left aligned 15 bit length raw bit field.
    uint BitField = ( getbits() & 0xfffe );

    if (BitField < Dec->DecodeLen[Dec->QuickBits])
    {
        uint Code = BitField>>(16 - Dec->QuickBits);
        addbits(Dec->QuickLen[Code]);
        return Dec->QuickNum[Code];
    }

    // Detect the real bit length for current code.
    uint Bits = 15;
    for (uint i = Dec->QuickBits+1; i < 15;i++)
        if (BitField < Dec->DecodeLen[i])
        {
            Bits = i;
            break;
        }

    addbits(Bits);

    // Calculate the distance from the start code for current bit length.
    uint distance = BitField - Dec->DecodeLen[Bits-1];

    // Start codes are left aligned, but we need the normal right aligned
    // number. So we shift the distance to the right.
    distance >>= (16-Bits);

    // Now we can calculate the position in the code list. It is the sum
    // of first position for current bit length and right aligned distance
    // between our bit field and start code for current bit length.
    uint pos = Dec->DecodePos[Bits] + distance;

    // Out of bounds safety check required for damaged archives.
    if (pos >= Dec->MaxNum)
        pos = 0;

    // Convert the position in the code list to position in alphabet
    // and return it.
    return Dec->DecodeNum[pos];
}


// We use it instead of direct m_ppm.DecodeChar call to be sure that
// we reset PPM structures in case of corrupt data. It is important,
// because these structures can be invalid after m_ppm.DecodeChar returned -1.
int Unpack::SafePPMDecodeChar()
{
    int ch = m_ppm.DecodeChar();
    if (ch == -1 )              // Corrupt PPM data found.
    {
        m_ppm.CleanUp();         // Reset possibly corrupt PPM data structures.
        m_blocktype = BLOCK_LZ; // Set faster and more fail proof LZ mode.
    }
    return(ch);
}


void Unpack::Unpack29(bool Solid)
{
    static unsigned char LDecode[]={0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 14, 16, 20, 24, 28, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224};
    static unsigned char LBits[]=  {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5};
    static int DDecode[DC];
    static byte DBits[DC];
    static int DBitLengthCounts[]= {4, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 14, 0, 12};
    static unsigned char SDDecode[]={0, 4, 8, 16, 32, 64, 128, 192};
    static unsigned char SDBits[]=  {2, 2, 3, 4, 5, 6, 6, 6};

    unsigned int Bits;

    if (DDecode[1] == 0)
    {
        int Dist=0, BitLength=0, Slot=0;
        for (int i=0;i<sizeof(DBitLengthCounts)/sizeof(DBitLengthCounts[0]);i++, BitLength++)
            for (int J=0;J<DBitLengthCounts[i];J++, Slot++, Dist+=(1<<BitLength))
            {
                DDecode[Slot] = Dist;
                DBits[Slot]   = BitLength;
            }
    }

    m_hasExtractFile = true;

    UnpInitData(Solid);

    if (!UnpReadBuf())
        return;

    if ((!Solid || !m_hasReadTables))
    {
        bool ok = ReadTables();
        if( ! ok )
            return;
    }

    while (true)
    {
        m_unpackPtr &= MAXWINMASK;

        if (InAddr > m_readborder)
        {
            if (!UnpReadBuf())
                break;
        }

        if ((   ((m_writePtr-m_unpackPtr) & MAXWINMASK) < 260) &&
                (m_writePtr != m_unpackPtr) )
        {
            UnpWriteBuf();
            if (m_writtenSize > m_destUnpSize)
                return;
            else
                m_hasExtractFile = false;
                return;
        }

        if (m_blocktype == BLOCK_PPM)
        {
            // Here speed is critical, so we do not use SafePPMDecodeChar,
            // because sometimes even the function can introduce
            // some additional penalty.
            int Ch = m_ppm.DecodeChar();
            if (Ch == -1)              // Corrupt PPM data found.
            {
                m_ppm.CleanUp();         // Reset possibly corrupt PPM data structures.
                m_blocktype = BLOCK_LZ; // Set faster and more fail proof LZ mode.
                break;
            }

            if (Ch == m_EscCharOfPPM)
            {
                int NextCh = SafePPMDecodeChar();
                if (NextCh == 0)  // End of PPM encoding.
                {
                    if (!ReadTables())
                        break;
                    continue;
                }

                if (NextCh == -1) // Corrupt PPM data found.
                    break;

                if (NextCh == 2)  // End of file in PPM mode..
                    break;

                if (NextCh == 3)  // Read VM code.
                {
                    if (!ReadVMCodePPM())
                        break;
                    continue;
                }

                if (NextCh == 4) // LZ inside of m_ppm.
                {
                    unsigned int Distance=0, Length;
                    bool Failed = false;

                    for (int i=0;i<4 && !Failed;i++)
                    {
                        int Ch = SafePPMDecodeChar();
                        if (Ch == -1)
                        {
                            Failed = true;
                        }
                        else
                        {
                            if (i == 3)
                                Length = (byte)Ch;
                            else
                                Distance = (Distance << 8) + (byte)Ch;
                        }
                    }

                    if (Failed)
                        break;

                    CopyString(Length+32, Distance+2);
                    continue;
                }
                if (NextCh == 5) // One byte distance match (RLE) inside of m_ppm.
                {
                    int Length = SafePPMDecodeChar();
                    if (Length == -1)
                        break;

                    CopyString(Length + 4, 1);
                    continue;
                }
                // If we are here, NextCh must be 1, what means that current byte
                // is equal to our 'escape' byte, so we just store it to m_window.

            } // end of `if (Ch == m_EscCharOfPPM)`

            m_window[m_unpackPtr++] = Ch;
            continue;
        }

        int Number = DecodeNumber(&m_LD);

        if (Number < 256)
        {
            m_window[m_unpackPtr++] = (byte)Number;
            continue;
        }

        if (Number >= 271)
        {
            Number -= 271;
            int Length = LDecode[Number] + 3;

            if ((Bits=LBits[Number]) > 0)
            {
                Length += ( getbits() >> (16-Bits) );
                addbits(Bits);
            }

            int DistNumber        = DecodeNumber(&m_DD);
            unsigned int Distance = DDecode[DistNumber] + 1;
            if ((Bits=DBits[DistNumber]) > 0)
            {
                if (DistNumber > 9)
                {
                    if (Bits > 4)
                    {
                        Distance += ((getbits()>>(20-Bits))<<4);
                        addbits(Bits-4);
                    }

                    if (m_lowDistanceRepeatCount > 0)
                    {
                        m_lowDistanceRepeatCount--;
                        Distance += m_prevLowDistance;
                    }
                    else
                    {
                        int LowDist = DecodeNumber(&m_LDD);
                        if (LowDist == 16)
                        {
                            m_lowDistanceRepeatCount = LOW_DIST_REP_COUNT-1;
                            Distance += m_prevLowDistance;
                        }
                        else
                        {
                            Distance += LowDist;
                            m_prevLowDistance = LowDist;
                        }
                    }
                } // end of if (DistNumber > 9)
                else
                {
                    Distance += getbits() >> (16-Bits);
                    addbits(Bits);
                }
            }

            if (Distance >= 0x2000)
            {
                Length++;
                if (Distance >= 0x40000L)
                    Length++;
            }

            InsertOldDist(Distance);
            InsertLastMatch(Length, Distance);
            CopyString(Length, Distance);
            continue;
        }

        if (Number == 256)
        {
            if (!ReadEndOfBlock())
                break;
            continue;
        }

        if (Number == 257)
        {
            if (!ReadVMCode())
                break;
            continue;
        }

        if (Number == 258)
        {
            if (m_lastLength != 0)
                CopyString(m_lastLength, m_lastDistance);
            continue;
        }

        if (Number < 263)
        {
            int DistNum           = Number - 259;
            unsigned int Distance = m_oldDistances[DistNum];

            for (int i=DistNum;i>0;i--)
                m_oldDistances[i] = m_oldDistances[i-1];

            m_oldDistances[0] = Distance;

            int LengthNumber = DecodeNumber(&m_RD);
            int Length       = LDecode[LengthNumber]+2;
            if ((Bits=LBits[LengthNumber])>0)
            {
                Length += getbits() >> (16-Bits);
                addbits(Bits);
            }
            InsertLastMatch(Length, Distance);
            CopyString(Length, Distance);
            continue;
        }

        if (Number<272)
        {
            unsigned int Distance = SDDecode[Number-=263]+1;
            if ((Bits=SDBits[Number])>0)
            {
                Distance += (getbits() >> (16-Bits));
                addbits(Bits);
            }
            InsertOldDist(Distance);
            InsertLastMatch(2, Distance);
            CopyString(2, Distance);
            continue;
        }
    }
    UnpWriteBuf();
}


bool Unpack::ReadEndOfBlock()
{
    unsigned int BitField = getbits();
    bool NewTable, NewFile=false;
    if (BitField & 0x8000)
    {
        NewTable = true;
        addbits(1);
    }
    else
    {
        NewFile = true;
        NewTable = ((BitField & 0x4000) != 0);
        addbits(2);
    }

    m_hasReadTables = !NewTable;
    return !(NewFile || NewTable && !ReadTables());
}


bool Unpack::ReadVMCode()
{
    unsigned int FirstByte = (getbits() >> 8);
    addbits(8);

    int Length = (FirstByte & 7) + 1;
    if (Length == 7)
    {
        Length = (getbits() >> 8) + 7;
        addbits(8);
    }
    else
        if (Length == 8)
        {
            Length = getbits();
            addbits(16);
        }
    Array<byte> VMCode(Length);
    for (int i=0;i<Length;i++)
    {
        // Try to read the new buffer if only one byte is left.
        // But if we read all bytes except the last, one byte is enough.
        if ( (InAddr >= m_readtop-1) &&
             (!UnpReadBuf()) &&
             (i < (Length-1))  )
            return false;

        VMCode[i] = getbits() >> 8;
        addbits(8);
    }
    return(AddVMCode(FirstByte, &VMCode[0], Length));
}


bool Unpack::ReadVMCodePPM()
{
    unsigned int firstByte = SafePPMDecodeChar();
    if ((int)firstByte == -1)
        return false;

    int length = (firstByte & 7) + 1;
    if (length == 7)
    {
        int B1 = SafePPMDecodeChar();
        if (B1 == -1)
            return false;

        length = B1 + 7;
    }
    else
    {
        if (length == 8)
        {
            int B1 = SafePPMDecodeChar();
            if (B1 == -1)
                return false;
            int B2 = SafePPMDecodeChar();
            if (B2 == -1)
                return false;
            length = B1 * 256 + B2;
        }
    }

    Array<byte> VMCode(length);
    for (int i=0;i<length;i++)
    {
        int ch = SafePPMDecodeChar();
        if (ch == -1)
            return false;
        VMCode[i] = ch;
    }

    return AddVMCode(firstByte, &VMCode[0], length);
}


bool Unpack::AddVMCode(unsigned int FirstByte, byte *Code, int CodeSize)
{
    BitInput Inp;
    Inp.InitBitInput();
    memcpy(Inp.InBuf, Code, Min(BitInput::MAX_SIZE, CodeSize));
    m_vm.Init();

    uint FiltPos;
    if (FirstByte & 0x80)
    {
        FiltPos = RarVM::ReadData(Inp);
        if (FiltPos == 0)
            ResetFilters();
        else
            FiltPos--;
    }
    else
    {
        FiltPos = m_lastfilter; // use the same filter as last time
    }

    if (FiltPos > m_filters.Size() || FiltPos > m_oldFilterLengths.Size())
        return false;

    m_lastfilter = FiltPos;
    bool NewFilter = (FiltPos == m_filters.Size());

    UnpackFilter * StackFilter = new UnpackFilter; // new filter for m_progStack
    UnpackFilter * Filter;

    if (NewFilter) // new filter code, never used before since VM reset
    {
        // Too many different filters, corrupt archive.
        if (FiltPos > 1024)
            return false;

        m_filters.Add(1);
        m_filters[m_filters.Size()-1]=Filter=new UnpackFilter;
        StackFilter->ParentFilter = (uint)(m_filters.Size()-1);
        m_oldFilterLengths.Add(1);
        Filter->ExecCount = 0;
    }
    else  // filter was used in the past
    {
        Filter = m_filters[FiltPos];
        StackFilter->ParentFilter = FiltPos;
        Filter->ExecCount++;
    }

    int EmptyCount = 0;
    for (uint i=0;i<m_progStack.Size();i++)
    {
        m_progStack[i-EmptyCount] = m_progStack[i];
        if (m_progStack[i] == NULL)
            EmptyCount++;
        if (EmptyCount > 0)
            m_progStack[i] = NULL;
    }
    if (EmptyCount == 0)
    {
        m_progStack.Add(1);
        EmptyCount = 1;
    }
    int StackPos = (int)(m_progStack.Size() - EmptyCount);
    m_progStack[StackPos] = StackFilter;
    StackFilter->ExecCount = Filter->ExecCount;

    uint BlockStart = RarVM::ReadData(Inp);
    if (FirstByte & 0x40)
        BlockStart += 258;
    StackFilter->BlockStart = (BlockStart + m_unpackPtr) & MAXWINMASK;

    if (FirstByte & 0x20)
        StackFilter->BlockLength = RarVM::ReadData(Inp);
    else
        StackFilter->BlockLength = FiltPos < m_oldFilterLengths.Size() ? m_oldFilterLengths[FiltPos] : 0;
    StackFilter->NextWindow = (m_writePtr != m_unpackPtr) && ((m_writePtr-m_unpackPtr) & MAXWINMASK) <= BlockStart;

    //  DebugLog("\nNextWindow: m_unpackPtr=%08x m_writePtr=%08x BlockStart=%08x", m_unpackPtr, m_writePtr, BlockStart);

    m_oldFilterLengths[FiltPos] = StackFilter->BlockLength;

    memset(StackFilter->Prg.InitR, 0, sizeof(StackFilter->Prg.InitR));
    StackFilter->Prg.InitR[3] = VM_GLOBALMEMADDR;
    StackFilter->Prg.InitR[4] = StackFilter->BlockLength;
    StackFilter->Prg.InitR[5] = StackFilter->ExecCount;

    if (FirstByte & 0x10)   // set registers to optional parameters if any
    {
        unsigned int InitMask = Inp.fgetbits()>>9;
        Inp.faddbits(7);
        for (int i=0;i<7;i++)
            if (InitMask & (1<<i))
                StackFilter->Prg.InitR[i] = RarVM::ReadData(Inp);
    }

    if (NewFilter)
    {
        uint VMCodeSize = RarVM::ReadData(Inp);
        if (VMCodeSize >= 0x10000 || VMCodeSize == 0)
            return false;
        Array<byte> VMCode(VMCodeSize);
        for (uint i=0;i<VMCodeSize;i++)
        {
            if (Inp.Overflow(3))
                return false;
            VMCode[i] = (Inp.fgetbits() >> 8);
            Inp.faddbits(8);
        }
        m_vm.Prepare(&VMCode[0], VMCodeSize, &Filter->Prg);
    }

    StackFilter->Prg.AltCmd   = & (Filter->Prg.Cmd[0]);
    StackFilter->Prg.CmdCount = Filter->Prg.CmdCount;

    size_t StaticDataSize = Filter->Prg.StaticData.Size();
    if ( StaticDataSize > 0 && StaticDataSize < VM_GLOBALMEMSIZE)
    {
        // read statically defined data contained in DB commands
        StackFilter->Prg.StaticData.Add(StaticDataSize);
        memcpy(&StackFilter->Prg.StaticData[0], &Filter->Prg.StaticData[0], StaticDataSize);
    }

    if (StackFilter->Prg.GlobalData.Size() <  VM_FIXEDGLOBALSIZE)
    {
        StackFilter->Prg.GlobalData.Reset();
        StackFilter->Prg.GlobalData.Add(VM_FIXEDGLOBALSIZE);
    }
    byte *GlobalData = &StackFilter->Prg.GlobalData[0];
    for (int i=0;i<7;i++)
        m_vm.SetLowEndianValue((uint *)&GlobalData[i*4], StackFilter->Prg.InitR[i]);
    m_vm.SetLowEndianValue((uint *)&GlobalData[0x1c], StackFilter->BlockLength);
    m_vm.SetLowEndianValue((uint *)&GlobalData[0x20], 0);
    m_vm.SetLowEndianValue((uint *)&GlobalData[0x2c], StackFilter->ExecCount);
    memset(&GlobalData[0x30], 0, 16);

    if (FirstByte & 8) // put data block passed as parameter if any
    {
        if (Inp.Overflow(3))
            return false;
        uint DataSize = RarVM::ReadData(Inp);
        if (DataSize > VM_GLOBALMEMSIZE - VM_FIXEDGLOBALSIZE)
            return false;
        size_t CurSize = StackFilter->Prg.GlobalData.Size();
        if (CurSize < DataSize + VM_FIXEDGLOBALSIZE)
            StackFilter->Prg.GlobalData.Add(DataSize + VM_FIXEDGLOBALSIZE - CurSize);
        byte *GlobalData = &StackFilter->Prg.GlobalData[VM_FIXEDGLOBALSIZE];
        for (uint i=0;i<DataSize;i++)
        {
            if (Inp.Overflow(3))
                return false;
            GlobalData[i] = (Inp.fgetbits() >> 8);
            Inp.faddbits(8);
        }
    }
    return true;
}


bool Unpack::UnpReadBuf()
{
    int datasize = m_readtop - InAddr; // Data left to process.
    if (datasize < 0)
        return false;

    if (InAddr > BitInput::MAX_SIZE/2)
    {
        // If we already processed more than half of buffer, let's move
        // remaining data into beginning to free more space for new data.
        if (datasize > 0)
            memmove(InBuf, InBuf+InAddr, datasize);
        InAddr  = 0;
        m_readtop = datasize;
    }
    else
    {
        datasize = m_readtop;
    }

    int readCode = m_io->UnpRead(InBuf+datasize, (BitInput::MAX_SIZE-datasize) & ~0xf);

    if (readCode > 0)
        m_readtop += readCode;

    m_readborder = m_readtop - 30;

    return (readCode != -1);
}


void Unpack::UnpWriteBuf()
{
    unsigned int WrittenBorder = m_writePtr;
    unsigned int WriteSize     = (m_unpackPtr - WrittenBorder) & MAXWINMASK;
    for (size_t i=0;i<m_progStack.Size();i++)
    {
        // Here we apply filters to data which we need to write.
        // We always copy data to virtual machine memory before processing.
        // We cannot process them just in place in m_window buffer, because
        // these data can be used for future string matches, so we must
        // preserve them in original form.

        UnpackFilter * flt = m_progStack[i];
        if (flt == NULL)
            continue;
        if (flt->NextWindow)
        {
            flt->NextWindow = false;
            continue;
        }
        unsigned int BlockStart  = flt->BlockStart;
        unsigned int BlockLength = flt->BlockLength;
        if (((BlockStart - WrittenBorder) & MAXWINMASK) < WriteSize)
        {
            if (WrittenBorder != BlockStart)
            {
                UnpWriteArea(WrittenBorder, BlockStart);
                WrittenBorder = BlockStart;
                WriteSize     = (m_unpackPtr - WrittenBorder) & MAXWINMASK;
            }
            if (BlockLength <= WriteSize)
            {
                unsigned int BlockEnd = (BlockStart + BlockLength) & MAXWINMASK;
                if (BlockStart < BlockEnd || BlockEnd == 0)
                    m_vm.SetMemory(0, m_window+BlockStart, BlockLength);
                else
                {
                    unsigned int FirstPartLength = MAXWINSIZE - BlockStart;
                    m_vm.SetMemory(0, m_window+BlockStart, FirstPartLength);
                    m_vm.SetMemory(FirstPartLength, m_window, BlockEnd);
                }

                VM_PreparedProgram *ParentPrg = &m_filters[flt->ParentFilter]->Prg;
                VM_PreparedProgram *Prg = &flt->Prg;

                if (ParentPrg->GlobalData.Size() > VM_FIXEDGLOBALSIZE)
                {
                    // Copy global data from previous script execution if any.
                    Prg->GlobalData.Alloc(ParentPrg->GlobalData.Size());
                    memcpy(&Prg->GlobalData[VM_FIXEDGLOBALSIZE], &ParentPrg->GlobalData[VM_FIXEDGLOBALSIZE], ParentPrg->GlobalData.Size()-VM_FIXEDGLOBALSIZE);
                }

                ExecuteCode(Prg);

                if (Prg->GlobalData.Size() > VM_FIXEDGLOBALSIZE)
                {
                    // Save global data for next script execution.
                    if (ParentPrg->GlobalData.Size() < Prg->GlobalData.Size())
                        ParentPrg->GlobalData.Alloc(Prg->GlobalData.Size());

                    memcpy(&ParentPrg->GlobalData[VM_FIXEDGLOBALSIZE], &Prg->GlobalData[VM_FIXEDGLOBALSIZE], Prg->GlobalData.Size()-VM_FIXEDGLOBALSIZE);
                }
                else
                {
                    ParentPrg->GlobalData.Reset();
                }

                byte * FilteredData = Prg->FilteredData;
                unsigned int FilteredDataSize = Prg->FilteredDataSize;

                delete m_progStack[i];
                m_progStack[i] = NULL;
                while ( i+1 < m_progStack.Size())
                {
                    UnpackFilter *NextFilter = m_progStack[i+1];
                    if (    NextFilter == NULL ||
                            NextFilter->BlockStart != BlockStart ||
                            NextFilter->BlockLength != FilteredDataSize ||
                            NextFilter->NextWindow)
                        break;

                    // Apply several filters to same data block.

                    m_vm.SetMemory(0, FilteredData, FilteredDataSize);

                    VM_PreparedProgram *ParentPrg = &m_filters[NextFilter->ParentFilter]->Prg;
                    VM_PreparedProgram *NextPrg   = &NextFilter->Prg;

                    if (ParentPrg->GlobalData.Size()>VM_FIXEDGLOBALSIZE)
                    {
                        // Copy global data from previous script execution if any.
                        NextPrg->GlobalData.Alloc(ParentPrg->GlobalData.Size());
                        memcpy(&NextPrg->GlobalData[VM_FIXEDGLOBALSIZE], &ParentPrg->GlobalData[VM_FIXEDGLOBALSIZE], ParentPrg->GlobalData.Size()-VM_FIXEDGLOBALSIZE);
                    }

                    ExecuteCode(NextPrg);

                    if (NextPrg->GlobalData.Size()>VM_FIXEDGLOBALSIZE)
                    {
                        // Save global data for next script execution.
                        if (ParentPrg->GlobalData.Size()<NextPrg->GlobalData.Size())
                            ParentPrg->GlobalData.Alloc(NextPrg->GlobalData.Size());
                        memcpy(&ParentPrg->GlobalData[VM_FIXEDGLOBALSIZE],&NextPrg->GlobalData[VM_FIXEDGLOBALSIZE], NextPrg->GlobalData.Size()-VM_FIXEDGLOBALSIZE);
                    }
                    else
                        ParentPrg->GlobalData.Reset();

                    FilteredData     = NextPrg->FilteredData;
                    FilteredDataSize = NextPrg->FilteredDataSize;
                    i++;
                    delete m_progStack[i];
                    m_progStack[i] = NULL;
                }
                m_io->UnpWrite(FilteredData, FilteredDataSize);
                m_writtenSize += FilteredDataSize;
                WrittenBorder = BlockEnd;
                WriteSize     = (m_unpackPtr-WrittenBorder)&MAXWINMASK;
            }
            else
            {
                for (size_t J=i;J<m_progStack.Size();J++)
                {
                    UnpackFilter * flt = m_progStack[J];
                    if (flt!=NULL && flt->NextWindow)
                        flt->NextWindow=false;
                }
                m_writePtr = WrittenBorder;
                return;
            }
        }
    }

    UnpWriteArea(WrittenBorder, m_unpackPtr);
    m_writePtr = m_unpackPtr;
}


void Unpack::ExecuteCode(VM_PreparedProgram *prog)
{
    if (prog->GlobalData.Size()>0)
    {
        prog->InitR[6]=(uint)m_writtenSize;
        m_vm.SetLowEndianValue((uint *)&prog->GlobalData[0x24],(uint)m_writtenSize);
        m_vm.SetLowEndianValue((uint *)&prog->GlobalData[0x28],(uint)(m_writtenSize>>32));
        m_vm.Execute(prog);
    }
}


void Unpack::UnpWriteArea(unsigned int startPtr, unsigned int endPtr)
{
    if (endPtr < startPtr)
    {
        UnpWriteData(&m_window[startPtr], -(int)startPtr & MAXWINMASK);
        UnpWriteData(m_window, endPtr);
    }
    else
    {
        UnpWriteData(&m_window[startPtr], endPtr-startPtr);
    }
}


void Unpack::UnpWriteData(byte *data, size_t size)
{
    if (m_writtenSize >= m_destUnpSize)
        return;

    size_t writesize  = size;
    int64 leftToWrite = m_destUnpSize - m_writtenSize;
    if ((int64)writesize > leftToWrite)
        writesize = (size_t)leftToWrite;

    m_io->UnpWrite(data, writesize);
    m_writtenSize += size;
}


bool Unpack::ReadTables()
{
    byte BitLength[BC];
    byte Table[HUFF_TABLE_SIZE];

    if (InAddr > m_readtop-25)
        if (!UnpReadBuf())
            return false;

    faddbits((8-InBit) & 7);
    uint BitField = fgetbits();

    if (BitField & 0x8000)
    {
        m_blocktype = BLOCK_PPM;
        return(m_ppm.DecodeInit(this, m_EscCharOfPPM));
    }

    m_blocktype              = BLOCK_LZ;
    m_prevLowDistance        = 0;
    m_lowDistanceRepeatCount = 0;

    if (!(BitField & 0x4000))
        memset(m_oldUnpackTable, 0, sizeof(m_oldUnpackTable));

    faddbits(2);

    for (int i=0;i<BC;i++)
    {
        int Length = (byte)(fgetbits() >> 12);
        faddbits(4);

        if (Length == 15)
        {
            int ZeroCount = (byte)(fgetbits() >> 12);
            faddbits(4);

            if (ZeroCount == 0)
            {
                BitLength[i] = 15;
            }
            else
            {
                ZeroCount += 2;
                while (ZeroCount-- > 0 && i < sizeof(BitLength)/sizeof(BitLength[0]))
                    BitLength[i++] = 0;
                i--;
            }
        }
        else
        {
            BitLength[i]=Length;
        }
    }

    MakeDecodeTables(BitLength, &m_BD, BC);

    const int TableSize = HUFF_TABLE_SIZE;
    for (int i=0;i<TableSize;)
    {
        if (InAddr > m_readtop-5)
            if (!UnpReadBuf())
                return false;

        int Number = DecodeNumber(&m_BD);
        if (Number < 16)
        {
            Table[i] = (Number + m_oldUnpackTable[i]) & 0xf;
            i++;
        }
        else
        {
            if (Number < 18)
            {
                int N;
                if (Number == 16)
                {
                    N = (fgetbits() >> 13) + 3;
                    faddbits(3);
                }
                else
                {
                    N = (fgetbits() >> 9) + 11;
                    faddbits(7);
                }

                while (N-- > 0 && i < TableSize)
                {
                    Table[i] = Table[i-1];
                    i++;
                }
            }
            else
            {
                int N;
                if (Number == 18)
                {
                    N = (fgetbits() >> 13) + 3;
                    faddbits(3);
                }
                else
                {
                    N = (fgetbits() >> 9) + 11;
                    faddbits(7);
                }
                while (N-- > 0 && i < TableSize)
                    Table[i++] = 0;
            }

        }
    }

    m_hasReadTables = true;
    if (InAddr > m_readtop)
        return false;

    MakeDecodeTables(&Table[0], &m_LD, NC);
    MakeDecodeTables(&Table[NC], &m_DD, DC);
    MakeDecodeTables(&Table[NC+DC], &m_LDD, LDC);
    MakeDecodeTables(&Table[NC+DC+LDC], &m_RD, RC);
    memcpy(m_oldUnpackTable, Table, sizeof(m_oldUnpackTable));

    return true;
}


void Unpack::UnpInitData(int Solid)
{
    if (!Solid)
    {
        m_hasReadTables = false;
        memset(m_oldDistances, 0, sizeof(m_oldDistances));
        m_oldDistancePtr = 0;
        m_lastDistance = m_lastLength = 0;

        //    memset(m_window, 0, MAXWINSIZE);
        memset(m_oldUnpackTable, 0, sizeof(m_oldUnpackTable));
        memset(&m_LD, 0, sizeof(m_LD));
        memset(&m_DD, 0, sizeof(m_DD));
        memset(&m_LDD, 0, sizeof(m_LDD));
        memset(&m_RD, 0, sizeof(m_RD));
        memset(&m_BD, 0, sizeof(m_BD));

        m_unpackPtr = m_writePtr = 0;
        m_EscCharOfPPM = 2;
        m_blocktype = BLOCK_LZ;

        ResetFilters();
    }

    InitBitInput();
    m_writtenSize = 0;
    m_readtop     = 0;
    m_readborder  = 0;

}


void Unpack::ResetFilters()
{
    m_oldFilterLengths.Reset();
    m_lastfilter = 0;

    for (size_t i=0; i < m_filters.Size(); i++)
        delete m_filters[i];
    m_filters.Reset();

    for (size_t i=0;i<m_progStack.Size();i++)
        delete m_progStack[i];
    m_progStack.Reset();
}


// LengthTable contains the length in bits for every element of alphabet.
// Dec is the structure to decode Huffman code/
// Size is size of length table and DecodeNum field in Dec structure,
void Unpack::MakeDecodeTables(byte *LengthTable, DecodeTable *Dec, uint Size)
{
    // Size of alphabet and DecodePos array.
    Dec->MaxNum = Size;

    // Calculate how many entries for every bit length in LengthTable we have.
    uint LengthCount[16];
    memset(LengthCount, 0, sizeof(LengthCount));
    for (size_t i=0;i<Size;i++)
        LengthCount[LengthTable[i] & 0xf]++;

    // We must not calculate the number of zero length codes.
    LengthCount[0] = 0;

    // Set the entire DecodeNum to zero.
    memset(Dec->DecodeNum, 0, Size * sizeof(*Dec->DecodeNum));

    // Initialize not really used entry for zero length code.
    Dec->DecodePos[0] = 0;

    // Start code for bit length 1 is 0.
    Dec->DecodeLen[0] = 0;

    // Right aligned upper limit code for current bit length.
    uint UpperLimit = 0;

    for (size_t i=1;i<16;i++)
    {
        // Adjust the upper limit code.
        UpperLimit += LengthCount[i];

        // Left aligned upper limit code.
        uint LeftAligned = (UpperLimit << (16-i));

        // Prepare the upper limit code for next bit length.
        UpperLimit *= 2;

        // Store the left aligned upper limit code.
        Dec->DecodeLen[i] = (uint)LeftAligned;

        // Every item of this array contains the sum of all preceding items.
        // So it contains the start position in code list for every bit length.
        Dec->DecodePos[i] = Dec->DecodePos[i-1] + LengthCount[i-1];
    }

    // Prepare the copy of DecodePos. We'll modify this copy below,
    // so we cannot use the original DecodePos.
    uint CopyDecodePos[16];
    memcpy(CopyDecodePos, Dec->DecodePos, sizeof(CopyDecodePos));

    // For every bit length in the bit length table and so for every item
    // of alphabet.
    for (uint i=0;i<Size;i++)
    {
        // Get the current bit length.
        byte CurBitLength = LengthTable[i] & 0xf;

        if (CurBitLength != 0)
        {
            // Last position in code list for current bit length.
            uint LastPos = CopyDecodePos[CurBitLength];

            // Prepare the decode table, so this position in code list will be
            // decoded to current alphabet item number.
            Dec->DecodeNum[LastPos] = i;

            // We'll use next position number for this bit length next time.
            // So we pass through the entire range of positions available
            // for every bit length.
            CopyDecodePos[CurBitLength]++;
        }
    }

    // Define the number of bits to process in quick mode. We use more bits
    // for larger alphabets. More bits means that more codes will be processed
    // in quick mode, but also that more time will be spent to preparation
    // of tables for quick decode.
    switch (Size)
    {
        case NC:
        case NC20:
            Dec->QuickBits = MAX_QUICK_DECODE_BITS;
            break;
        default:
            Dec->QuickBits = MAX_QUICK_DECODE_BITS - 3;
            break;
    }

    // Size of tables for quick mode.
    uint QuickDataSize = (1 << Dec->QuickBits);

    // Bit length for current code, start from 1 bit codes. It is important
    // to use 1 bit instead of 0 for minimum code length, so we are moving
    // forward even when processing a corrupt archive.
    uint CurBitLength = 1;

    // For every right aligned bit string which supports the quick decoding.
    for (uint Code=0;Code<QuickDataSize;Code++)
    {
        // Left align the current code, so it will be in usual bit field format.
        uint BitField = Code << (16 - Dec->QuickBits);

        // Prepare the table for quick decoding of bit lengths.

        // Find the upper limit for current bit field and adjust the bit length
        // accordingly if necessary.
        while (BitField >= Dec->DecodeLen[CurBitLength] &&
               CurBitLength < ASIZE(Dec->DecodeLen))
            CurBitLength++;

        // Translation of right aligned bit string to bit length.
        Dec->QuickLen[Code] = CurBitLength;

        // Prepare the table for quick translation of position in code list
        // to position in alphabet.

        // Calculate the distance from the start code for current bit length.
        uint Dist = BitField - Dec->DecodeLen[CurBitLength-1];

        // Right align the distance.
        Dist >>= (16 - CurBitLength);

        // Now we can calculate the position in the code list. It is the sum
        // of first position for current bit length and right aligned distance
        // between our bit field and start code for current bit length.
        uint Pos = Dec->DecodePos[CurBitLength] + Dist;

        if (Pos<Size) // Safety check for damaged archives.
        {
            // Define the code to alphabet number translation.
            Dec->QuickNum[Code] = Dec->DecodeNum[Pos];
        }
        else
        {
            Dec->QuickNum[Code] = 0;
        }
    }
}
