#! /bin/sh
#

realdirname () {
  tmp_save_dir=`pwd`
  cd $@
  result=`pwd -P`
  cd $tmp_save_dir
  result=`echo $result |sed 's,/,\\\\,g'`
}

realfilename () {
  case $@ in
  */*)
    tmp_save_dir=`pwd`
    cd `echo $@ | sed 's,/[^/]*$,,'`
    result=`pwd -P`/`basename $@`
    cd $tmp_save_dir
    ;;
  *)
    result=$@
    ;;
  esac
  result=`echo $result |sed 's,/,\\\\,g'`
}

next_is_include=no
resfiles=
srcfiles=
exefile=

for arg in $@; do

if test $next_is_include = yes; then
  realdirname $arg
  includes="$includes /i=$result"
  next_is_include=no
else
  case $arg in
  -I) next_is_include=yes
    ;;

  -I*)
    foo=`echo $arg | sed 's,^-I,,'`
    realdirname $foo
    includes="$includes /i=$result"
    ;;

  *.rc)
    realfilename $arg
    srcfiles="$srcfiles $result"
    ;;

  *.res)
    realfilename $arg
    resfiles="$resfiles $result"
    ;;

  *.exe)
    realfilename $arg
    exefile=$result
    ;;

  *) echo "Bad argument: $arg"
    ;;
  esac
fi
done

echo "wrc /bt=nt /dWIN32 /d_WIN32 /d__NT__ /r $includes $srcfiles"
if test -z "$exefile"; then
  wrc /bt=nt /dWIN32 /d_WIN32 /d__NT__ /r $includes $srcfiles
else
  wrc $resfiles $exefile
fi
