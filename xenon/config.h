// Heretic 360: the build settings Crispy Doom's CMake would normally generate from cmake/config.h.cin.

#ifndef HERETIC360_CONFIG_H
#define HERETIC360_CONFIG_H

#define PACKAGE_NAME "Crispy Doom"
#define PACKAGE_TARNAME "crispy-doom"
#define PACKAGE_VERSION "7.2"
#define PACKAGE_STRING "Heretic 360 0.8.1 (Crispy Doom 7.2)"
#define PROGRAM_PREFIX "crispy-"

// newlib has <dirent.h> and declares strcasecmp and strncasecmp in <strings.h>.
#define HAVE_DIRENT_H
#define HAVE_DECL_STRCASECMP 1
#define HAVE_DECL_STRNCASECMP 1

#endif
