/*
 * Copyright (C) 2014  LINK/2012 <dma_2012@hotmail.com>
 * Licensed under the MIT License, see LICENSE at top level directory.
 * 
 *  CAECustomBankLoader
 *  This is a custom bank loader for GTA San Andreas. The differences between the original and this are:
 *      [*] Dynamically allocated sound buffers instead of BankSlot.dat pre-allocation.
 *      [*] Dedicated thread to load banks
 *      [*] Capable of reading bank dumps split on the diskse
 *      [*] Capable of reading wave files on the disk
 * 
 *  Before you ask if BankSlot.dat pre-allocation isn't faster...
 *  No, it isn't, because the game ends up allocating a temporary buffer of memory when reading the bank from the SFXPak anyway.
 * 
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <map>
#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>
#include <injector/injector.hpp>
#include <injector/calling.hpp>
#include <injector/hooking.hpp>
#include <injector/utility.hpp>
#include "Queue.h"
#include "CAEBankLoader.h"

using namespace injector;
void InjectCustomBankLoader();

// Request status
enum
{
    REQUEST_STATUS_NULL         = 0,
    REQUEST_STATUS_BEGIN        = 1,
    REQUEST_STATUS_CUSTOM       = 100,  // Following are custom status
    REQUEST_STATUS_IN_PROGRESS,
    REQUEST_STATUS_DONE
};

// Bank loading thread
static Queue queue;                                 // Request queue
static HANDLE hSemaphore;                           // Thread semaphore
static HANDLE hThread;                              // Thread handle
static DWORD __stdcall BankLoadingThread(void*);    // Thread body

// Bank information for lookup so there's no need to peek the SFXPak for the bank header
static class CAEBankInfo* pBankInfo;
static HANDLE* hFiles;

/*
 *  CAEBankHeader
 *      Information about a bank.
 */
struct CAEBankHeader
{
    public:
        friend class CAEBankInfo;
        BankHeader          m_Header;       // Sounds information
        CAEBankLookupItem*  m_pLookup;      // Bank offset / size information

    public:
        unsigned short GetNumSounds()
        {
            return m_Header.m_nSounds;
        }
        
        // Gets the bank header offset in the file
        unsigned int GetBankOffset()
        {
            return m_pLookup->m_dwOffset;
        }
        
        // Gets the bank sound buffer offset in the file
        unsigned int GetSoundOffset()
        {
            return GetBankOffset() + sizeof(BankHeader);
        }
        
        unsigned int GetSoundOffsetRaw(unsigned short usSound)
        {
            return m_Header.m_aSounds[usSound].m_dwOffset;
        }
        
        // Gets the bank sound buffer offset for the sound id @usSound in the file
        unsigned int GetSoundOffset(unsigned short usSound)
        {
            return GetSoundOffset() + GetSoundOffsetRaw(usSound);
        }
        
        // Gets the bank sound buffer size
        unsigned int GetSoundSize()
        {
            return m_pLookup->m_dwSize;
        }
        
        // Gets the sound buffer size for the sound in @usSound
        unsigned int GetSoundSize(unsigned short usSound)
        {
            short nSoundsOnBank = this->m_Header.m_nSounds;
            
            // If it's the last sound on the bank we should use the bank size to find the 'diff'
            if(usSound >= nSoundsOnBank - 1)    // Diff between bank size and this sound offset
                return m_pLookup->m_dwSize - m_Header.m_aSounds[usSound].m_dwOffset;
            else                                // Diff between next sound offset and this sound offset
                return m_Header.m_aSounds[(usSound+1) % 400].m_dwOffset - m_Header.m_aSounds[usSound].m_dwOffset;
        }
        
        void* AllocateBankSlot(CAEBankSlot& b, CAESoundRequest& r, unsigned int& dwOffset, unsigned int& dwSize)
        {
            unsigned short usSound = r.m_usSound;
            bool bSingleSound = usSound != 0xFFFF;
            
            // Setup output data
            void* pBuffer = r.m_pBuffer;
            dwOffset = bSingleSound? GetSoundOffset(usSound) : GetSoundOffset();
            dwSize   = bSingleSound? GetSoundSize(usSound)   : GetSoundSize();

            // Cleanup the old buffer
            if(pBuffer) operator delete(pBuffer);
            // Allocate new sound buffer
            pBuffer = operator new(dwSize);
            
            // Setup the bankslot data
            b.m_dwSlotBufferSize = dwSize;
            b.m_nSoundsOnBank    = GetNumSounds();
            r.m_pBuffer          = pBuffer;
            r.m_pBufferData      = m_Header.m_aSounds;
            
            return pBuffer;
        }
};


/*
 *  CAEBankInfo
 *      Stores information from a bank in memory, including it's filepath.
 */
class CAEBankInfo
{
    protected:
        friend DWORD __stdcall BankLoadingThread(void*);
        friend class CAECustomBankLoader;

        int             m_iFileId;        // Bank file handle (index in hFiles array)
        CAEBankHeader   m_OriginalHeader; // Bank information
        
    public:
        // Loads header and other information about the bank file
        bool FetchBankFile(CAEBankLookupItem* pLookup, short usBankId, int iPak, size_t dwOffset, size_t dwSize);
};


/*
 *  CAECustomBankLoader
 *      Custom bank loader for the game
 */
class CAECustomBankLoader : public CAEBankLoader
{
    public:
        static void Patch();
        
        bool PostInitialise();
        void Finalize();
        void Service();
        bool InitialiseThread();
        void LoadRequest(int i);
        
        const char* GetPakName(unsigned char i)
        {
            return &this->m_pPakFiles[52 * i];
        }
};

static_assert(sizeof(CAECustomBankLoader) == sizeof(CAEBankLoader), "Invalid size of CAECustomBankLoader");





/*
 *  CAECustomBankLoader::PostInitialise
 *      Initialises the custom bank loader....
 *      This is called after the standard bank loader gets initialized.
 */
bool CAECustomBankLoader::PostInitialise()
{
    if(this->InitialiseThread())
    {
        // Our custom bank loader do not perform pre-allocation.....
        // We use CAEBankSlot::m_dwOffsetOnBuffer with the actual allocated memory pointer and things will work!
        this->m_iSoundBuffersSize = 0;
        this->m_pSoundBuffers = nullptr;

        // Cleanup CAEBankSlot pre-allocation information
        for(int i = 0; i < this->m_usNumBankSlots; ++i)
        {
            // Those need to be set when you allocate your own buffer memory
            this->m_pBankSlots[i].m_dwOffsetOnBuffer = 0;
            this->m_pBankSlots[i].m_dwSlotBufferSize = 0;
        }

        //
        hFiles = new HANDLE[this->m_iNumPakFiles];
        for(int i = 0; i < this->m_iNumPakFiles; ++i)
        {
            char szPakPath[MAX_PATH];
            sprintf(szPakPath, "AUDIO/SFX/%s", GetPakName(i));
            hFiles[i] = CreateFileA(szPakPath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                    FILE_FLAG_RANDOM_ACCESS | FILE_ATTRIBUTE_READONLY, nullptr);
        }

        // Store bank information so that we don't have to fetch it every time we need to load a bank or sound
        pBankInfo = new CAEBankInfo[this->m_usNumBanks];
        for(int i = 0; i < this->m_usNumBanks; ++i)
        {
            // Preload the bank header from the SFXPak file!
            auto* lookup = &this->m_pBankLookup[i];
            if(!pBankInfo[i].FetchBankFile(lookup, i, lookup->m_iPak, lookup->m_dwOffset, lookup->m_dwSize))
                return false;
        }

        return true;
    }
    return false;
}

/*
 *  CAECustomBankLoader::InitialiseThread
 *      Initialises the custom bank worker thread....
 *      By default the game uses CdStream thread to process banks, we're separating it right now.
 */
bool CAECustomBankLoader::InitialiseThread()
{
    // Initialise bank loading thread
    if(hSemaphore = CreateSemaphoreA(nullptr, 0, 50 + 1, "BankLoaderSem"))
    {
        hThread = CreateThread(nullptr, 0, BankLoadingThread, this, CREATE_SUSPENDED, nullptr);
        if(hThread)
        {
            // Make the loading thread have the same priority as the main thread
            // This is necessary mainly because of the WinXP behaviour on the Sleep function
            SetThreadPriority(hThread, GetThreadPriority(GetCurrentThread()));
            ResumeThread(hThread);
        }
        else
        {
            CloseHandle(hSemaphore);
            return false;
        }
    }
    else
        return false;

    // Initialise bank loading queue
    InitialiseQueue(&queue, 50 + 1);
    return true;
}

/*
 *  CAECustomBankLoader::Finalize
 *      Finalizes all resources that the custom loader owns
 *      This is called before the standard bank loader gets destroyed
 */
void CAECustomBankLoader::Finalize()
{
    // Cleanup resources
    CloseHandle(hThread);
    CloseHandle(hSemaphore);
    FinalizeQueue(&queue);
    delete[] pBankInfo;

    for(int i = 0; i < this->m_iNumPakFiles; ++i)
        CloseHandle(hFiles[i]);
    delete[] hFiles;

    // Destroy any sound buffer still allocated
    for(int i = 0; i < this->m_usNumBankSlots; ++i)
    {
        operator delete((void*)(this->m_pBankSlots[i].m_dwOffsetOnBuffer));
        this->m_pBankSlots[i].m_dwOffsetOnBuffer = 0;
    }
}

/*
 *  CAEBankInfo::FetchBankFile
 *      Fetches information about a specific bank, from a bank header in a bank file.
 */
bool CAEBankInfo::FetchBankFile(CAEBankLookupItem* pLookup, short usBankId, int iFileId, size_t dwOffset, size_t dwSize)
{
    OVERLAPPED ov = {0};
    ov.Offset     = dwOffset;

    // Read the bank header
    if(ReadFile(hFiles[iFileId], &this->m_OriginalHeader.m_Header, sizeof(BankHeader), 0, &ov))
    {
        // MiniBanks (custom format) size is the entire file size
        if(dwSize == -1) dwSize = GetFileSize(hFiles[iFileId], 0) - sizeof(BankHeader);

        pLookup->m_dwOffset = dwOffset;
        pLookup->m_dwSize = dwSize;
        this->m_iFileId = iFileId;
        this->m_OriginalHeader.m_pLookup = pLookup;
        
        return true;
    }
    return false;
}

/*
 *  CAECustomBankLoader::Service
 *      Processes the bank loading system
 */
void CAECustomBankLoader::Service()
{
    // Process sound requests
    for(int i = 0; i < 50 && this->m_nRequestsToLoad; ++i)
    {
        auto& r = this->m_aSoundRequests[i];
        unsigned short bankslot = r.m_usBankSlot;
        
        switch(r.m_iLoadingStatus)
        {
            // The request has just been sent
            case REQUEST_STATUS_BEGIN:
            {
                auto& b = this->m_pBankSlots[bankslot];
                
                // Mark the bankslot as free, we don't want anyone using it while we're touching it!
                r.m_pBuffer = (void*) b.m_dwOffsetOnBuffer;
                this->m_pBankSlots[bankslot].m_dwOffsetOnBuffer = 0;
                memset(this->m_pBankSlots[bankslot].m_aBankItems, 0, sizeof(this->m_pBankSlots[bankslot].m_aBankItems));
                this->m_pBankSlots[bankslot].m_usBankNum = 0xFFFF;
                this->m_aBankSlotSound[bankslot] = 0xFFFF;
                
                // Request the sound to the bank loading thread
                r.m_iLoadingStatus = REQUEST_STATUS_IN_PROGRESS;
                AddToQueue(&queue, i);
                ReleaseSemaphore(hSemaphore, 1, nullptr);
                
                break;
            }
            
            // The request has been completed, finish it
            case REQUEST_STATUS_DONE:
            {
                auto& b = this->m_pBankSlots[bankslot];
                
                // Mark the bankslot with the loaded bank/sound
                this->m_pBankSlots[bankslot].m_usBankNum = r.m_usBank;
                this->m_aBankSlotSound[bankslot] = r.m_usSound;
                b.m_dwOffsetOnBuffer = (uintptr_t)(r.m_pBuffer);
                memcpy(b.m_aBankItems, r.m_pBufferData, sizeof(b.m_aBankItems));
                
                // Special setup for single sounds
                if(r.m_usSound != 0xFFFF)
                {
                    b.m_aBankItems[r.m_usSound].m_dwOffset = 0;
                    b.m_aBankItems[(r.m_usSound + 1) % 400].m_dwOffset = b.m_dwSlotBufferSize;
                }
                
                // Cleanup request object
                r.m_iLoadingStatus = REQUEST_STATUS_NULL;
                r.m_usBankSlot = 0xFFFF;
                r.m_usBank = 0xFFFF;
                r.m_usSound = 0xFFFF;
                r.m_pBuffer = r.m_pBufferData = nullptr;
                --this->m_nRequestsToLoad;
                
                break;
            }
        }
    }
}

/*
 *  BankLoadingThread 
 *      Thread which loads the bank files on demand.
 */
DWORD __stdcall BankLoadingThread(void* arg)
{
    CAECustomBankLoader& AEBankLoader = *(CAECustomBankLoader*)(arg);
    
    while(true)
    {
        WaitForSingleObject(hSemaphore, INFINITE);
        int i = GetFirstInQueue(&queue);
        AEBankLoader.LoadRequest(i);
        RemoveFirstInQueue(&queue);
        AEBankLoader.m_aSoundRequests[i].m_iLoadingStatus = REQUEST_STATUS_DONE;
    }

    return 0;
}

/*
 *  CAECustomBankLoader::LoadRequest
 *      Load sound request at index i of the request array
 */
void CAECustomBankLoader::LoadRequest(int i)
{
    unsigned int dwOffset, dwSize;
    auto& r = this->m_aSoundRequests[i];
    auto& b = this->m_pBankSlots[r.m_usBankSlot];
    auto& f = pBankInfo[r.m_usBank];                // The bank information

    void* pBuffer = f.m_OriginalHeader.AllocateBankSlot(this->m_pBankSlots[r.m_usBankSlot], r, dwOffset, dwSize);
    if(dwSize != 0)
    {
        OVERLAPPED ov = {0};
        ov.Offset = dwOffset;
        ReadFile(hFiles[f.m_iFileId], pBuffer, dwSize, 0, &ov);
    }
    
    // On single sound request some data must be changed...
    if(r.m_usSound != 0xFFFF)
        b.m_nSoundsOnBank = 0xFFFF;
}


/*
 *  CAECustomBankLoader::Patch
 *      Patches the game to use this custom bank loader 
 */
static void __fastcall ServiceCaller(CAEBankLoader* loader)
{
    return static_cast<CAECustomBankLoader*>(loader)->Service();
}

// Patcher
void CAECustomBankLoader::Patch()
{
    typedef function_hooker_thiscall<0x4D99B3, char(CAEBankLoader*)> ihook;
    typedef function_hooker_thiscall<0x4D9800, void(CAEBankLoader*)> dhook;

    //
    MakeJMP (0x4DFE30, (void*) ServiceCaller);
    MakeCALL(0x4E065B, return_value<void*, nullptr>);   // Return null bankslot pre-allocated memory
    MakeCALL(0x4DFD9D, return_value<void*, nullptr>);   // Return null streaming handle for SFXPak
    MakeNOP (0x4DFDC3, 5);                              // Don't free PakFiles.dat buffer
    MakeNOP (0x4DFDCE, 7);                              // ^

    // After the standard bank loader initialization, initialise our custom bank loader
    make_static_hook<ihook>([](ihook::func_type Initialise, CAEBankLoader*& loader)
    {
        char result = 0;
        if(Initialise(loader))      // Initialise CAEBankLoader
        {
            // Initialise CAECustomBankLoader
            if(static_cast<CAECustomBankLoader*>(loader)->PostInitialise()) 
                result = 1;
        }
        return result;
    });
    
    // Finalizes the custom bank loader
    make_static_hook<dhook>([](dhook::func_type dtor, CAEBankLoader*& loader)
    {
        static_cast<CAECustomBankLoader*>(loader)->Finalize();
        return dtor(loader);
    });
}

/*
 *  InjectCustomBankLoader
 *      To be called from DllMain to patch the game to use this custom bank loader
 */
void InjectCustomBankLoader()
{
    CAECustomBankLoader::Patch();
}
