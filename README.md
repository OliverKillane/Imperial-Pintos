# PintOS-Group-16
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

### Running Tests
Each task has tests runnable from the task's respective directory. We use the `-j` flag to allow make to run in parallel.
```
# To run Task 0 - Alarm Clock
cd src/devices && make check -j

# To run Task 1 - Scheduling
cd src/threads && make check -j

# To run Task 2 - User programs
cd src/userprog && make check -j

# To run Task 3 - Virtual Memory
cd src/vm && make check -j
```

## Authors
This project was completed by Bartłomiej Cieślar, Jordan Hall, Oliver Killane and Robert Buxton from the 11th of October to the 10th of December.
