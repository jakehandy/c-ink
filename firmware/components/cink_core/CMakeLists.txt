# cink_core: the portable half of C-Ink (digest model, ranking, layout,
# renderer, fonts). Builds as an ESP-IDF component on device and as a plain
# static library for the simulator, tests and the desktop preview.
set(CINK_CORE_SRCS
  src/canvas.cpp src/font.cpp src/icons.cpp src/json.cpp src/model.cpp
  src/rank.cpp src/timefmt.cpp src/ui.cpp src/capi.cpp)

if(ESP_PLATFORM)
  idf_component_register(SRCS ${CINK_CORE_SRCS} INCLUDE_DIRS include)
  target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Wno-unused-parameter)
else()
  add_library(cink_core STATIC ${CINK_CORE_SRCS})
  target_include_directories(cink_core PUBLIC include)
  target_compile_features(cink_core PUBLIC cxx_std_17)
  if(NOT MSVC)
    target_compile_options(cink_core PRIVATE -Wall -Wextra -Wno-unused-parameter)
  endif()
endif()
