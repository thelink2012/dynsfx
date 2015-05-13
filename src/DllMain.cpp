/*
 * Copyright (C) 2015  LINK/2012 <dma_2012@hotmail.com>
 * Licensed under the MIT License, see LICENSE at top level directory.
 * 
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <injector/injector.hpp>
#include <injector/hooking.hpp>
using namespace injector;

extern void InjectCustomBankLoader();

extern "C"
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if(fdwReason == DLL_PROCESS_ATTACH)
    {
        auto& gvm = injector::address_manager::singleton();

        // Works only in GTA SA 1.0 [US|EU]
        if(gvm.IsSA() && gvm.GetMajorVersion() == 1 && gvm.GetMinorVersion() == 0)
        {
            // So we are going to do lazy patching here. Reason? Mod Loader!
            //
            // We should ensure Mod Loader didn't do this dynamic sfx patch already,
            // because ML's patch is more important since it extends the game to deal with external waves.
            //
            using hpatch = function_hooker_thiscall<0x74872D, int()>;
            make_static_hook<hpatch>([](hpatch::func_type IsAlreadyRunning) -> int
            {
                auto is_running = IsAlreadyRunning();
                if(!is_running)
                {
                    // Check if the CAEBankLoader::Service function hasn't been detoured by Mod Loader already.
                    if(ReadMemory<uint8_t>(0x4DFE30, true) != 0xE9) // JMP
                        InjectCustomBankLoader();
                }
                return is_running;
            });
        }
    }
    return TRUE;
}
