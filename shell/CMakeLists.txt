
include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}/..
  ${CMAKE_CURRENT_BINARY_DIR}/../
)

# okular

set(okular_SRCS
   main.cpp
   okular_main.cpp
   shell.cpp
   shellutils.cpp
   welcomescreen.cpp

   ../gui/recentitemsmodel.cpp

   shell.qrc
)

ki18n_wrap_ui(okular_SRCS
    welcomescreen.ui)

file(GLOB ICONS_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/../icons/*-apps-okular.png")
ecm_add_app_icon(okular_SRCS ICONS ${ICONS_SRCS})

add_executable(okular ${okular_SRCS})

target_link_libraries(okular KF6::I18n KF6::Parts KF6::WindowSystem KF6::Crash KF6::IconThemes KF6::KIOGui)
if (TARGET Qt6::DBus)
    target_link_libraries(okular Qt6::DBus)
endif()
if (HAVE_X11)
    target_link_libraries(okular Qt6::GuiPrivate)
endif()

install(TARGETS okular ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

if(APPLE)
    set_target_properties(okular PROPERTIES
        MACOSX_BUNDLE_DISPLAY_NAME "Okular"
        MACOSX_BUNDLE_BUNDLE_NAME "Okular"
        MACOSX_BUNDLE_ICON_FILE "okular_SRCS"
        MACOSX_BUNDLE_LONG_VERSION_STRING "Okular ${RELEASE_SERVICE_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${RELEASE_SERVICE_VERSION_MAJOR}.${RELEASE_SERVICE_VERSION_MINOR}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${RELEASE_SERVICE_VERSION}"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/MacOSXBundleInfo.plist.in"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.kde.okular"
        MACOSX_BUNDLE_COPYRIGHT "2002-2026 The Okular Developers")

    # Bundle a fontconfig configuration so poppler can substitute
    # non-embedded PDF fonts; the fontconfig library's compiled-in config
    # path does not exist outside the build machine (see shell/main.cpp).
    install(FILES mac/fontconfig/fonts.conf
            DESTINATION ${KDE_INSTALL_BUNDLEDIR}/okular.app/Contents/Resources/etc/fonts)
    install(DIRECTORY mac/fontconfig/conf.d
            DESTINATION ${KDE_INSTALL_BUNDLEDIR}/okular.app/Contents/Resources/etc/fonts)
    # Carlito (SIL OFL) is metric-compatible with Calibri; fonts.conf scans
    # Resources/fonts so non-embedded Calibri keeps its glyph spacing even on
    # systems where Apple's document-compatibility fonts are unavailable.
    install(FILES mac/fontconfig/fonts/Carlito.ttc mac/fontconfig/fonts/NOTICE.txt
            DESTINATION ${KDE_INSTALL_BUNDLEDIR}/okular.app/Contents/Resources/fonts)
endif()

# okular shell data files

install( PROGRAMS org.kde.okular.desktop  DESTINATION  ${KDE_INSTALL_APPDIR} )
install( FILES org.kde.okular.appdata.xml DESTINATION  ${KDE_INSTALL_METAINFODIR} )
