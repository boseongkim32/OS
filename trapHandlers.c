/*
 * trapHandlers.c 
 *
 * This file contains the implementation of the trap handlers for the Yalnix kernel. 
 */

#include <hardware.h>
#include <kernel_func.h>
#include <yalnix.h> 
#include <ykernel.h>
#include <load_program.h>
#include <syscalls.h>

/* ********************************** handle_trap_kernel ********************************** */ 
/*
 * Any syscall called by the user will enter into this trap. Takes the trap code and calls the syscall handler accordingly.  
 */
void handle_trap_kernel(UserContext *uctxt) {

    // Copy current UserContext into the PCB of the current process 
    runningProcess->uc = *uctxt; 

    // Get the code sent by the trap 
    int trap_code = uctxt->code;

    // Pass a pointer to the usercontext inside the PCB of the current process 
    switch(trap_code) {
        case YALNIX_GETPID:
            TracePrintf(1, "CALLING YALNIX_GETPID\n");
            kernel_getpid(runningProcess);
            break;
        case YALNIX_BRK: 
            TracePrintf(1, "CALLING YALNIX_BRK\n"); 
            kernel_brk(runningProcess); 
            break; 
        case YALNIX_DELAY:
            TracePrintf(1, "CALLING YALNIX_DELAY\n");
            kernel_delay(runningProcess); 
            break; 
        case YALNIX_FORK:
            TracePrintf(1, "CALLING YALNIX_FORK\n");
            kernel_fork(runningProcess);
            break;
        case YALNIX_EXEC:
            TracePrintf(1, "CALLING YALNIX_EXEC\n");
            kernel_exec(runningProcess);
            break;
        case YALNIX_EXIT:
            TracePrintf(1, "CALLING YALNIX_EXIT\n");
            kernel_exit(runningProcess);
            break;
        case YALNIX_WAIT:
            TracePrintf(1, "CALLING YALNIX_WAIT\n");
            kernel_wait(runningProcess);
            break;
        case YALNIX_PIPE_INIT:
            TracePrintf(1, "CALLING YALNIX_PIPE_INIT\n");
            kernel_pipe_init(runningProcess);
            break;
        case YALNIX_PIPE_READ:
            TracePrintf(1, "CALLING YALNIX_PIPE_READ\n");
            kernel_pipe_read(runningProcess);
            break;
        case YALNIX_PIPE_WRITE:
            TracePrintf(1, "CALLING YALNIX_PIPE_WRITE\n");
            kernel_pipe_write(runningProcess);
            break;
        case YALNIX_LOCK_INIT: 
            TracePrintf(1, "CALLING YALNIX_LOCK_INIT\n");
            kernel_lock_init(runningProcess);
            break;
        case YALNIX_LOCK_ACQUIRE: 
            TracePrintf(1, "CALLING YALNIX_LOCK_ACQUIRE\n");
            kernel_lock_acquire(runningProcess);
            break; 
        case YALNIX_LOCK_RELEASE:
            TracePrintf(1, "CALLING YALNIX_LOCK_RELEASE\n");
            kernel_lock_release(runningProcess);
            break;
        case YALNIX_CVAR_INIT: 
            TracePrintf(1, "CALLING YALNIX_CVAR_INIT\n");
            kernel_cvar_init(runningProcess);
            break; 
        case YALNIX_CVAR_SIGNAL: 
            TracePrintf(1, "CALLING YALNIX_CVAR_SIGNAL\n");
            kernel_cvar_signal(runningProcess);
            break;
        case YALNIX_CVAR_BROADCAST: 
            TracePrintf(1, "CALLING YALNIX_CVAR_BROADCAST\n");
            kernel_cvar_broadcast(runningProcess);
            break; 
        case YALNIX_CVAR_WAIT: 
            TracePrintf(1, "CALLING YALNIX_CVAR_WAIT\n");
            kernel_cvar_wait(runningProcess);
            break;
        case YALNIX_RECLAIM: 
            TracePrintf(1, "CALLING YALNIX_RECLAIM\n");
            kernel_reclaim(runningProcess);
            break; 
        case YALNIX_TTY_WRITE: 
            TracePrintf(1, "CALLING YALNIX_TTY_WRITE\n");
            kernel_tty_write(runningProcess);
            break;
        case YALNIX_TTY_READ: 
            TracePrintf(1, "CALLING YALNIX_TTY_READ\n");
            kernel_tty_read(runningProcess);
            break;
        default:
            TracePrintf(1, "Trap code not recognized\n"); 
    }

    TracePrintf(1, "PID: %d finished handling its trap business\n", runningProcess->pid);

    // Copy the UserContext from the PCB of the current process into the UserContext pointer 
    *uctxt = runningProcess->uc; 
}

/* ********************************** handle_trap_clock ********************************** */ 
/*
 * Hardware clock interrupts call this trap handler. We search through the blocked queues and place on the ready queue any processes that are available to run.  
 */
void handle_trap_clock(UserContext *uctxt) {

    // check to see if we can switch to a delayed process
    for (PCB_node_t* curr = blockedHead; curr != NULL; curr = curr->next) {

        // Processes that are delayed 
        if (curr->pcb->goTime > 0 && curr->pcb->delay == 1) {
            curr->pcb->goTime--; 
            if (curr->pcb->goTime == 0) {
                if (addPCB(&readyHead, curr->pcb) == -1) {
                    TracePrintf(1, "Failed to add process %d to ready queue\n", curr->pcb->pid);
                } else {
                    removePCB(&blockedHead, curr->pcb);
                }
            }
        }
       
        // Processes that are waiting for a child to finish 
        if (curr->pcb->wait == 1) {
            // check in defunct queue to see if there is a process that has current process as a parent
            for (PCB_node_t* defunct = defunctHead; defunct != NULL; defunct = defunct->next) {
                if (defunct->pcb->parent == curr->pcb) {
                    if (addPCB(&readyHead, curr->pcb) == -1) {
                        TracePrintf(1, "Failed to add process  to ready queue\n");
                    } else {
                        removePCB(&blockedHead, curr->pcb);
                    }
                }
            }
        }       

        // Processes that are waiting for a pipe to be written to 
        if (curr->pcb->read == 1) {
            // find the pipe that the process is reading from
            for (pipe_node_t* pipe_node = pipeHead; pipe_node != NULL; pipe_node = pipe_node->next) {
                if (pipe_node->pipe->id == curr->pcb->pipe_id) {
                    if (pipe_node->pipe->writeIndex != pipe_node->pipe->readIndex) {
                        if (addPCB(&readyHead, curr->pcb) == -1) {
                            TracePrintf(1, "Failed to add PCB to ready queue\n");
                        } else {
                            removePCB(&blockedHead, curr->pcb);
                        }
                    }
                }
            }
        }    
    }
    TracePrintf(2, "My PID: %d\n", runningProcess->pid);
    TracePrintf(2, "About to find a current pcb\n");
    PCB_t* next_pcb_p = find_ready_pcb();

    // copy current UserContext into the PCB of the current process
    runningProcess->uc = *uctxt;
   
    if (addPCB(&readyHead, runningProcess) == -1) {
        TracePrintf(1, "Failed to add process to ready queue\n");
    } else {
        removePCB(&readyHead, next_pcb_p); 
    }

    TracePrintf(2, "About to switch context\n"); 
    int rc = KernelContextSwitch(KCSwitch, (void*) (runningProcess), (void *)(next_pcb_p));
    if (rc != 0) {
        TracePrintf(1, "KernelContextSwitch failed\n");
        runningProcess = idlePCB; 
    }

    TracePrintf(2, "Returned from clock tick into some new process\n");

    // using region 1 page table for current process
    WriteRegister(REG_PTBR1, (unsigned int) runningProcess -> pageTable); 
    WriteRegister(REG_PTLR1, (unsigned int) MAX_PT_LEN); 
    // Flush the TLB to remove stale mappings
    WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_1); 

    // new current process
    *uctxt = runningProcess->uc;
}

/* ********************************** handle_trap_illegal ********************************** */ 
/*
 * Illegal instruction trap handler 
 * Abort the currently running Yalnix user process but continue running other processes.
 */ 
void handle_trap_illegal(UserContext *uctxt) {

    TracePrintf(1, "Illegal instruction encountered\n");
    // Exit the current process
    PCB_t* curr_pcb = runningProcess;
    
    // Adjust the status of curr_pcb to indicate error
    curr_pcb->status = ERROR;
    kernel_exit(curr_pcb); // dispatches new process to run
}

/* ********************************** handle_trap_memory ********************************** */ 
/*
 * Memory trap handler. If it's below region 1 or beyond it, or not near the stack, we kill it. Otherwise, we grow the stack.  
 */
void handle_trap_memory(UserContext *uctxt) {

    TracePrintf(1, "Memory trap\n");

    // find the faulting address from user context
    int addr = (int) uctxt->addr;
    int faulting_page = (addr >> PAGESHIFT) - MAX_PT_LEN;

    TracePrintf(1, "Faulting address: 0x%x\n", addr);
    TracePrintf(3, "Faulting page: %d\n", faulting_page); 
    TracePrintf(3, "Last user stack page: %d\n", runningProcess->lastUserStackPage);
    TracePrintf(3, "Last user data page: %d\n", runningProcess->lastUserDataPage);
    TracePrintf(3, "Break: %d\n", runningProcess->brk);
    TracePrintf(3, "PID of current process: %d\n", runningProcess->pid);

    // Is address in region 1?
    if (addr < VMEM_1_BASE || addr >= VMEM_1_LIMIT) {
        TracePrintf(1, "Address is not in region 1\n");
        // exit current process; dispatches new process to run
        runningProcess->status = ERROR;
        kernel_exit(runningProcess);
        return;
    }
    // faulting address is in region 1
    if (addr >= VMEM_1_BASE) {
        // check if the address is within the range of 2 pages from user stack page  but above break
        if (faulting_page >= (runningProcess->lastUserStackPage - 2) && (faulting_page <= runningProcess->lastUserStackPage) && (faulting_page > runningProcess->brk)) {
            TracePrintf(1, "Address is within 2 pages from user stack page\n");
            TracePrintf(1, "Faulting Page: %d, Last User Stack Page: %d\n", faulting_page, runningProcess->lastUserStackPage);
            // allocate pages to stack to "cover" the faulting page
            for (int i = faulting_page; i <= runningProcess->lastUserStackPage; i++) {
                int index = findNextEmptySpace();
                if (index == -1) {
                    TracePrintf(1, "No available physical memory\n");
                    // exit current process; dispatches new process to run
                    runningProcess->status = ERROR;
                    kernel_exit(runningProcess);
                    return;
                }
                runningProcess->pageTable[i].valid = 1;
                runningProcess->pageTable[i].prot = PROT_READ | PROT_WRITE;
                runningProcess->pageTable[i].pfn = index;
            }
            
            // updating last user stack page
            runningProcess->lastUserStackPage = faulting_page - 1;
        } else {
            TracePrintf(1, "Address is not within 2 pages from user stack page\n");
            // exit current process; dispatches new process to run
            TracePrintf(1, "FAULTING PAGE: %d\n", faulting_page);
            runningProcess->status = ERROR;
            kernel_exit(runningProcess);
        }
    }
}

/* ********************************** handle_trap_math ********************************** */ 
/*
 * Abort the currently running Yalnix user process but continue running other processes.
 */
void handle_trap_math(UserContext *uctxt) {

    TracePrintf(1, "Illegal math operation\n");
    // Exit the current process
    PCB_t* curr_pcb = runningProcess;

    // adjust the status of curr_pcb to indicate error
    curr_pcb->status = ERROR;
    kernel_exit(curr_pcb); // dispatches new process to run
}

/* ********************************** handle_trap_tty_transmit ********************************** */ 
/*
 * Hardware fires this trap handler once it has finished writing to the terminal. We search through the blocked processes to see if any are waiting for the terminal to be ready for another transmit. 
 */ 
void handle_trap_tty_transmit(UserContext *uctxt) {

    TracePrintf(1, "Transmit trap\n"); 
    int terminal_id = uctxt->code;
    terminal_t *terminal = terminal_array[terminal_id];

    // transmit is completed for this terminal
    terminal->terminal_status = 0; 

    // search through the blocked processes to start process whose transmit finished
    for (PCB_node_t* curr = blockedHead; curr != NULL; curr = curr->next) {
        if (curr->pcb->terminalWrite == 1 && curr->pcb->tty_id == terminal_id) {
            // if the process is waiting for its transmit to this specific terminal to be complete
            if (curr->pcb->transmit == 1) {             
                // we can now unblock that process since its transmit is complete
                if (addPCB(&readyHead, curr->pcb) == -1) {
                    TracePrintf(1, "Failed to add PCB to ready queue\n");
                } else {
                    removePCB(&blockedHead, curr->pcb);
                    TracePrintf(1, "Transmit to terminal %d is completed. We now place PCB %d on the ready queue\n", terminal_id, curr->pcb->pid);
                }

                // reset values of pcb to indicate that it has finished its transmit
                curr->pcb->transmit = 0;
                curr->pcb->terminalWrite = 0;
                curr->pcb->terminal_transmit_busy = 0;
                curr->pcb->tty_id = -1;
                break;
                
            }
        }
    } 

    // check if there is a process waiting for the terminal to be ready for another transmit 
    for (PCB_node_t* curr = blockedHead; curr != NULL; curr = curr->next) { 
        if (curr->pcb->terminalWrite == 1 && curr->pcb->tty_id == terminal_id && curr->pcb->terminal_transmit_busy == 1) {
            // we can now unblock that process since the terminal is no longer busy
            if (addPCB(&readyHead, curr->pcb) == -1) {
                TracePrintf(1, "Failed to add PCB to ready queue\n");
            } else { 
                removePCB(&blockedHead, curr->pcb);
                TracePrintf(1, "Transmit in progress completed! We now place PCB %d on the ready queue\n", terminal_id, curr->pcb->pid);
            }

            // indicate that it is no longer waiting for another transmit
            curr->pcb->terminalWrite = 0;
            curr->pcb->terminal_transmit_busy = 0;
            curr->pcb->transmit = 0;

            // terminal is now busy again
            terminal->terminal_status = 1;
            break; 
        }
    }
}

/* ********************************** handle_trap_tty_receive ********************************** */ 
/*
 * Hardware fires this trap handler once it has received input from the terminal. We call TtyReceive and also adjust the terminal buffer. 
 */ 
void handle_trap_tty_receive(UserContext *uctxt) {

    // Get the terminal number from the user context 
    int terminalID = uctxt->code;
    TracePrintf(1, "Receive trap from %d\n", terminalID); 
    terminal_t *terminal = terminal_array[terminalID]; 
    
    // If people keep typing to the terminal without anyone reading, the buffer can fill up. Thus, we should expand it 2x and copy it over 
    if (terminal->pos_of_next_char + TERMINAL_MAX_LINE >= terminal->terminal_buffer_size) {
        TracePrintf(1, "Expanding the terminal buffer\n"); 
        char* expandedBuffer = (char*) malloc(sizeof(char) * 2 * terminal->terminal_buffer_size);
        if (expandedBuffer == NULL) {
            TracePrintf(1, "Failed to allocate memory for expanded terminal buffer\n"); 
            return; 
        }
        memcpy(expandedBuffer, terminal->buffer, terminal->terminal_buffer_size);
        free(terminal->buffer); 
        terminal->buffer = expandedBuffer;
        terminal->terminal_buffer_size *= 2; 
    }

    // Read next line from the terminal into the terminal buffer 
    int length = TtyReceive(terminalID, terminal->buffer + terminal->pos_of_next_char, TERMINAL_MAX_LINE);

    // Search through the blocked process to see if any are waiting for input from the terminal 
    for (PCB_node_t* curr = blockedHead; curr != NULL; curr = curr->next) {
        if (curr->pcb->terminalRead == 1 && curr->pcb->tty_id == terminalID) {
            if (addPCB(&readyHead, curr->pcb) == -1) {
                TracePrintf(1, "Failed to add PCB to ready queue\n");
            } else {
                TracePrintf(1, "In trap_tty_receive, placing PCB %d on the ready queue\n", curr->pcb->pid);
                removePCB(&blockedHead, curr->pcb);
            }
            curr->pcb->terminalRead = 0; 
            break; 
        }
    }

    // Update position of the last character 
    terminal->pos_of_next_char += length;
} 

/* ********************************** other_trap ********************************** */ 
void other_trap() {
    TracePrintf(1, "This trap is not yet handled\n");
}
