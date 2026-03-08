#!/bin/bash
export ZENOS=~/OSDev/ZenOS
export CROSS=$ZENOS/userland/newlib/cross/bin
export PATH=$CROSS:$PATH
export SYSROOT=$ZENOS/userland/sysroot
cd $ZENOS/userland/newlib/build-zenos
rm -f config.cache

../newlib-cygwin/configure \
  --target=x86_64-zenos \
  --host=x86_64-pc-linux-gnu \
  --prefix=$SYSROOT \
  --disable-newlib-supplied-syscalls \
  --enable-newlib-io-long-long \
  --enable-newlib-io-c99-formats \
  --enable-newlib-multithread \
  --disable-shared

make -j$(nproc)
make install

# Compile locks.c and add it to libc.a
$CROSS/x86_64-zenos-gcc -c \
  -I$SYSROOT/x86_64-zenos/include \
  -D_RETARGETABLE_LOCKING \
  -O2 \
  $ZENOS/userland/newlib/newlib-cygwin/newlib/libc/sys/zenos/locks.c \
  -o /tmp/locks.o
$CROSS/x86_64-zenos-ar r $SYSROOT/x86_64-zenos/lib/libc.a /tmp/locks.o

cp $SYSROOT/x86_64-zenos/lib/libc.a   $ZENOS/userland/libs/
cp $SYSROOT/x86_64-zenos/lib/libg.a   $ZENOS/userland/libs/
cp $SYSROOT/x86_64-zenos/lib/libm.a   $ZENOS/userland/libs/
cp $SYSROOT/x86_64-zenos/lib/libnosys.a $ZENOS/userland/libs/
cp -r $SYSROOT/x86_64-zenos/include/* $ZENOS/userland/libs/include/
