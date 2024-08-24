# sdat2img
Convert sparse Android data image (.dat) into filesystem ext4 image (.img)

This is a C++ equivalent of the original sdat2img tool, which was originally written in Python by xpirt, luxi78, and howellzhu.

## Requirements
This project uses `libbrotli` to enable inline brotli decompression. Please ensure that `libbrotli-dev` is installed on your system to build the project (Recommended).

## Build
Quite straightforward as it uses CMake.

## Usage
```
./sdat2img <transfer_list> <system_new_file> [system_img]
```
- `<transfer_list>` = input, system.transfer.list from rom zip
- `<system_new_file>` = input, system.new.dat from rom zip
- `[system_img]` = output ext4 raw image file

Or for lazy people:
```
./sdat2img <directory/to/extracted> <partition_name> [out_filename.img]
```
- `<directory/to/extracted>` = if you extract ROM zip to a folder, point to the directory
- `<partition_name>` = Like system, vendor, etc...
- `[out_filename.img]` = Optional output path of ext4 RAW image

The program guesses the file names from the supplied directory and acts same as the first usage.

## Example
This is a simple example on a Linux system: 
```
~$ ./sdat2img vendor.transfer.list vendor.new.dat vendor.img
```

- OR

```
~$ ./sdat2img LineageExtracted/ system
```
Where LineageExtracted/ contains `system.new.dat(.br)` `system.transfer.list` and it would output to LineageExtracted/system.img (as inferred)

## Performance and comparison with Python implementation
- Python: 150 lines
- C++: 530 lines

C++ (Around x3.5 faster):
```
real    0m1.278s
user    0m0.086s
sys     0m1.102s
```

Python:
```
real    0m4.366s
user    0m0.871s
sys     0m2.336s
```
