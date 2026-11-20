// Heretic 360: the build settings Chocolate Doom's CMake would normally generate from cmake/config.h.cin.

#ifndef HERETIC360_CONFIG_H
#define HERETIC360_CONFIG_H

#define PACKAGE_NAME "Chocolate Doom"
#define PACKAGE_TARNAME "chocolate-doom"
#define PACKAGE_VERSION "3.1.1"
#define PACKAGE_STRING "Heretic 360 0.6 (Chocolate Doom 3.1.1)"
#define PROGRAM_PREFIX "chocolate-"

// newlib has <dirent.h> and declares strcasecmp and strncasecmp in <strings.h>.
#define HAVE_DIRENT_H
#define HAVE_DECL_STRCASECMP 1
#define HAVE_DECL_STRNCASECMP 1

#endif
