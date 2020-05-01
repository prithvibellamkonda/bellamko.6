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

#define MAX_PAGES 256U
#define MAX_PROCESSES 18U

#define TERM_CHECK 1000U
#define TERM_CHANCE 50U

#define TERMINATED 2U

//memory request mode
#define SIMPLE 0U
#define BIASED 1U

//memory read/write
#define RD 0U
#define WR 1U

#define FRAME_MEM 32U

const double weights[32] = {1.0, 1.5, 1.8333333333333333, 2.083333333333333, 2.283333333333333, 2.4499999999999997, 2.5928571428571425, 2.7178571428571425, 2.8289682539682537, 2.9289682539682538, 3.0198773448773446, 3.103210678210678, 3.180133755133755, 3.251562326562327, 3.3182289932289937, 3.3807289932289937, 3.439552522640758, 3.4951080781963135, 3.547739657143682, 3.597739657143682, 3.6453587047627294, 3.690813250217275, 3.73429151108684, 3.7759581777535067, 3.8159581777535068, 3.854419716215045, 3.8914567532520823, 3.927171038966368, 3.9616537975870574, 3.9949871309203906, 4.02724519543652, 4.05849519543652};

unsigned
biased_page()
{
    double d = ((double)rand() / RAND_MAX) * weights[FRAME_MEM-1];
    for(unsigned i=0; i<FRAME_MEM; i++){
        if(weights[i] >= d) return i;
    }
    return 31; // make compiler happy
}

unsigned
biased_address(unsigned b)
{

    return 32768 * b + biased_page() * 1024 + (rand() % 1024);
}

unsigned
random_address(unsigned b)
{
    return rand() %  32768 + 32768 * b;
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

int sig_int = 0;

void
process_signal(int signum)
{
	switch (signum){
		case SIGINT:
            sig_int = 1 ;
			break;
        default:
            break;
	}
}

void*
get_memory_block(char* fpath, unsigned size)
{
	int memFd = shm_open(fpath, O_RDWR, 0600);
	void* addr = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, memFd, 0);
	close(memFd);
	return addr;
}

void
terminate(shared_mem** clock, user_mem** self_block) 
{
    (*self_block)->usr_msg = TERMINATED;
    sem_post(&((*self_block)->sem_w));
    munmap(*clock, sizeof(shared_mem));
    munmap(*self_block, sizeof(user_mem));
    exit(EXIT_SUCCESS);
}

int
main(int argc, char** argv)
{
    srand(getpid());
    shared_mem* clock = get_memory_block("/shared_mem", sizeof(shared_mem));
	user_mem* self_block = get_memory_block(argv[1], sizeof(user_mem));
    unsigned m_mode = atoi(argv[2]);

    unsigned last_reference = 0;
    while(1){
        signal(SIGINT,process_signal);
        if(sig_int == 1) terminate(&clock, &self_block);

        int plus_minus_hundred = (rand() % 200) - 100;
        if(last_reference > (TERM_CHECK + plus_minus_hundred)){
            last_reference = 0;
            if((rand() % 100) < TERM_CHANCE)
                terminate(&clock, &self_block);
        }

        sem_wait(&(self_block->sem_r));

        if(m_mode == SIMPLE)    self_block->request = random_address(self_block->b_id);
        else                    self_block->request = biased_address(self_block->b_id);

        if((rand() % 10) < 6)   self_block->mode = RD;
        else                    self_block->mode = WR;
        
        sem_post(&(self_block->sem_w)); 

        sem_wait(&(self_block->sem_ok)); 
        last_reference++;
    }

    return 0;
}

