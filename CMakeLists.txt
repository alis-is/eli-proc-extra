project (eli_proc_extra)

file(GLOB eli_proc_extra_sources ./src/**.c)
set(eli_proc_extra ${eli_proc_extra_sources})

add_library(eli_proc_extra ${eli_proc_extra})
target_link_libraries(eli_proc_extra)