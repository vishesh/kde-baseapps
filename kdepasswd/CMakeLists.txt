add_subdirectory(kcm)

set(kdepasswd_SRCS
    kdepasswd.cpp
    passwd.cpp
    passwddlg.cpp)

kde4_add_executable(kdepasswd ${kdepasswd_SRCS})

target_link_libraries(kdepasswd ${KDE4_KIO_LIBS} ${KDE4_KDESU_LIBS})

install(TARGETS kdepasswd ${INSTALL_TARGETS_DEFAULT_ARGS})
install(PROGRAMS kdepasswd.desktop DESTINATION ${XDG_APPS_INSTALL_DIR})
