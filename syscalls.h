/*
 * syscalls.h 
 *
 * Yalnix system call function prototypes 
 */ 

#ifndef SYSCALLS_H 
#define SYSCALLS_H 

#include <kernel_func.h>

/* *********************************** Function Prototypes *********************************** */ 
void kernel_getpid(PCB_t *curr_pcb); 
void kernel_brk(PCB_t *curr_pcb); 
void kernel_delay(PCB_t *curr_pcb);  
void kernel_fork(PCB_t *curr_pcb);
void kernel_exec(PCB_t *curr_pcb);
void kernel_exit(PCB_t *curr_pcb);
void kernel_wait(PCB_t *curr_pcb);
void kernel_tty_read(PCB_t *curr_pcb); 
void kernel_tty_write(PCB_t *curr_pcb);
void kernel_pipe_init(PCB_t *curr_pcb);
void kernel_pipe_read(PCB_t *curr_pcb);
void kernel_pipe_write(PCB_t *curr_pcb);
void kernel_lock_init(PCB_t *curr_pcb); 
void kernel_lock_acquire(PCB_t *curr_pcb); 
void kernel_lock_release(PCB_t *curr_pcb); 
void kernel_cvar_init(PCB_t *curr_pcb); 
void kernel_cvar_signal(PCB_t *curr_pcb); 
void kernel_cvar_broadcast(PCB_t *curr_pcb); 
void kernel_cvar_wait(PCB_t *curr_pcb); 
void kernel_reclaim(PCB_t *curr_pcb);
int reclaim_pipe(int pipe_id, PCB_t *curr_pcb); 
int reclaim_lock(int lock_id, PCB_t *curr_pcb); 
int reclaim_cvar(int cvar_id, PCB_t *curr_pcb);
void kernel_lock_helper(PCB_t *curr_pcb, int lock_id, lock_t* lock); 

#endif
