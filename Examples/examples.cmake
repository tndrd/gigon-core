add_executable(Echo Examples/Echo.cpp)
target_link_libraries(Echo PUBLIC AsioContext)

add_executable(Explorer Examples/DriverExplorer.cpp)
target_link_libraries(Explorer PUBLIC AsioContext)
