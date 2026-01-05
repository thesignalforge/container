dnl
dnl Signalforge Container Extension
dnl config.m4 - Build configuration
dnl
dnl Copyright (c) 2024 Signalforge
dnl License: MIT
dnl

PHP_ARG_ENABLE([signalforge_container],
  [whether to enable signalforge_container support],
  [AS_HELP_STRING([--enable-signalforge-container],
    [Enable signalforge_container support])],
  [no])

if test "$PHP_SIGNALFORGE_CONTAINER" != "no"; then

  dnl Check for required headers
  AC_CHECK_HEADERS([stdint.h], [], [
    AC_MSG_ERROR([stdint.h not found])
  ])

  dnl Source files
  PHP_NEW_EXTENSION(signalforge_container,
    signalforge_container.c \
    src/container.c \
    src/binding.c \
    src/autowire.c \
    src/reflection_cache.c \
    src/factory.c \
    src/compiler.c \
    src/pool.c \
    src/fast_lookup.c \
    src/cache_file.c,
    $ext_shared,, -DZEND_ENABLE_STATIC_TSRMLS_CACHE=1)

  dnl Add header files
  PHP_ADD_BUILD_DIR($ext_builddir)
  PHP_ADD_BUILD_DIR($ext_builddir/src)
  PHP_ADD_INCLUDE($ext_srcdir)
  PHP_ADD_INCLUDE($ext_srcdir/src)

  dnl Install headers for potential use by other extensions
  PHP_INSTALL_HEADERS([ext/signalforge_container], [php_signalforge_container.h src/container.h src/binding.h src/autowire.h src/reflection_cache.h src/factory.h src/compiler.h src/simd.h src/pool.h src/fast_lookup.h src/cache_file.h])

fi

