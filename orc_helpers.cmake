include_guard(DIRECTORY)

# Helper function to link a target through orc
# ORC is specified as a dependency of the target so it is built and ready
# to use at link-time. Note that the configuration is linked between debug/release of ORC and the
# target (e.g., the Debug ORC will test Debug ${target}, and so for Release.)
function(link_via_orc target)
    add_dependencies(${target} orc_orc)
    set_target_properties(${target}
                          PROPERTIES
                          XCODE_ATTRIBUTE_ALTERNATE_LINKER "$<TARGET_FILE:orc_orc>"
                          XCODE_ATTRIBUTE_LIBTOOL "$<TARGET_FILE:orc_orc>")
    set_target_properties(${target} PROPERTIES XCODE_GENERATE_SCHEME ON)
endfunction()
