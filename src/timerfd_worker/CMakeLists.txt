find_package(Threads)
add_executable(timerfd_worker timerfd_worker.c ${SOURCES})
target_link_libraries(timerfd_worker ${CMAKE_THREAD_LIBS_INIT})
