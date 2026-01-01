# OSLAB4-File-System-Implementation
Using C to implement a file system

```
make
sudo insmod osfs.ko
sudo dmesg
mkdir mnt
sudo mount -t osfs none mnt/
sudo dmesg
cd mnt
sudo touch test1.txt
sudo bash -c "echo 'I LOVE OSLAB' > test1.txt"
```

bonus:
```
sudo dd if=/dev/zero of=bigfile bs=1024 count=8
ls -l bigfile
```

finish:
```
cd ..
sudo umount mnt
sudo rmmod osfs
make clean
```
