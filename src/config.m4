PHP_ARG_ENABLE(fufake, whether to enable fufake support,
[  --enable-fufake           Enable fufake support])

if test "$PHP_FUFAKE" != "no"; then
  PHP_NEW_EXTENSION(fufake, fufake.c, $ext_shared)
fi
