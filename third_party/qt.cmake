# qt.cmake

# Instruct CMake to run moc, uic, and rcc automatically for Qt
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Try to find an existing system installation of Qt6 (Widgets is all a tray app needs)
find_package(Qt6 COMPONENTS Widgets QUIET)

if(NOT Qt6_FOUND)
    message(WARNING "Qt6 Widgets not found! Attempting to locate Qt5 fallback...")
    find_package(Qt5 COMPONENTS Widgets QUIET)
endif()

# If neither Qt6 nor Qt5 is found, prompt the user with simple install commands
if(NOT Qt6_FOUND AND NOT Qt5_FOUND)
    if(WIN32)
        message(FATAL_ERROR
            "\n========================================================================\n"
            " Qt6/Qt5 development libraries not found!\n"
            " Please install Qt using one of these simple methods:\n\n"
            " 1. Via winget:  winget install Qt.QtCreator\n"
            " 2. Via vcpkg:   vcpkg install qtbase\n"
            " 3. Or download the official installer: https://www.qt.io/download\n"
            "========================================================================\n"
        )
    else() # Linux / macOS
        message(FATAL_ERROR
            "\n========================================================================\n"
            " Qt6/Qt5 development libraries not found!\n"
            " Please install them using your package manager:\n\n"
            " Ubuntu/Debian:  sudo apt install qt6-base-dev\n"
            " Fedora:         sudo dnf install qt6-qtbase-devel\n"
            " Arch Linux:     sudo pacman -S qt6-base\n"
            " macOS (Homebrew): brew install qt\n"
            "========================================================================\n"
        )
    endif()
endif()

# Create a helper variable for easy linking in the main CMake file
if(Qt6_FOUND)
    set(QT_LIBRARIES Qt6::Widgets)
else()
    set(QT_LIBRARIES Qt5::Widgets)
endif()
