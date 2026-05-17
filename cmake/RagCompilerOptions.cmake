function(rag_configure_target target_name)
    target_compile_features(${target_name} PUBLIC cxx_std_20)
    target_include_directories(${target_name} PUBLIC ${PROJECT_SOURCE_DIR}/src)

    target_compile_definitions(${target_name}
        PUBLIC
            RAG_ENGINE_VERSION="${PROJECT_VERSION}"
            $<$<CONFIG:Debug>:RAG_DEBUG=1>
            $<$<CONFIG:Debug>:RAG_ENABLE_ASSERTS=1>
            $<$<NOT:$<CONFIG:Debug>>:RAG_RELEASE=1>
    )

    if(WIN32)
        target_compile_definitions(${target_name}
            PUBLIC
                RAG_PLATFORM_WINDOWS=1
                WIN32_LEAN_AND_MEAN
                NOMINMAX
                UNICODE
                _UNICODE
        )
    endif()

    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4 /permissive- /EHsc /MP /Zc:preprocessor /utf-8)
    else()
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()
