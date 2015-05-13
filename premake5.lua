--[[
    DynSFX Build Script
    Use 'premake5 --help' for help
--]]


--[[
    Options and Actions
--]]

newoption {
    trigger     = "outdir",
    value       = "path",
    description = "Output directory for the build files"
}
if not _OPTIONS["outdir"] then
    _OPTIONS["outdir"] = "build"
end


--[[
    The Solution
--]]
solution "DynSFX"

    configurations { "Release", "Debug" }

    location( _OPTIONS["outdir"] )
    targetprefix "" -- no 'lib' prefix on gcc
    targetdir "bin"
    implibdir "bin"

    flags {
        "StaticRuntime",
        "NoImportLib",
        "NoRTTI",
        "NoBufferSecurityCheck"
    }

    defines {
        'INJECTOR_GVM_PLUGIN_NAME="\\"Dynamic SFX\\""'    -- (additional quotes needed for gmake)
    }

    defines {
        "_CRT_SECURE_NO_WARNINGS",
        "_SCL_SECURE_NO_WARNINGS"
    }

    includedirs {
        "src",
        "deps/injector/include",
    }

    configuration "Debug*"
        flags { "Symbols" }
        
    configuration "Release*"
        defines { "NDEBUG" }
        optimize "Speed"

    configuration "vs*"
        buildoptions { "/arch:IA32" }           -- disable the use of SSE/SSE2 instructions

    project "dynsfx"
        language "C++"
        kind "SharedLib"
        targetname "dynsfx"
        targetextension ".asi"
        
        flags { "NoPCH" }
        
        files {
            "src/**.cpp",
            "src/**.hpp",
            "src/**.h",
        }

