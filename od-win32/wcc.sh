#! /bin/sh
#
# Somewhat horrible shell script to convert GCC style options into Watcom
# style.
#
# I put this into the public domain - if you think you can use it for
# anything, use it.
#
# Written 1998 Bernd Schmidt

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

gcc_wsysinc1=`echo $INCLUDE | sed 's,;, ,g' | sed 's,.:,;\\0,g' |sed -e 's,;,//,g' -e 's,:,/,g' -e 's,\\\\,/,g'`
for foo in $gcc_wsysinc1; do
  gcc_wsysinc2="$gcc_wsysinc2 -I$foo"
done
gcc_spcdefs="-undef -U__GNUC__ -U__GNUC_MINOR__ -D_WIN32 -D__WATCOMC__ -D_STDCALL_SUPPORTED=1 -D__X86__ -D__386__=1 -DM_I386=1 -D_M_I386 -D_M_IX86=500 -D__NT__=1"

mode=link
options=
srcfiles=
asmfiles=
objfiles=
resfiles=
outputfile=
libraries=
gnudefines=
wccdefines=
includes=
gnuincludes=

next_is_output=no
next_is_include=no

for arg in $@; do

if test $next_is_output = yes; then
  outputfile=$arg
  next_is_output=no
else if test $next_is_include = yes; then
  includes="$includes $arg"
  gnuincludes="$gnuincludes -I$arg"
  next_is_include=no
else
  case $arg in
# Passing /xxx options directly may be too risky - they could be confused for
# file names - so use this escape.
  --/--*)
    options="$options `echo $arg |sed s,^--/--,/,`"
    ;;

  -I) next_is_include=yes
    ;;

  -I*)
    includes="$includes `echo $arg | sed 's,^-I,,'`"
    gnuincludes="$gnuincludes $arg"
    ;;

  -D*=*)
    gnudefines="$gnudefines $arg"
    wccdefines="$wccdefines `echo $arg |sed 's,^-D,/d,'`"
    ;;

  -D*)
    gnudefines="$gnudefines $arg"
    wccdefines="$wccdefines `echo $arg |sed 's,^-D,/d,'`="
    ;;

  -c) if [ "$mode" != "link" ]; then
        echo "Bad argument"
        exit 10
      fi
      mode=compile
    ;;

  -S) if [ "$mode" != "link" ]; then
        echo "Bad argument"
        exit 10
      fi
      mode=assemble
    ;;

  -E) if [ "$mode" != "link" ]; then
        echo "Bad argument"
        exit 10
      fi
      mode=preprocess
    ;;

  -g) options="$options /d2"
    ;;

  -o) next_is_output=yes
    if [ "x$outputfile" != "x" ]; then
      echo "Multiple output files!"
      exit 10
    fi
    ;;

  -l*) libraries="$libraries `echo $arg | sed 's,^-l,,'`"
    ;;

  *.c)
    srcfiles="$srcfiles $arg"
    ;;

  *.S)
    asmfiles="$asmfiles $arg"
    ;;
    
  *.res)
    realfilename $arg
    resfiles="$resfiles $result"
    ;;

  *.o)
    realfilename $arg
    objfiles="$objfiles $result"
    ;;

  *) echo "Bad argument: $arg"
    ;;
  esac
fi
fi
done

#echo "Source files: $srcfiles"
#echo "Object files: $objfiles"
#echo "Output files: $outputfile"
#echo "Libraries: $libraries"
echo "Mode: $mode"
#echo "Options: $options"

if [ "$mode" != "link" -a "x$libraries" != "x" ]; then
  echo "Libraries specified in non-link mode!"
  exit 10
fi

prefiles=
srccount=0
for foo in $srcfiles; do
  bar=wccsh-tmppre$srccount.i
  prefiles="$prefiles $bar"
  echo "gcc -E -nostdinc $gnuincludes $gcc_wsysinc2 $gcc_spcdefs $gnudefines $foo -o $bar"
  if gcc -E -nostdinc $gnuincludes $gcc_wsysinc2 $gcc_spcdefs $gnudefines $foo -o $bar; then

  else
    exit 10
  fi
  if [ ! -f $bar ]; then
    exit 10
  fi
  srccount=`expr $srccount + 1`
done

tmpobjs=
if test $mode = compile -o $mode = link; then
  tmpcnt=0
  for foo in $asmfiles; do
    bar=wccsh-tmpobj$tmpcnt.o
    tmpcnt=`expr $tmpcnt + 1`
    tmpobjs="$tmpobjs $bar"
    echo "gcc -c $foo -o $bar"
    gcc -c $foo -o $bar
    srccount=`expr $srccount + 1`
  done
  for foo in $prefiles; do
    bar=wccsh-tmpobj$tmpcnt.o
    tmpcnt=`expr $tmpcnt + 1`
    tmpobjs="$tmpobjs $bar"
    sed -e '/^# [0123456789]*/s,^# ,#line ,' -e '/^#line/s,"[^"]*$,",' <$foo> wccsh_tmpsrc.c
    echo "wcc386 $foo $options /fo=$bar"
    wcc386 wccsh_tmpsrc.c $options /fo=wcc_tmp.o >&2
    mv -f wcc_tmp.o $bar
    if [ ! -f $bar ]; then
      rm -f $prefiles $tmpobjs
      exit 10
    fi
  done
fi

case $mode in
  preprocess)
    for foo in $prefiles; do
      if [ "x$outputfile" = "x" ]; then
        cat $foo
      else
        mv -f $foo $outputfile
      fi
    done
    ;;

  compile)
    if [ "$srccount" != "1" -a "x$outputfile" != "x" ]; then
      echo "cannot specify -o and -c with multiple compilations" >&2
      exit 10
    fi
    if [ "x$outputfile" != "x" ]; then
      echo Moving "'" $tmpobjs "'" to "'" $outputfile "'"
      if mv $tmpobjs $outputfile 2>/dev/null; then
      fi
    fi
    ;;

  assemble)
    ;;

  link)
    if [ "x$outputfile" = "x" ]; then
      outputfile=a.out
    fi
    FILES=
    for foo in $objfiles $tmpobjs; do
      if [ "x$FILES" = "x" ]; then
        FILES=$foo
      else
        FILES="$FILES,$foo"
      fi
    done
    for foo in $resfiles; do
      FILES="$FILES op resource=$foo"
    done
    for foo in $libraries; do
      FILES="$FILES LIB $foo"
    done
#    echo Files: $FILES
#    echo "wlink SYSTEM nt FIL $FILES NAME $outputfile"
    wlink SYSTEM nt FIL $FILES NAME $outputfile >wccsh-linkerror.log
    if grep "cannot open.*No such file or" wccsh-linkerror.log; then
#      rm wccsh-linkerror.log
      rm $outputfile
      exit 10
    fi
#    rm wccsh-linkerror.log
    if [ ! -f $outputfile ]; then
      rm -f $prefiles $tmpobjs
      exit 10
    fi
    ;;
esac

rm -f $prefiles $tmpobjs
exit 0
