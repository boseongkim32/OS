/*
 * syscall_sync.c
 *
 * This file contains the implementation of the synchronization system calls. 
 */

#include <hardware.h>
#include <kernel_func.h>
#include <yalnix.h>
#include <ykernel.h>
#include <load_program.h>
#include <syscalls.h> 

/* ********************************** kernel_lock_init ********************************** */ 
/*
 * Creates the lock and adds it to the list of locks. 
 */
void kernel_lock_init(PCB_t *curr_pcb) {

    TracePrintf(1, "In the kernel_lock_init handler\n");
    UserContext* uctxt = &(curr_pcb->uc);

    // collecting arguments passed to the library call
    int *lock_idp = (int*) uctxt->regs[0]; 

    // creating a new lock
    lock_t *lock = malloc(sizeof(lock_t));

    // in case of error, return ERROR
    if (lock == NULL) {
        TracePrintf(1, "Failed to allocate memory for lock\n");
        uctxt->regs[0] = ERROR;
        return;
    }
    
    lock->lockID = locks_num; 
    locks_num += 2;
    lock->status = 0;
    lock->waitingHead = NULL; 

    // Need to add lock to the list of locks 
    lock_node_t* new_node = (lock_node_t*) malloc(sizeof(lock_node_t)); 
    if (new_node == NULL) {
        TracePrintf(1, "Failed to allocate memory for lock node\n");
        uctxt->regs[0] = ERROR;
        return;
    }

    new_node->lock = lock; 
    new_node->next = lockHead;
    lockHead = new_node; 

    TracePrintf(1, "Lock id: %d\n", lock->lockID);

    // lock_idp now holds the identifier for the lock 
    *lock_idp = lock->lockID;

    // Return 0 for success
    uctxt->regs[0] = 0;
}

/* ********************************** kernel_lock_acquire ********************************** */ 
/*
 * Checks to see if the lock is available. If it is, the lock is acquired. If not, the caller is blocked. 
 */
void kernel_lock_acquire(PCB_t *curr_pcb) {

    TracePrintf(1, "In the kernel_lock_acquire handler\n");
    UserContext* uctxt = &(curr_pcb->uc);

    // collecting arguments passed to the library call
    int lock_id = (int) uctxt->regs[0]; 

    lock_t *lock = NULL;
    for (lock_node_t *curr = lockHead; curr != NULL; curr = curr->next) {
        if (curr->lock->lockID == lock_id) {
            lock = curr->lock;
        }
    }

    // If the lock doesn't exist, return error 
    if (lock == NULL) {
        TracePrintf(1, "Lock not found\n");
        uctxt->regs[0] = ERROR;
        return;
    }
    kernel_lock_helper(curr_pcb, lock_id, lock); 

    // Return 0 upon success 
    uctxt->regs[0] = 0; 
}

/* ********************************** kernel_lock_helper ********************************** */ 
/*
 * Helper function for kernel_lock_acquire. Used by cvar wait as well.  
 */
void kernel_lock_helper(PCB_t *curr_pcb, int lock_id, lock_t *lock) {

    if (lock->status == 0) {
        lock->status = 1;
        curr_pcb->lock_id = lock_id; 
        curr_pcb->lock = 1; 
    } else {
        // block the caller
        curr_pcb->lock = 0; 
        if (addPCB(&(lock->waitingHead), curr_pcb) == -1) {
            TracePrintf(1, "Failed to add PCB to the waiting queue\n");
            return;
        }
        PCB_t* next_pcb_p = find_ready_pcb();
        removePCB(&readyHead, next_pcb_p);
        KernelContextSwitch(KCSwitch, (void*) (curr_pcb), (void *)(next_pcb_p));

        // using region 1 page table for current process 
        WriteRegister(REG_PTBR1, (unsigned int) runningProcess -> pageTable); 
        WriteRegister(REG_PTLR1, (unsigned int) MAX_PT_LEN); 
        WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_1); 

        curr_pcb->lock = 1;
        curr_pcb->lock_id = lock_id; 
        lock->status = 1;
        // Remove the PCB from the waiting queue 
        removePCB(&(lock->waitingHead), curr_pcb); 
    }

}

/* ********************************** kernel_lock_release ********************************** */ 
/*
 * Releases the lock and tells the next process in the lock waiting queue to be ready 
 */ 
void kernel_lock_release(PCB_t *curr_pcb) {

    TracePrintf(1, "In the kernel_lock_release handler\n");
    UserContext* uctxt = &(curr_pcb->uc);

    // collecting arguments passed to the library call
    int lock_id = (int) uctxt->regs[0];

    // Need to check that the caller currently holds the lock 
    if (curr_pcb->lock_id != lock_id || curr_pcb->lock == 0) {
        TracePrintf(1, "Caller does not hold the lock\n");
        uctxt->regs[0] = ERROR;
        return;
    } 

    lock_t *lock = NULL;
    for (lock_node_t *curr = lockHead; curr != NULL; curr = curr->next) {
        if (curr->lock->lockID == lock_id) {
            lock = curr->lock;
        }
    }

    if (lock == NULL) {
        TracePrintf(1, "Lock not found\n");
        uctxt->regs[0] = ERROR;
        return;
    }

    lock->status = 0;
    curr_pcb->lock = 0;
    curr_pcb->lock_id = 0; 

    // Wake up the next process in the waiting queue and remove it from the queue 
    PCB_node_t *curr = lock->waitingHead;

    if (curr != NULL) {
        TracePrintf(2, "Waking up the next process in the waiting queue\n");
        while (curr->next != NULL) {
            curr = curr->next; 
        }
        PCB_t* next_pcb_p = curr->pcb;

        if (addPCB(&readyHead, next_pcb_p) == -1) {
            TracePrintf(1, "Failed to add PCB to the ready queue\n");
            uctxt->regs[0] = ERROR;
            return;
        } else {
            TracePrintf(1, "In the release lock method. We added PCB %d to the ready queue\n", next_pcb_p->pid);
            removePCB(&(lock->waitingHead), next_pcb_p);
        }
    }
    
    // return 0 upon success 
    uctxt->regs[0] = 0; 
}

/* ********************************** kernel_cvar_init ********************************** */ 
/*
 * Initializes a condition variable and adds it to the list of condition variables
 */
void kernel_cvar_init(PCB_t *curr_pcb) {

    TracePrintf(1, "In the kernel_cvar_init handler\n");
    UserContext* uctxt = &(curr_pcb->uc);

    // colelcting arguments passed to the library call
    int *cvar_id = (int*) uctxt->regs[0]; 

    // creating a new cvar
    cvar_t *cvar = malloc(sizeof(cvar_t));
    // in case of error, return ERROR
    if (cvar == NULL) {
        TracePrintf(1, "Failed to allocate memory for cvar\n");
        uctxt->regs[0] = ERROR;
        return;
    }
    
    cvar->cvarID = cvars_num; 
    cvars_num += 2;
    cvar->waitingHead = NULL;

    TracePrintf(2, "Cvar id: %d\n", cvar->cvarID);

    // add cvar to the list of cvars
    cvar_node_t* new_node = (cvar_node_t*) malloc(sizeof(cvar_node_t)); 
    if (new_node == NULL) {
        TracePrintf(1, "Failed to allocate memory for cvar node\n");
        uctxt->regs[0] = ERROR;
        return;
    } 
    new_node->cvar = cvar; 
    new_node->next = cvarHead;
    cvarHead = new_node; 

    *cvar_id = cvar->cvarID;

    // Return 0 for success 
    uctxt->regs[0] = 0; 
} 

/* ********************************** kernel_cvar_signal ********************************** */ 
/*
 * Wakes up the first process in the waiting queue of the condition variable 
 */
void kernel_cvar_signal(PCB_t *curr_pcb) {

    TracePrintf(1, "In the kernel_cvar_signal handler\n");
    UserContext* uctxt = &(curr_pcb->uc);

    // collecting arguments passed to the library call
    int cvar_id = (int) uctxt->regs[0]; 

    cvar_t *cvar = NULL;
    for (cvar_node_t *curr = cvarHead; curr != NULL; curr = curr->next) {
        if (curr->cvar->cvarID == cvar_id) {
            cvar = curr->cvar;
        }
    }

    if (cvar == NULL) {
        TracePrintf(1, "Cvar not found\n");
        uctxt->regs[0] = ERROR;
        return;
    }

    if (cvar->waitingHead != NULL) {
        PCB_t* next_pcb_p = cvar->waitingHead->pcb; 

        if (addPCB(&readyHead, next_pcb_p) == -1) {
            TracePrintf(1, "Failed to add PCB to the ready queue\n");
            uctxt->regs[0] = ERROR;
            return;
        } else {
            TracePrintf(1, "In the signal cvar method. We added PCB %d to the ready queue\n", next_pcb_p->pid);
            removePCB(&(cvar->waitingHead), next_pcb_p);
        }
    }

    // return 0 upon success 
    uctxt->regs[0] = 0; 
} 

/* ********************************** kernel_cvar_broadcast ********************************** */ 
/*
 * Wakes up all processes in the waiting queue of the condition variable 
 */
void kernel_cvar_broadcast(PCB_t *curr_pcb) {

    TracePrintf(1, "In the kernel_cvar_broadcast handler\n");
    UserContext* uctxt = &(curr_pcb->uc);

    // collecting arguments passed to the library call
    int cvar_id = (int) uctxt->regs[0]; 

    cvar_t *cvar = NULL;
    for (cvar_node_t *curr = cvarHead; curr != NULL; curr = curr->next) {
        if (curr->cvar->cvarID == cvar_id) {
            cvar = curr->cvar;
        }
    }

    if (cvar == NULL) {
        TracePrintf(1, "Cvar not found\n");
        uctxt->regs[0] = ERROR;
        return;
    }

    while (cvar->waitingHead != NULL) {
        PCB_t* next_pcb_p = cvar->waitingHead->pcb;        
        if (addPCB(&readyHead, next_pcb_p) == -1) {
            TracePrintf(1, "Failed to add PCB to the ready queue\n");
            uctxt->regs[0] = ERROR;
            return;
        } else {
            // remove the PCB from the waiting queue 
            removePCB(&(cvar->waitingHead), next_pcb_p);
        }
    }

    // return 0 upon success 
    uctxt->regs[0] = 0; 
}

/* ********************************** kernel_cvar_wait ********************************** */ 
/*
 * Waits on a condition variable. The caller releases the lock and is blocked. 
 */
void kernel_cvar_wait(PCB_t *curr_pcb) {

    TracePrintf(1, "In the kernel_cvar_wait handler\n");
    UserContext* uctxt = &(curr_pcb->uc);

    // collecting arguments passed to the library call
    int cvar_id = (int) uctxt->regs[0]; 
    int lock_id = (int) uctxt->regs[1]; 

    // Need to check that the caller currently holds the lock 
    if (curr_pcb->lock_id != lock_id || curr_pcb->lock == 0) {
        TracePrintf(1, "Caller does not hold the lock\n");
        uctxt->regs[0] = ERROR;
        return;
    } 

    // Release the lock identified by lock_id 
    lock_t *lock = NULL; 
    for (lock_node_t *curr = lockHead; curr != NULL; curr = curr->next) {
        if (curr->lock->lockID == lock_id) {
            lock = curr->lock;
        }
    }

    if (lock == NULL) {
        TracePrintf(1, "Lock not found\n");
        uctxt->regs[0] = ERROR;
        return;
    }

    lock->status = 0; 
    curr_pcb->lock = 0; 
    curr_pcb->lock_id = 0; 

    // Look for the condition variable 
    cvar_t *cvar = NULL;
    for (cvar_node_t *curr = cvarHead; curr != NULL; curr = curr->next) {
        if (curr->cvar->cvarID == cvar_id) {
            cvar = curr->cvar;
        }
    }

    if (cvar == NULL) {
        TracePrintf(1, "Cvar not found\n");
        uctxt->regs[0] = ERROR;
        return;
    }

    // Block the caller 
    if (addPCB(&(cvar->waitingHead), curr_pcb) == -1) {
        TracePrintf(1, "Failed to add PCB to the waiting queue\n");
        uctxt->regs[0] = ERROR;
        return;
    }
    PCB_t* next_pcb_p = find_ready_pcb();
    removePCB(&readyHead, next_pcb_p); 
    KernelContextSwitch(KCSwitch, (void*) (curr_pcb), (void *)(next_pcb_p)); 

    // using region 1 page table for current process 
    WriteRegister(REG_PTBR1, (unsigned int) runningProcess -> pageTable); 
    WriteRegister(REG_PTLR1, (unsigned int) MAX_PT_LEN); 
    WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_1); 

    kernel_lock_helper(curr_pcb, lock_id, lock);
} 

/* ********************************** kernel_reclaim ********************************** */ 
/*
 * Destroys the synchronization object and frees the memory associated with it. 
 * The way that we set up the IDs of the synchronization objects, we can determine the type of the object by looking at the ID. 
 */ 
void kernel_reclaim(PCB_t *curr_pcb) {

    TracePrintf(1, "In the kernel_reclaim handler\n");
    UserContext* uctxt = &(curr_pcb->uc);

    // collecting arguments passed to the library call
    int object_id = (int) uctxt->regs[0]; 

    if (object_id == 0) {
        TracePrintf(1, "Not a valid object id\n"); 
        uctxt->regs[0] = ERROR;
        return;
    }

    int caseVal; 
    int returnVal; 

    // Check to see what kind of object it is 
    if (object_id < 0) {
        caseVal = 0; 
    } else if (object_id % 2 == 0) {
        caseVal = 1; 
    } else {
        caseVal = 2; 
    }

    // Depending on the type of object, call the appropriate helper function 
    switch (caseVal) {
        case 0:
            TracePrintf(1, "Reclaiming pipe\n"); 
            returnVal = reclaim_pipe(object_id, curr_pcb); 
            break; 
        case 1:
            TracePrintf(1, "Reclaiming lock\n"); 
            returnVal = reclaim_lock(object_id, curr_pcb); 
            break; 
        case 2: 
            TracePrintf(1, "Reclaiming cvar\n"); 
            returnVal = reclaim_cvar(object_id, curr_pcb); 
            break; 
    } 
    
    if (returnVal == 0) {
        uctxt->regs[0] = 0; 
    } else {
        uctxt->regs[0] = ERROR; 
    }
}

/* ********************************** Helper Functions for Kernel Reclaim ********************************** */ 

/* ********************************** reclaim_pipe ********************************** */ 
int reclaim_pipe(int pipe_id, PCB_t *curr_pcb) { 

    // Check if any process is waiting on the pipe 
    for (PCB_node_t* curr = blockedHead; curr != NULL; curr = curr->next) {
        if ((curr->pcb->pipe_id == pipe_id) && (curr->pcb->read == 1)) {
            return -1; 
        }
    }

    // Free the associated memory and adjust the list of pipes 
    pipe_t *pipe = NULL;
    pipe_node_t *prev = NULL; 
    pipe_node_t *curr = pipeHead; 
    while (curr != NULL) {
        if (curr->pipe->id == pipe_id) {
            pipe = curr->pipe;
            if (prev == NULL) {
                pipeHead = curr->next; 
            } else {
                prev->next = curr->next; 
            }
            free(pipe->buffer); 
            free(pipe); 
            free(curr);
            return 0; 
        }
        prev = curr; 
        curr = curr->next; 
    }
    return -1; 
}

/* ********************************** reclaim_lock ********************************** */ 
int reclaim_lock(int lock_id, PCB_t *curr_pcb) {

    if ((curr_pcb->lock_id != lock_id) || (curr_pcb->lock == 0)) {
        TracePrintf(1, "Caller does not hold the lock\n"); 
        return -1; 
    }

    // Free the associated memory and adjust the list of locks 
    lock_t *lock = NULL;
    lock_node_t *prev = NULL; 
    lock_node_t *curr = lockHead; 
    while (curr != NULL) {
        if (curr->lock->lockID == lock_id) {

            lock = curr->lock;
            if (lock->waitingHead != NULL) {
                TracePrintf(1, "There are processes waiting on this lock\n"); 
                return -1; 
            } 

            if (prev == NULL) {
                lockHead = curr->next; 
            } else {
                prev->next = curr->next; 
            }
            free(lock); 
            free(curr);
            return 0; 
        }
        prev = curr; 
        curr = curr->next; 
    }

    return -1; 
}

/* ********************************** reclaim_cvar ********************************** */ 
int reclaim_cvar(int cvar_id, PCB_t *curr_pcb) {

    cvar_t *cvar = NULL;
    cvar_node_t *prev = NULL; 
    cvar_node_t *curr = cvarHead; 

    // Free the associated memory and adjust the list of condition variables 
    while (curr != NULL) {
        if (curr->cvar->cvarID == cvar_id) {
            cvar = curr->cvar;
            if (cvar->waitingHead != NULL) {
                TracePrintf(1, "There are processes waiting on this cvar\n"); 
                return -1; 
            }

            if (prev == NULL) {
                cvarHead = curr->next; 
            } else {
                prev->next = curr->next; 
            }
            free(cvar); 
            free(curr);
            return 0; 
        }
        prev = curr; 
        curr = curr->next; 
    }
    return -1; 
}
