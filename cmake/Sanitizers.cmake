option(MYCHAT_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)

function(mychat_enable_sanitizers target)
    if(MYCHAT_SANITIZERS)
        target_compile_options(${target} PRIVATE
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
        )
        target_link_options(${target} PRIVATE
            -fsanitize=address,undefined
        )
    endif()
endfunction()
