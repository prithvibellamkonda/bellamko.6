#define _XOPEN_SOURCE 700
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/mman.h>

#define PTR shared_mem** sh_mem, user_mem*** u_mems
#define ADR &sh_mem, &u_mems

#define MAX_PAGES 256U
#define MAX_PROCESSES 18U
#define PROCESSES_SPAWN_CHANCE 12U

#define NS_S 1000000000U
#define MS_S 1000U

#define TERMINATED 2U

#define RD 0U
#define WR 1U

//stats
unsigned next_id = 0;
unsigned mem_accss_n = 0;
unsigned page_fault_n = 0;
unsigned clock_ptr = 0;

FILE* logfile;


void
advance_clock_ptr()
{
    clock_ptr = (clock_ptr + 1) % MAX_PAGES;
}

unsigned
address_to_page(unsigned address)
{
    return address / 1024;
}

typedef struct{
    unsigned seconds;
    unsigned nanoseconds;
} clockwork;

typedef struct{
    int pages[MAX_PAGES];
    unsigned dirty[MAX_PAGES];
    unsigned referenced[MAX_PAGES];
    unsigned processes[MAX_PROCESSES];
    clockwork cw;
} shared_mem;

typedef struct{
    sem_t sem_r;
    sem_t sem_w;
    sem_t sem_ok;
    unsigned usr_msg;
    unsigned request;
    unsigned mode;
    unsigned id;
    unsigned b_id;
} user_mem;


clockwork
clockwork_new(unsigned seconds, unsigned nanoseconds)
{
    clockwork cw;
    cw.seconds = seconds;
    cw.nanoseconds = nanoseconds;
    return cw;
}

void
clockwork_add(clockwork* c1, clockwork c2)
{
    unsigned ns = (*c1).nanoseconds + c2.nanoseconds;
    (*c1).nanoseconds = ns % NS_S;
    (*c1).seconds = (*c1).seconds + c2.seconds + (ns / NS_S);
}

int
clockwork_gt(clockwork c1, clockwork c2)
{
    if(c1.seconds > c2.seconds) return 1;
    if(c1.seconds == c2.seconds){
        if(c1.nanoseconds > c2.nanoseconds) return 1;
    } 
    return -1;
}

void
write_exit(clockwork c)
{
    double s = c.seconds + (double)c.nanoseconds/NS_S;
    double mem_access_per_second = mem_accss_n/s;
    double page_faults_per_mem_access = (double)page_fault_n/mem_accss_n;
    double avg_access_speed = ((mem_accss_n-page_fault_n)*10 + page_fault_n*14*(NS_S/MS_S))/(double)mem_accss_n;
    char r[512];
    snprintf(r, 512, "Memory accesses per second: %.4lf, Page faults per memory access: %.4lf, Average memory access time in nanoseconds: %.4lf\n", mem_access_per_second, page_faults_per_mem_access, avg_access_speed);
    printf("%s", r);
    fputs(r, logfile);
    fflush(logfile);
}

void
write_request(user_mem** process, unsigned address, unsigned mode, clockwork c)
{
    char r[126];
    if(mode == WR)
        snprintf(r, 126, "P%u requesting write on address %u at %u:%u\n", (*process)->id, address, c.seconds, c.nanoseconds);
    else
        snprintf(r, 126, "P%u requesting read on address %u at %u:%u\n", (*process)->id, address, c.seconds, c.nanoseconds);
    fputs(r, logfile);
    fflush(logfile);
}

void
write_found_dirty(user_mem** process, unsigned address)
{
    char r[126];
    snprintf(r, 126, "Indicating to P%u that write has happened to address %u\n", (*process)->id, address);
    fputs(r, logfile);
    fflush(logfile);
}

void
write_page_found(user_mem** process, unsigned address, unsigned page, clockwork c)
{
    char r[126];
    snprintf(r, 126, "Address %u found in frame %u, giving data to P%u at %u:%u\n", address, page, (*process)->id, c.seconds, c.nanoseconds);
    fputs(r, logfile);
    fflush(logfile);
}

void
write_page_fault(unsigned address)
{
    char r[126];
    snprintf(r, 126, "Address %u not in frame, pagefault\n", address);
    fputs(r, logfile);
    fflush(logfile);
}

void
write_swap(unsigned old_page, unsigned new_page)
{
    char r[126];
    snprintf(r, 126, "Clearing frame %u and swaping in page %u\n", old_page, new_page);
    fputs(r, logfile);
    fflush(logfile);
}

void
write_dirty(unsigned page)
{
    char r[126];
    snprintf(r, 126, "Frame %u marked dirty, taking additional time to write changes\n", page);
    fputs(r, logfile);
    fflush(logfile);
}

void
write_load(unsigned page, unsigned frame)
{
    char r[126];
    snprintf(r, 126, "Loading page %u into frame %u\n", page);
    fputs(r, logfile);
    fflush(logfile);
}

void
write_terminated(user_mem** process, clockwork c)
{
    char r[256];
    snprintf(r, 126, "P%u terminated at %u:%u\n", (*process)->id, c.seconds, c.nanoseconds);
    fputs(r, logfile);
    fflush(logfile);
}

void
write_current_state(shared_mem** sh_mem)
{
    char r[256];
    snprintf(r, 256, "Current memory layout at time %u:%u\nframe occupied ref_bit dirty_bit\n", ((*sh_mem)->cw).seconds, ((*sh_mem)->cw).nanoseconds);
    fputs(r, logfile);
    for(unsigned i=0; i<MAX_PAGES; i++){
        char p[126];
        if(((*sh_mem)->pages)[i] == -1){
            snprintf(p, 126, "%-3u   no       %u       %u\n", i, ((*sh_mem)->referenced)[i], ((*sh_mem)->dirty)[i]);
        }
        else{
            snprintf(p, 126, "%-3u   yes      %u       %u\n", i, ((*sh_mem)->referenced)[i], ((*sh_mem)->dirty)[i]);
        }
        fputs(p, logfile);
    }
    fflush(logfile);
}

void*
create_memory_block(char* fpath, unsigned size)
{
	int memFd = shm_open(fpath, O_RDWR|O_CREAT, 0600);
	ftruncate(memFd, size);
	void* addr = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, memFd, 0);
	close(memFd);
	return addr;
}

void*
create_user_block(unsigned n, unsigned size)
{
    char tmp[4];
    snprintf(tmp, 4, "/%u", n);
    return create_memory_block(tmp, size);
}

void
init_share_mem(shared_mem** sh_mem)
{
    for(unsigned i=0; i<MAX_PROCESSES; i++){
        ((*sh_mem)->processes)[i] = 0;
    }
    for(unsigned i=0; i<MAX_PAGES; i++){
        ((*sh_mem)->pages)[i] = -1;
        ((*sh_mem)->dirty)[i] = 0;
        ((*sh_mem)->referenced)[i] = 0;
    }
    (*sh_mem)->cw = clockwork_new(0,0);
}

void
init_user_mem(user_mem** u_mem)
{
    sem_init(&((*u_mem)->sem_r), 1, 1);
    sem_init(&((*u_mem)->sem_w), 1, 0);
    sem_init(&((*u_mem)->sem_ok), 1, 0);
    (*u_mem)->usr_msg = 0;
    (*u_mem)->request = 0;
    (*u_mem)->mode = 0;
    (*u_mem)->id = next_id++; 
    (*u_mem)->b_id = (*u_mem)->id % MAX_PROCESSES; 
}

void
clean_and_exit(PTR)
{
    munmap((*sh_mem), sizeof(shared_mem));
    shm_unlink("/shared_mem");
    killpg(getpid(), SIGINT);
    while (wait(NULL) > 0);
    for(unsigned i=0; i<MAX_PROCESSES; i++){
        char tmp[4];
        snprintf(tmp, 4, "/%u", i);
        shm_unlink(tmp);
    }
    free(*u_mems);
    fclose(logfile);
    exit(EXIT_SUCCESS);
}

void
init(PTR)
{
    *sh_mem = create_memory_block("/shared_mem", sizeof(shared_mem));
    init_share_mem(&(*sh_mem));
    *u_mems = malloc(MAX_PROCESSES * sizeof(user_mem*));
    for(unsigned i=0; i<MAX_PROCESSES; i++){
        (*u_mems)[i] = create_user_block(i, sizeof(user_mem));
    }
}

void
spawn_process(unsigned n)
{
    char tmp[4];
    snprintf(tmp, 4, "/%u", n);
    char rn[2];
    snprintf(rn, 2, "%u", rand() % 2);
    pid_t pid = fork();
    if(pid == 0){
        execl("./process", "process", tmp, rn, (char *) NULL);
        exit(EXIT_SUCCESS);
    }
    else return;
}

void
create_process(PTR)
{
    for(unsigned i=0; i<MAX_PROCESSES; i++){
        if(((*sh_mem)->processes)[i] == 0){
            ((*sh_mem)->processes)[i] = 1;
            init_user_mem(&((*u_mems)[i]));
            return spawn_process(i);
        }
    }
}

int
check_page(unsigned page, shared_mem** sh_mem)
{
    for(int i=0; i<MAX_PAGES; i++)
        if(((*sh_mem)->pages)[i] == page) return i;
    return -1;
}

int
next_free_page(shared_mem** sh_mem)
{
    for(int i=0; i<MAX_PAGES; i++)
        if(((*sh_mem)->pages)[i] == -1) return i;
    return -1;
}

unsigned
find_swap(shared_mem** sh_mem)
{
    while(1){
        if(((*sh_mem)->referenced)[clock_ptr] == 0) return clock_ptr;
        ((*sh_mem)->referenced)[clock_ptr] = 0;
        advance_clock_ptr();
    }
}

void
handle_processes(PTR)
{
    for(unsigned i=0; i<MAX_PROCESSES; i++){
        if(((*sh_mem)->processes)[i] == 0) continue;
        clockwork_add(&((*sh_mem)->cw), clockwork_new(0, 5));
        sem_wait(&(((*u_mems)[i])->sem_w));

        if(((*u_mems)[i])->usr_msg == TERMINATED){
            write_terminated(&((*u_mems)[i]), (*sh_mem)->cw);
            ((*sh_mem)->processes)[i] = 0;
            sem_destroy(&(((*u_mems)[i])->sem_w));
            sem_destroy(&(((*u_mems)[i])->sem_r));
            sem_destroy(&(((*u_mems)[i])->sem_ok));
            continue;
        }

        mem_accss_n++;
        unsigned pg = address_to_page(((*u_mems)[i])->request);
        unsigned md = ((*u_mems)[i])->mode;
        int req;
        write_request(&((*u_mems)[i]), ((*u_mems)[i])->request, md, (*sh_mem)->cw);

        if((req=check_page(pg, &(*sh_mem))) != -1){ // page is in memory
            if(((*sh_mem)->dirty)[pg] == 1) write_found_dirty(&((*u_mems)[i]), ((*u_mems)[i])->request);
            else                            write_page_found(&((*u_mems)[i]), ((*u_mems)[i])->request, pg, (*sh_mem)->cw);
            ((*sh_mem)->referenced)[req] = 1;
            clockwork_add(&((*sh_mem)->cw), clockwork_new(0, 10));
            sem_post(&(((*u_mems)[i])->sem_r));
            sem_post(&(((*u_mems)[i])->sem_ok));
        }
        else{ //page_fault
            int page = next_free_page(&(*sh_mem));
            if(page == -1){ //no free frame
                page_fault_n++;
                write_page_fault(((*u_mems)[i])->request);
                unsigned swap_page = find_swap(&(*sh_mem));
                write_swap(swap_page, pg);
                if(((*sh_mem)->dirty)[swap_page] == 1) {
                    clockwork_add(&((*sh_mem)->cw), clockwork_new(0, 14*(NS_S/MS_S)));
                    write_dirty(swap_page);
                }
                ((*sh_mem)->pages)[swap_page] = pg;
                if(md == WR)    ((*sh_mem)->dirty)[swap_page] = 1;
                else            ((*sh_mem)->dirty)[swap_page] = 0;
                advance_clock_ptr();
                clockwork_add(&((*sh_mem)->cw), clockwork_new(0, 14*(NS_S/MS_S)));
                sem_post(&(((*u_mems)[i])->sem_r));
                sem_post(&(((*u_mems)[i])->sem_ok));
            }
            else{
                ((*sh_mem)->pages)[page] = pg;
                write_load(pg, page);
                if(md == WR)    ((*sh_mem)->dirty)[page] = 1;
                else            ((*sh_mem)->dirty)[page] = 0;
                advance_clock_ptr();
                clockwork_add(&((*sh_mem)->cw), clockwork_new(0, 14*(NS_S/MS_S)));
                sem_post(&(((*u_mems)[i])->sem_r));
                sem_post(&(((*u_mems)[i])->sem_ok));
            }
        }
    }
}

int
main()
{
    srand(getpid());
    system("rm -f log.log");
    logfile = fopen("log.log", "a");
    shared_mem* sh_mem; user_mem** u_mems;
    init(ADR);

    unsigned real_time = time(NULL);
    unsigned seconds = (sh_mem->cw).seconds;
    create_process(ADR);
    while(1){
        if((time(NULL) - real_time > 2) || (next_id > 100)){
            write_exit(sh_mem->cw);
            clean_and_exit(ADR);
        }
        if((rand() % 100) < PROCESSES_SPAWN_CHANCE){
            create_process(ADR);
        }
        handle_processes(ADR);
        if(((sh_mem->cw).seconds - seconds) >= 1){
            write_current_state(&sh_mem);
            seconds = (sh_mem->cw).seconds;
        }
    }

    return 0;
}

