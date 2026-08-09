cmake_minimum_required(VERSION 3.20)
project(KajsShooterGame3D CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(RAYLIB_DIR ${CMAKE_SOURCE_DIR}/vendor/raylib-6.0_win64_mingw-w64)

add_executable(kaj_shooter
  src/main.cpp
  src/assets.cpp
  src/world.cpp
  src/weapons.cpp
  src/fx.cpp
  src/sky.cpp
  src/player.cpp
  src/render.cpp
  src/vehicle.cpp
  src/story.cpp
  src/net.cpp
  src/game.cpp
)

target_include_directories(kaj_shooter PRIVATE ${RAYLIB_DIR}/include src)
target_link_libraries(kaj_shooter PRIVATE
  ${RAYLIB_DIR}/lib/libraylib.a
  opengl32 gdi32 winmm ws2_32
)

if(MINGW)
  target_link_options(kaj_shooter PRIVATE -static-libgcc -static-libstdc++)
endif()
