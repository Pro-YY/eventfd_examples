find_package(Threads)
add_executable(eventfd_worker eventfd_worker.c ${SOURCES})
target_link_libraries(eventfd_worker ${CMAKE_THREAD_LIBS_INIT})
