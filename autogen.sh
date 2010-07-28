#!/bin/sh
# Run this to generate all the initial makefiles, etc.

srcdir=`dirname $0`
test -z "$srcdir" && srcdir=.
ACLOCAL_FLAGS="$ACLOCAL_FLAGS -I m4"

DIE=0

(test -f $srcdir/configure.ac) || {
    echo -n "**Error**: Directory "\`$srcdir\'" does not look like the"
    echo " top-level package directory"
    exit 1
}

(autoconf --version) < /dev/null > /dev/null 2>&1 || {
  echo
  echo "**Error**: You must have \`autoconf' installed."
  echo "Download the appropriate package for your distribution,"
  echo "or get the source tarball at ftp://ftp.gnu.org/pub/gnu/"
  DIE=1
}

(grep -e "^AM_PROG_LIBTOOL" -e "^LT_INIT" $srcdir/configure.ac >/dev/null) && {
  (libtoolize --version) < /dev/null > /dev/null 2>&1 \
	  && LIBTOOLIZE=libtoolize || {
	(glibtoolize --version) < /dev/null > /dev/null 2>&1 \
		&& LIBTOOLIZE=glibtoolize || {
      echo
      echo "**Error**: You must have \`libtool' installed."
      echo "You can get it from: ftp://ftp.gnu.org/pub/gnu/"
      DIE=1
    }
  }
}

(automake --version) < /dev/null > /dev/null 2>&1 || {
  echo
  echo "**Error**: You must have \`automake' installed."
  echo "You can get it from: ftp://ftp.gnu.org/pub/gnu/"
  DIE=1
  NO_AUTOMAKE=yes
}

(git --version) < /dev/null > /dev/null 2>&1 || {
  NO_GIT=yes
}


# if no automake, don't bother testing for aclocal
test -n "$NO_AUTOMAKE" || (aclocal --version) < /dev/null > /dev/null 2>&1 || {
  echo
  echo "**Error**: Missing \`aclocal'.  The version of \`automake'"
  echo "installed doesn't appear recent enough."
  echo "You can get automake from ftp://ftp.gnu.org/pub/gnu/"
  DIE=1
}

if test "$DIE" -eq 1; then
  exit 1
fi

if test -z "$*"; then
  echo "**Warning**: I am going to run \`configure' with no arguments."
  echo "If you wish to pass any to it, please specify them on the"
  echo \`$0\'" command line."
  echo
fi

case $CC in
xlc )
  am_opt=--include-deps;;
esac

for coin in `find $srcdir -name configure.ac -print`
do 
  dr=`dirname $coin`
  if test -f $dr/NO-AUTO-GEN; then
    echo skipping $dr -- flagged as no auto-gen
  else
    echo processing $dr
    ( cd $dr

      aclocalinclude="$ACLOCAL_FLAGS"

      if grep "^AM_GLIB_GNU_GETTEXT" configure.ac >/dev/null; then
	echo "Creating $dr/aclocal.m4 ..."
	test -r $dr/aclocal.m4 || touch $dr/aclocal.m4
	echo "Running glib-gettextize...  Ignore non-fatal messages."
	echo "no" | glib-gettextize --force --copy
	echo "Making $dr/aclocal.m4 writable ..."
	test -r $dr/aclocal.m4 && chmod u+w $dr/aclocal.m4
      fi
      if grep "^AC_PROG_INTLTOOL" configure.ac >/dev/null; then
        echo "Running intltoolize..."
	intltoolize --copy --force --automake
      fi
      if grep -e "^AM_PROG_LIBTOOL" -e "^LT_INIT" configure.ac >/dev/null; then
	if test -z "$NO_LIBTOOLIZE" ; then 
	  echo "Running libtoolize..."
	  $LIBTOOLIZE --force --copy
	fi
      fi
      echo "Running aclocal $aclocalinclude ..."
      aclocal $aclocalinclude
      if grep -e "^AM_CONFIG_HEADER" -e "^AC_CONFIG_HEADERS" configure.ac \
           >/dev/null; then
	echo "Running autoheader..."
	autoheader
      fi
      if test -z "$NO_GIT" && git branch; then
          echo "Generating ChangeLog using git ..."
          git log >ChangeLog
      else
          echo "No git, generating stub ChangeLog ..."
          touch ChangeLog
      fi
      echo "Running automake --gnu $am_opt ..."
      automake --add-missing --gnu $am_opt
      echo "Running autoconf ..."
      autoconf
    )
  fi
done

conf_flags=--enable-maintainer-mode

if test x$NOCONFIGURE = x; then
  echo Running $srcdir/configure $conf_flags "$@" ...
  $srcdir/configure $conf_flags "$@" \
  && echo Now type \`make\' to compile. || exit 1
else
  echo Skipping configure process.
fi
