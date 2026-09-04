SOLUTION_NAME = "FEMFXLib"
FEMFX_LIB_NAME = "AMD_FEMFX"

solution ( SOLUTION_NAME )

-- Solution-wide config

    filename(SOLUTION_NAME.."_".._ACTION)
    configurations { "Debug", "Release" }
    platforms { "x64" }
    symbols "On"
    architecture "x86_64"
    flags { "MultiProcessorCompile", "NoBufferSecurityCheck" }
    floatingpoint "Fast"
    vectorextensions "AVX2"
    -- FMA3 is a separate CPU feature flag from AVX2 in GCC/Clang (even
    -- though virtually every real AVX2-capable CPU also has FMA3) —
    -- needed because simd_madd_ps calls _mm_fmadd_ps, which GCC refuses
    -- to inline without this flag even present, rather than silently
    -- emitting a non-inlined call.
    filter "action:gmake2"
        buildoptions { "-mfma" }
    filter {}
    exceptionhandling ("Off")
    --callingconvention ("VectorCall")

    defines { 
        "WIN32", 
        "NOMINMAX",
        "__forceinline=inline"
	}

    objdir ('obj/'.._ACTION..'/%{cfg.architecture}')
    targetdir ( 'build/'.._ACTION..'/%{cfg.architecture}/%{cfg.platform}/%{cfg.buildcfg}' )

    filter "kind:StaticLib"
        defines "_LIB"

    filter "configurations:Debug" 
        defines { "_DEBUG" }
        targetsuffix("_d")

    filter "configurations:Release"
        defines { "NDEBUG" }
        optimize "Speed"

	filter "action:vs2017"
		systemversion "10.0.17763.0"
    
include "."

