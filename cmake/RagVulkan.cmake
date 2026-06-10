macro(rag_find_vulkan)
    if(WIN32 AND DEFINED ENV{VULKAN_SDK})
        file(TO_CMAKE_PATH "$ENV{VULKAN_SDK}" RAG_VULKAN_SDK)
        list(PREPEND CMAKE_PREFIX_PATH "${RAG_VULKAN_SDK}")
        set(Vulkan_ROOT "${RAG_VULKAN_SDK}")

        set(Vulkan_INCLUDE_DIR "${RAG_VULKAN_SDK}/Include" CACHE PATH "RAG Engine Vulkan SDK include directory")
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(_RAG_VULKAN_LIBRARY "${RAG_VULKAN_SDK}/Lib/vulkan-1.lib")
        else()
            set(_RAG_VULKAN_LIBRARY "${RAG_VULKAN_SDK}/Lib32/vulkan-1.lib")
        endif()

        if(EXISTS "${_RAG_VULKAN_LIBRARY}")
            set(Vulkan_LIBRARY "${_RAG_VULKAN_LIBRARY}" CACHE FILEPATH "RAG Engine Vulkan loader library")
        endif()

        message(STATUS "RAG Engine: using VULKAN_SDK=${RAG_VULKAN_SDK}")
    endif()

    find_package(Vulkan REQUIRED)

    if(NOT Vulkan_FOUND)
        message(FATAL_ERROR
            "RAG_ENABLE_VULKAN is ON, but CMake could not find the Vulkan SDK. "
            "Install the LunarG Vulkan SDK, ensure VULKAN_SDK is set, or configure "
            "with -DRAG_ENABLE_VULKAN=OFF for the non-Vulkan sandbox.")
    endif()

    if(NOT TARGET Vulkan::Vulkan)
        message(FATAL_ERROR "RAG Engine requires CMake's imported target Vulkan::Vulkan.")
    endif()

    find_program(
        RAG_GLSLANG_VALIDATOR_EXECUTABLE
        NAMES glslangValidator glslangValidator.exe
        HINTS
            "$ENV{VULKAN_SDK}/Bin"
            "${RAG_VULKAN_SDK}/Bin"
        DOC "Path to the LunarG Vulkan SDK glslangValidator executable"
    )

    if(NOT RAG_GLSLANG_VALIDATOR_EXECUTABLE)
        message(FATAL_ERROR
            "RAG Engine Phase 2B requires glslangValidator from the LunarG Vulkan SDK "
            "to compile GLSL shaders. Reinstall the SDK with shader tools enabled.")
    endif()

    message(STATUS "RAG Engine: Vulkan include dirs: ${Vulkan_INCLUDE_DIRS}")
    message(STATUS "RAG Engine: Vulkan libraries: ${Vulkan_LIBRARIES}")
    message(STATUS "RAG Engine: glslangValidator: ${RAG_GLSLANG_VALIDATOR_EXECUTABLE}")
endmacro()
