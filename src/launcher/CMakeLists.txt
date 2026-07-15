set(LAUNCHER_SOURCES
  launcher.cpp
)

add_executable(omnigpu_launcher ${LAUNCHER_SOURCES})

target_compile_features(omnigpu_launcher PRIVATE cxx_std_23)

if(WIN32)
  target_link_libraries(omnigpu_launcher PRIVATE
    kernel32
    user32
  )
endif()
