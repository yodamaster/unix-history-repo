#!/bin/sh
# $FreeBSD$

desc="write access to subdirectory is required to move it to another directory"

dir=`dirname $0`
. ${dir}/../misc.sh

echo "1..13"

n0=`namegen`
n1=`namegen`
n2=`namegen`
n3=`namegen`

expect 0 mkdir ${n2} 0777
expect 0 mkdir ${n3} 0777
cdir=`pwd`

# Check that write permission on containing directory (${n2}) is enough
# to rename subdirectory (${n0}).
expect 0 mkdir ${n2}/${n0} 0700
expect 0 -u  65534 -g 65534 rename ${n2}/${n0} ${n2}/${n1}

# Check that write permission on containing directory (${n2}) is not enough
# to move subdirectory (${n0}) from that directory.
expect EACCES -u  65534 -g 65534 rename ${n2}/${n1} ${n3}/${n0}

expect 0 rmdir ${n2}/${n1}
expect ENOENT rmdir ${n2}/${n1}

# Check that write permission on containing directory (${n2}) is enough
# to move file (${n0}) from that directory.
expect 0 create ${n2}/${n0} 0755
expect 0 -u  65534 -g 65534 rename ${n2}/${n0} ${n3}/${n0}

expect 0 unlink ${n3}/${n0}
expect ENOENT unlink ${n2}/${n0}

expect 0 rmdir ${n3}
expect 0 rmdir ${n2}

