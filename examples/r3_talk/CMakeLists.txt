if (WHISPER_SDL2)
    # r3_talk
    set(TARGET r3_talk)
    add_executable(${TARGET} r3_talk.cpp)
    target_link_libraries(${TARGET} PRIVATE common common-sdl whisper ${CMAKE_THREAD_LIBS_INIT})

    include(DefaultTargetOptions)
endif ()
