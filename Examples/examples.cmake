add_executable(Echo Examples/Echo.cpp)
target_link_libraries(Echo PUBLIC AsioContext)

add_executable(Explorer Examples/DriverExplorer.cpp)
target_link_libraries(Explorer PUBLIC AsioContext)

add_executable(LoadPlugin Examples/LoadPlugin.cpp)
target_link_libraries(LoadPlugin PUBLIC Vst2Effect)