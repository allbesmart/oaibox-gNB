#find_package(Boost REQUIRED)
find_package(UHD REQUIRED UHD)

add_library(oai_usrpdevif MODULE usrp_lib.cpp usrp_fbs.cpp)
#target_include_directories(oai_usrpdevif PRIVATE Boost::boost)
target_link_libraries(oai_usrpdevif PRIVATE ${UHD_LIBRARIES})
target_link_libraries(oai_usrpdevif PRIVATE ${UHD_INCLUDE_DIRS} /home/user/openairinterface5g/radio/USRP/libtlkcore_lib.so)
target_link_libraries(oai_usrpdevif PRIVATE UTIL)
set_target_properties(oai_usrpdevif PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
add_custom_command(TARGET oai_usrpdevif POST_BUILD
                   COMMAND ${CMAKE_COMMAND} -E create_symlink liboai_usrpdevif.so liboai_device.so
                   WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
