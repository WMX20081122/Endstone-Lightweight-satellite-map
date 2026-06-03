cmake_minimum_required(VERSION 3.15)

project(endstone_bdslm CXX)
set(BASE_VERSION "1.0.0")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHsc /utf-8 /W3 /wd4996")
endif()

include(FetchContent)

# Endstone API
FetchContent_Declare(
    endstone
    GIT_REPOSITORY https://github.com/EndstoneMC/endstone.git
    GIT_TAG v0.11.4
)
FetchContent_MakeAvailable(endstone)

# nlohmann_json
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0
)
FetchContent_MakeAvailable(nlohmann_json)

# cpp-httplib (header-only HTTP server/client)
FetchContent_Declare(
    cpp-httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.18.3
)
FetchContent_MakeAvailable(cpp-httplib)

# Plugin sources
file(GLOB PLUGIN_SOURCES
    CONFIGURE_DEPENDS
    "src/*.cpp"
)

# Use Endstone's plugin build function (automatically sets PREFIX "endstone_")
endstone_add_plugin(${PROJECT_NAME} ${PLUGIN_SOURCES})

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        nlohmann_json::nlohmann_json
        httplib::httplib
)

target_include_directories(${PROJECT_NAME}
    PRIVATE
        include
)
