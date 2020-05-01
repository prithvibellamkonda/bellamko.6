# Second chance page replacement algorithm

## Operating system
each page - 1K
memory per process - less than 32K
os memory - 256K
forks user processes randomly
never more than 18 processes at a time
10 ns for non dirty memory
14 ms for firty
termintes after 100 process or 2 real life seconds

## User process
32K max -> 32 pages
two memory request schemes:
- Simple: random value (0K, 32K)
- Biased: weigh vector [1, 1/2, .. 1/32] 
    scaling to [1, 1.5, 1.833, 2.0822] 
    pick page based on weight vector
    address = page_num * 1024 + rand() % 1024
    read/write : 60/40
os: page_num = address / 1024
every 1000 +/- 100 memory references check for termination (term cahance 50%)

## Stats
- number of memory accesses per second
- number of page faults per access
- average memory access speed

## Implementation
Two data structures, one for os and other for user processes
Os keeps track of free frames, as well which page is loaded into which frame
Os also keeps track of additional frame descriptors, namely reference and dirty bit
It also keeps track of active user processes.
Interaction between os and user processes is implemented via semaphore.
Important function in oss.c is handle_processes. It cycliclly serves active processes.
On pagefault, it uses second chance (aka clock) algorithm to find page for swaping.
On termination, all resourses are securely freed and finall stats are printed to stdout, 
rs well as in the logfile.
More detailed explanation is the source code itself

## Note
With no limitation regarding number of lines in logfile, with 
current settings, depending on the CPU used to execute the program, 
logfile can get up to 20mb, with little over hald million lines.
