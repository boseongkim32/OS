/*
 * kernel_func.h 
 *
 * This file contains the function declarations for the kernel functions. 
 */

#ifndef KERNEL_FUNC
#define KERNEL_FUNC 

#include <hardware.h> 
#include <yalnix.h>

#define TERMINAL_BUFFER_SIZE 1024

/* ************************** Forward Declarations *************************** */
typedef struct PCB PCB_t;                  // Forward declaration of PCB_t
typedef struct PCB_node PCB_node_t;        // Forward declaration of PCB_node_t

/* ************************** Globals *************************** */ 
extern int total_pipes;
extern int locks_num; 
extern int cvars_num; 

/* ************************** Structs *************************** */ 
typedef struct PCB {
  UserContext uc;
  KernelContext kc;
  int pid;
  int status;
  pte_t* pageTable;
  int firstKernelStackFrame; 
  int secondKernelStackFrame;
  int lastUserDataPage; 
  int lastUserStackPage; 
  int brk; 
  int goTime;                               // Used for delay 
  PCB_t* parent;
  PCB_node_t* childHead;
  int delay;                                // 1 if delay, 0 if not
  int wait;                                 // 1 if waiting, 0 if not
  int read;                                 // 1 if blocked on read, 0 if not
  int pipe_id;                              // useful only when process is waiting on a pipe
  int transmit;                             // 1 if blocked on transmit, 0 if not
  int terminalWrite;                        // 1 if blocked on terminal write, 0 if not
  int terminalRead;                         // 1 if blocked on terminal read, 0 if not 
  int tty_id;                               // useful only when process is waiting on a terminal
  int terminal_transmit_busy;               // 1 if terminal is busy when PCB requested for transmit, 0 if not 
  int lock_id;
  int lock;                                 // 1 if it possesses a lock, 0 if not  
} PCB_t;

typedef struct PCB_node {
    PCB_t *pcb;
    struct PCB_node *next;
} PCB_node_t;

typedef struct pipe {
  int id;
  void* buffer;
  int readIndex;
  int writeIndex;
} pipe_t;

typedef struct pipe_node {
  pipe_t *pipe;
  struct pipe_node *next;
} pipe_node_t;

typedef struct terminal_t {
    int tty_id;
    char* buffer;
    int pos_of_next_char;
    int terminal_status;                     // 0 if not in use, 1 if in use
    int terminal_buffer_size; 
} terminal_t;

typedef struct lock_t {
  int lockID;
  int status;                                // 0 if unlocked, 1 if locked 
  PCB_node_t* waitingHead;
} lock_t;

typedef struct lock_node {
    lock_t* lock;
    struct lock_node *next; 
} lock_node_t; 

typedef struct cvar_t {
  int cvarID;
  PCB_node_t* waitingHead; 
} cvar_t;

typedef struct cvar_node {
    cvar_t* cvar;
    struct cvar_node *next; 
} cvar_node_t; 

/* *************************** Global Variables *************************** */ 
extern int* free_frames;
extern int free_frames_len; 
extern PCB_t *runningProcess; 
extern PCB_node_t *readyHead;
extern PCB_node_t *blockedHead; 
extern PCB_node_t *defunctHead; 
extern pipe_node_t *pipeHead;
extern lock_node_t *lockHead; 
extern cvar_node_t *cvarHead; 
extern PCB_t* idlePCB;
extern terminal_t* terminal_array[NUM_TERMINALS];
extern pte_t kernelPageTable[MAX_PT_LEN];

/* *************************** Function Declarations *************************** */ 
int findNextEmptySpace(); 
void* add_to_region0_pageTable(int index, int permissions);
void DoIdle();
void DoInit();
void printKernelTable(); 
void printTable(void* pageTableAddr);
PCB_t* initializePCB(UserContext uc, KernelContext kc, pte_t* pageTable); 
pte_t* setupUserPageTable(); 
KernelContext *KCCopy(KernelContext *kc_in, void *new_pcb_p_void, void *not_used);
KernelContext *KCSwitch(KernelContext *kc_in, void *curr_pcb_p, void *next_pcb_p);
int addPCB(PCB_node_t **head, PCB_t *pcb);
int addPipe(pipe_node_t **head, pipe_t *pipe);
PCB_t* removePCB(PCB_node_t **head, PCB_t *pcb); 
PCB_t* find_ready_pcb();
int find_empty_page(pte_t* pageTable);
void displayReadyQueue();
void displayBlockedQueue();

/* *************************** Trap Handlers *************************** */ 
void handle_trap_clock(UserContext *uctxt); 
void handle_trap_kernel(UserContext *uctxt); 
void handle_trap_illegal(UserContext *uctxt);
void handle_trap_memory(UserContext *uctxt);
void handle_trap_math(UserContext *uctxt);
void handle_trap_tty_transmit(UserContext *uctxt); 
void handle_trap_tty_receive(UserContext *uctxt);
void other_trap(); 

#endif // KERNEL_FUNC 
