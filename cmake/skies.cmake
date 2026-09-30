# Sky images for the moods in assets/moods (kke::Mood, docs/MOODS.md):
# equirectangular HDR "pure skies" from Poly Haven (https://polyhaven.com),
# CC0 (public domain), 2K. Fetched once at configure time like the other
# dependencies, checked against their SHA-256, cached in the build folder
# and copied to bin/assets/skies. Not in the repository: 40 MB of pictures
# would weigh on every clone.
#
# -DKKE_FETCH_SKIES=OFF skips the download; the moods then show their
# gradient skies instead (and say so once in the log).
option(KKE_FETCH_SKIES "Download the CC0 sky images the moods use (assets/moods)" ON)

# name (as the moods use it) | Poly Haven id | SHA-256 of the 2K .hdr
set(KKE_SKIES
    "clear_day|kloofendal_48d_partly_cloudy_puresky|5244534e9cf5b606f2ff513aa00ddb161b0a4826ffd88a0d3bd03ac29247d198"
    "morning|qwantani_mid_morning_puresky|c7df6496906754b8068e78a1766c79ac7438b7cd7c17e7b3f9c7f851d7125b44"
    "noon|qwantani_noon_puresky|d070d48c8e802c92027b0f7b1965b5bd820b0d25afca225503abfec67e00ad07"
    "late_afternoon|qwantani_late_afternoon_puresky|d1c4d610811b7b94328d67fa48fc107078af21318197e5b937310c825d72074d"
    "sunset|qwantani_sunset_puresky|d66e08231e9c09ca40c6d035214ae8efb638782745c82addbb2a063fa32cabef"
    "dusk|qwantani_dusk_2_puresky|440d1b5d29c79bea0985a13777642df6829e80399602a9d5f39bc20203ae6d1b"
    "night|qwantani_night_puresky|d458fe7f20969d89eedbb7ae12d346abf68c86d844235696e7d70baad3e04cf0"
    "overcast|kloofendal_overcast_puresky|312b1b04b7f10057a4f1418abc59d1166c8933cc93fc72051502edaf8d6b2fcd"
    "misty_morning|kloofendal_misty_morning_puresky|a59683cd08861fb18d69ee8f3938f50ec81c6feda5381e8b8576f340d5a25bb1"
    "stormy|wasteland_clouds_puresky|c9be5b722797528f2deae5969d6af061e75fca912675bcb11d467de5eea427c6"
)

set(KKE_SKY_FILES "")
if(KKE_FETCH_SKIES)
    set(_sky_cache ${CMAKE_BINARY_DIR}/_skies)
    foreach(entry ${KKE_SKIES})
        string(REPLACE "|" ";" parts "${entry}")
        list(GET parts 0 name)
        list(GET parts 1 id)
        list(GET parts 2 sha)
        set(file ${_sky_cache}/${name}.hdr)
        if(EXISTS ${file})
            file(SHA256 ${file} have)
        else()
            set(have "")
        endif()
        if(NOT have STREQUAL sha)
            message(STATUS "Sky '${name}': downloading ${id} (Poly Haven, CC0)")
            # The hash is checked here, not with EXPECTED_HASH: that turns a
            # failed download (offline, blocked host) into a CMake error, and
            # a missing sky should fall back to the gradient, not stop configure.
            file(DOWNLOAD "https://dl.polyhaven.org/file/ph-assets/HDRIs/hdr/2k/${id}_2k.hdr" ${file}.part
                 STATUS status TLS_VERIFY ON)
            list(GET status 0 code)
            list(GET status 1 reason)
            if(code EQUAL 0)
                file(SHA256 ${file}.part got)
                if(NOT got STREQUAL sha)
                    set(code 1)
                    set(reason "SHA-256 mismatch")
                endif()
            endif()
            if(code EQUAL 0)
                file(RENAME ${file}.part ${file})
            else()
                file(REMOVE ${file}.part)
                message(STATUS "Sky '${name}': not downloaded (${reason}); moods that use it show a gradient sky")
                continue()
            endif()
        endif()
        list(APPEND KKE_SKY_FILES ${name})
    endforeach()
endif()

# Every game gets the moods, their ambience loops and whichever skies were fetched (called from
# engine/CMakeLists.txt for kke_engine, an INTERFACE dependency of all games).
function(kke_add_moods TARGET)
    file(GLOB mood_files CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/assets/moods/*.yaml ${CMAKE_SOURCE_DIR}/assets/moods/*.yml ${CMAKE_SOURCE_DIR}/assets/moods/*.json)
    foreach(f ${mood_files})
        get_filename_component(n ${f} NAME)
        engine_copy_runtime_file(${TARGET} ${f} assets/moods/${n})
    endforeach()
    # The moods' ambience loops (CC0, from OpenGameArt; docs/DEPENDENCIES.md).
    file(GLOB ambience_files CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/assets/ambience/*.flac)
    foreach(f ${ambience_files})
        get_filename_component(n ${f} NAME)
        engine_copy_runtime_file(${TARGET} ${f} assets/ambience/${n})
    endforeach()
    foreach(name ${KKE_SKY_FILES})
        engine_copy_runtime_file(${TARGET} ${CMAKE_BINARY_DIR}/_skies/${name}.hdr assets/skies/${name}.hdr)
    endforeach()
endfunction()
