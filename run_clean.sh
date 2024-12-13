fusermount -u /tmp/st1005/mountdir
rm DISKFILE
make clean
make
./rufs -s /tmp/rr1185/mountdir -d
