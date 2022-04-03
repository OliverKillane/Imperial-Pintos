# Imperial-Pintos
An implementation of the Imperial PintOS coursework tasks 0-3 with (design documents for tasks 1-3).

## Background
The Pintos Coursework Project was originally created by Stanford as the successor to NACHOS (Not Another Completely Heuristric Operating System). Stanford Pintos can be found 
[here](https://www.scs.stanford.edu/21wi-cs140/pintos/pintos.html). 

Imperial College have since heavily adapted pintos, and it is run as a 9 week, 4 member, group project. The full Imperial Pintos guide can be found [here](https://www.doc.ic.ac.uk/~mjw03/OSLab/pintos.pdf).

The pintos project is designed to help students learn operating systems development by implementing some major kernel components, including process/thread scheduling, syscalls, user processes and virtual memory. 
While maintaining high standards of code quality, and designing to prevent concurrency related bugs (a major part of the design).

## Imperial Student Usage
Pintos beans are best served warm, plagarism from this repo will result in a cold reception by markers. Imperial are (*rightly*) meticulous with their checks. 
Even if this is missed, this repo contains some added bugs to prevent copy & paste idiocy. 

That said, there is nothing against using this repo for inspiration & general direction, just ensure you correctly cite sources in your peliminaries for reports.

## Design Documentation
We have included the `tex` for our design documents. Note that these reference code directly from inside the repo so must be compiled in-place.
![Untitled](https://user-images.githubusercontent.com/44177991/161397254-47ae48d4-c15a-464f-bbd3-5196def780bf.png)

## Get Started
### Prerequisites
- `qemu-system-i386` (can be installed with `qemu-system`) (used to emulate i386-based IBM PC clone hardware)
- `perl` (used for matching test outputs, for our linter)
- `gcc`
- GNU Binutils (needed as compilation makes use of several utilities)
- `make` (used for build and tests)

### Running Pintos
1. To create a simulated disk, use the command
```bash
pintos-mkdisk filesys.dsk --filesys-size=2 # create a 2MB disk
```

2. To format the disk (format the pintos disk and then quit)
```bash
pintos -f -q
``` 

3. To copy files onto the simulated disk
```bash
pintos -p hostfilename -a pintosfilename -- -q # copy file 'hostfilename' into the simulated disk, with the name 'pintosfilename'
pintos -g hostfilename -a pintosfilename -- -q # copy file 'pintosfilename' from the simulated disk, to the host with the name 'hostfilename'
```
Once compiled with GCC, elf binaries can be loaded to be run on pintos, examples are contained in `src/examples`

4. Run pintos with the `pintos` command (use the `-q` flag to allow the terminal to resume control once pintos exits)
```bash
Command line syntax: [OPTION...] [ACTION...]
Options must precede actions.
Actions are executed in the order specified.

Available actions:
  run 'PROG [ARG...]' Run PROG and wait for it to complete. 
  ls                 List files in the root directory.      
  cat FILE           Print FILE to the console.
  rm FILE            Delete FILE.
Use these actions indirectly via `pintos' -g and -p options:
  extract            Untar from scratch device into file system.
  append FILE        Append FILE to tar file on scratch device. 

Options:
  -h                 Print this help message and power off.     
  -q                 Power off VM after actions or on panic.
  -r                 Reboot after actions.
  -f                 Format file system device during startup.
  -filesys=BDEV      Use BDEV for file system instead of default.
  -scratch=BDEV      Use BDEV for scratch instead of default.
  -swap=BDEV         Use BDEV for swap instead of default.
  -rs=SEED           Set random number seed to SEED.
  -mlfqs             Use multi-level feedback queue scheduler.
  -ul=COUNT          Limit user memory to COUNT pages.
```
5. We can then use `pintos run "command"` to run the pintos command line, including executing binaries we have already loaded.

### Basic Example (`src/userprog`)
For example
```bash
cd src/examples
make echo
cd ../userprog

# now in the userprog directory
make
pintos-mkdisk filesys.dsk --filesys-size=2
pintos -p ../examples/echo -a echo -- -f -q run 'echo "hello world!"'
```
To get output
```
SeaBIOS (version 1.13.0-1ubuntu1.1)
Booting from Hard Disk...
PPiiLLoo  hhddaa11

LLooaaddiinngg.......................
Kernel command line: -f -q extract run 'echo "hello world!"'
Pintos booting with 3,968 kB RAM...
367 pages available in kernel pool.
367 pages available in user pool.
SeaBIOS (version 1.13.0-1ubuntu1.1).1
Booting from Hard Disk...
PPiiLLoo  hhddaa1
1
LLooaaddiinngg.......................
Kernel command line: -f -q extract run 'echo "hello world!"'
Pintos booting with 3,968 kB RAM...
367 pages available in kernel pool.
367 pages available in user pool.
Calibrating timer...  431,718,400 loops/s.
hda: 1,008 sectors (504 kB), model "QM00001", serial "QEMU HARDDISK"
hda1: 206 sectors (103 kB), Pintos OS kernel (20)
hda2: 93 sectors (46 kB), Pintos scratch (22)
hdb: 5,040 sectors (2 MB), model "QM00002", serial "QEMU HARDDISK"  
hdb1: 4,096 sectors (2 MB), Pintos file system (21)
filesys: using hdb1
scratch: using hda2
Formatting file system...done.
Boot complete.
Extracting ustar archive from scratch device into file system...
Putting 'echo' into the file system...
Erasing ustar archive...
Executing 'echo "hello world!"':
"hello world!" 
echo: exit(0)
Execution of 'echo "hello world!"' complete.
Timer: 77 ticks
Thread: 39 idle ticks, 37 kernel ticks, 1 user ticks
hdb1 (filesys): 63 reads, 190 writes
hda2 (scratch): 92 reads, 2 writes
Console: 958 characters output
Keyboard: 0 keys pressed
Exception: 0 page faults
Powering off...
```

### Basic Example (`src/vm`)
Using the virtual memory task is slightly different as a swap partition is required. The `filesysgen` script is used to create a disk with a swap partition.
```bash
cd src/examples
make echo
cd ../vm

# now in the vm directory
make
./filesysgen
pintos -p ../examples/echo -a echo -- -f -q run 'echo "hello world!"'
```

To get output (notice pintos recognises the existence of a swap partition in the simulated drive)

```
SeaBIOS (version 1.13.0-1ubuntu1.1)
Booting from Hard Disk...
PPiiLLoo  hhddaa11

LLooaaddiinngg.......................
Kernel command line: -f -q extract run 'echo "hello world!"'
Pintos booting with 3,968 kB RAM...
367 pages available in kernel pool.
367 pages available in user pool.
SeaBIOS (version 1.13.0-1ubuntu1.1).1
Booting from Hard Disk...
PPiiLLoo  hhddaa1
1
LLooaaddiinngg.......................
Kernel command line: -f -q extract run 'echo "hello world!"'
Pintos booting with 3,968 kB RAM...
367 pages available in kernel pool.
367 pages available in user pool.
Calibrating timer...  431,718,400 loops/s.
hda: 1,008 sectors (504 kB), model "QM00001", serial "QEMU HARDDISK"
hda1: 206 sectors (103 kB), Pintos OS kernel (20)
hda2: 93 sectors (46 kB), Pintos scratch (22)
hdb: 5,040 sectors (2 MB), model "QM00002", serial "QEMU HARDDISK"  
hdb1: 4,096 sectors (2 MB), Pintos file system (21)
filesys: using hdb1
scratch: using hda2
Formatting file system...done.
Boot complete.
Extracting ustar archive from scratch device into file system...
Putting 'echo' into the file system...
Erasing ustar archive...
Executing 'echo "hello world!"':
"hello world!" 
SeaBIOS (version 1.13.0-1ubuntu1.1)1
Booting from Hard Disk...
PPiiLLoo  hhddaa1
1
LLooaaddiinngg...........................
Kernel command line: -f -q extract run 'echo "hello world!"'
Pintos booting with 3,968 kB RAM...
367 pages available in kernel pool.
367 pages available in user pool.  
Calibrating timer...  523,468,800 loops/s.
hda: 1,008 sectors (504 kB), model "QM00001", serial "QEMU HARDDISK"
hda1: 225 sectors (112 kB), Pintos OS kernel (20)
hda2: 93 sectors (46 kB), Pintos scratch (22)
hdb: 9,072 sectors (4 MB), model "QM00002", serial "QEMU HARDDISK"  
hdb1: 8,192 sectors (4 MB), Pintos file system (21)
hdc: 263,088 sectors (128 MB), model "QM00003", serial "QEMU HARDDISK"
hdc1: 262,144 sectors (128 MB), Pintos swap (23)
filesys: using hdb1
scratch: using hda2
swap: using hdc1
Formatting file system...done.
Boot complete.
Extracting ustar archive from scratch device into file system...
Putting 'echo' into the file system...
Erasing ustar archive...
Executing 'echo "hello world!"':
"hello world!" 
echo: exit(0)
Execution of 'echo "hello world!"' complete.
Timer: 76 ticks
Thread: 42 idle ticks, 33 kernel ticks, 1 user ticks
hdb1 (filesys): 64 reads, 195 writes
hda2 (scratch): 92 reads, 2 writes
hdc1 (swap): 0 reads, 0 writes
Console: 1126 characters output
Keyboard: 0 keys pressed
Exception: 3 page faults
Powering off...
```

### Running Tests
Each task has tests runnable from the task's respective directory. We use the `-j` flag to allow make to run in parallel.
```bash
# To run Task 0 - Alarm Clock
cd src/devices && make check -j

# To run Task 1 - Scheduling
cd src/threads && make check -j

# To run Task 2 - User Programs
cd src/userprog && make check -j

# To run Task 3 - Virtual Memory
cd src/vm && make check -j
```

## CI
The continuous integration for this repository is for GitLab (where the repo was hosted during development).
It contains a basic pipeline for building and running the unit tests within a docker build specifically for the project, as well as linting.

The CI is restricted to merge requests as running the tests is quite intensive.

Linting uses the same style as the linux kernel, and is only applied to files changed from the original provided files. This is to maintain consistent formatting, but prevent the diff being overwhelming for markers (as would be the case if all files, including those untouched were updated to conform to the style).

## Authors
This project was completed by Bartłomiej Cieślar, Jordan Hall, Oliver Killane and Robert Buxton from the 11th of October to the 10th of December.
