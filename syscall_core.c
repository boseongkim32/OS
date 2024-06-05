/*
 * syscall_core.c 
 *
 * This file contains the implementation of some of the core system calls. 
 */

#include <hardware.h> 
#include <kernel_func.h> 
#include <yalnix.h>
#include <ykernel.h>
#include <load_program.h>
#include <syscalls.h> 

/* ********************************** kernel_getpid ********************************** */
/*
 * The PCB already has the pid info, so we just return that. 
 */
void kernel_getpid(PCB_t* curr_pcb) {
    curr_pcb->uc.regs[0] = curr_pcb->pid; 
}

/* ********************************** kernel_brk ********************************** */ 
/*
 * This function is called by mallocs and frees used by the user process.
 */
void kernel_brk(PCB_t* curr_pcb) {

    UserContext* uctxt = &(curr_pcb->uc); 
    void* addr = (void*) UP_TO_PAGE(uctxt->regs[0]); 
    int page = ((int) addr >> PAGESHIFT) - 128; 
    int rc = 0; 

    TracePrintf(1, "curr_pcb.brk page: %d\n", curr_pcb->brk);

    // Need to check if the address is under user text or above stack 
    if ((page <= curr_pcb->lastUserDataPage) || page >= curr_pcb->lastUserStackPage) {
        TracePrintf(1, "Invalid address\n");
        rc = ERROR; 
    } else {
        // Handling proper address
        if (page > curr_pcb->brk) {
            TracePrintf(2, "Page we want to allocate up until: %d\n", page);
            // Allocate pages up to an including page
            for (int i = curr_pcb->brk; i < page; i++) {
                int index = findNextEmptySpace();
                // If there is no available memory, return error 
                if (index == -1) {
                    rc = ERROR; 
                    break; 
                }
                // creating new page entry for allocated page
                pte_t entry; 
                entry.valid = 1; 
                entry.prot = PROT_READ | PROT_WRITE; 
                entry.pfn = index; 
                char* entryAddr = (char*)&(curr_pcb->pageTable[i]); 
                memcpy(entryAddr, &entry, sizeof(pte_t)); 
                TracePrintf(2, "Allocated page %d for brk\n", i); 
            }
        } else if (page < curr_pcb->brk) {
            // Deallocate pages 
            for (int i = page; i < curr_pcb->brk; i++) {
                curr_pcb->pageTable[i].valid = 0;
                TracePrintf(2, "Deallocated page %d in brk\n", i); 
            }
            WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_1); 
        }
        curr_pcb->brk = page;
        TracePrintf(1, "Set current break to page %d\n", curr_pcb->brk);
    }

    // Write to register to flush region 1 page table 
    uctxt->regs[0] = rc;
} 

/* ********************************** kernel_delay ********************************** */ 
/*
 * Called when User Process called Delay.
 * PCB fields goTime and delay need to be adjusted accordingly. 
 */
void kernel_delay(PCB_t* curr_pcb) {

    UserContext* uctxt = &(curr_pcb->uc); 
    int clock_ticks = uctxt->regs[0]; 
    
    if (clock_ticks > 0) {
        TracePrintf(1, "Delaying for %d clock ticks\n", clock_ticks);
        curr_pcb->goTime = clock_ticks;
        curr_pcb->delay = 1;
        PCB_t* next_pcb_p = find_ready_pcb();
        if (addPCB(&blockedHead, curr_pcb) == -1) {
            TracePrintf(1, "Failed to add PCB to ready queue, no delay occurs\n");
            uctxt->regs[0] = ERROR;
            return; 
        } else {
            removePCB(&readyHead, curr_pcb);
        }

        removePCB(&readyHead, next_pcb_p);
        int rc = KernelContextSwitch(KCSwitch, (void*) (curr_pcb), (void *)(next_pcb_p));
        if (rc != 0) {
            TracePrintf(1, "Failed to context switch\n");
            uctxt->regs[0] = ERROR;
            return;
        }
        // return value
        uctxt->regs[0] = 0; 
        runningProcess->delay = 0; // reset delay flag
        TracePrintf(1, "Returning out of kernel_delay\n");
        
        // using region 1 page table for current process
        WriteRegister(REG_PTBR1, (unsigned int) curr_pcb -> pageTable); 
        WriteRegister(REG_PTLR1, (unsigned int) MAX_PT_LEN); 
        // Flush the TLB to remove stale mappings
        WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_1); 

    } else if (clock_ticks == 0) {
        TracePrintf(1, "No delay\n");
        uctxt->regs[0] = 0;
    } else if (clock_ticks < 0) {
        TracePrintf(1, "Invalid clock_ticks\n");
        uctxt->regs[0] = ERROR; 
    }
}

/* ********************************** kernel_fork ********************************** */ 
/*
 * Called when user process calls Fork. 
 */
void kernel_fork(PCB_t *curr_pcb) {

    UserContext* uctxt = &(curr_pcb->uc); 
    PCB_t* parentPCB = curr_pcb;
    
    // creating a child PCB
    KernelContext childKC;
    pte_t* child_pageTable = malloc(sizeof(pte_t) * MAX_PT_LEN);
    if (child_pageTable == NULL) {
        TracePrintf(1, "Failed to allocate memory for child pagetable\n");
        uctxt->regs[0] = ERROR; 
        return;
    }

    // Creating the page table for child 
    for (int i = 0; i < MAX_PT_LEN; i++) {
        if (curr_pcb->pageTable[i].valid == 1) {
            child_pageTable[i].valid = 1;
            child_pageTable[i].prot = curr_pcb->pageTable[i].prot;
            child_pageTable[i].pfn = findNextEmptySpace();
            if (child_pageTable[i].pfn == -1) {
                uctxt->regs[0] = ERROR;
                return;
            }
        } else {
            child_pageTable[i].valid = 0;
        }
    }

    // temporary mapping magic
    for (int i = 0; i < MAX_PT_LEN; i++) {
        if (curr_pcb->pageTable[i].valid == 1) {
            pte_t temp_entry;
            int temp_frame_mapping = child_pageTable[i].pfn;
            temp_entry.valid = 1;
            temp_entry.prot = PROT_READ | PROT_WRITE;
            temp_entry.pfn = temp_frame_mapping;

            // find empty space in region parent pt to insert (index)
            int unused_page =  find_empty_page(kernelPageTable);

            // not enough empty page entries to store temporary mapping
            if (unused_page == -1) {
                uctxt->regs[0] = ERROR;
                return;
            }

            // temporary mapping
            kernelPageTable[unused_page] = temp_entry;
            memcpy((void*)(unused_page * PAGESIZE), (void*) ((i * PAGESIZE) + 128*PAGESIZE), PAGESIZE);

            // remove temporary mapping
            kernelPageTable[unused_page].valid = 0;
            WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);
        }
    }

    // creating child PCB 
    PCB_t* childPCB = initializePCB(*uctxt, childKC, child_pageTable); 
    if (childPCB == NULL) {
        TracePrintf(1, "Failed to initialize child PCB\n");
        uctxt->regs[0] = ERROR; 
        return;
    }

    // updating information about child PCB
    childPCB->brk = curr_pcb->brk;
    childPCB->lastUserDataPage = curr_pcb->lastUserDataPage;
    childPCB->lastUserStackPage = curr_pcb->lastUserStackPage;

    /*
        Handling parent case in Fork()
    */
    int parent_pid = parentPCB->pid;
    if (addPCB(&(curr_pcb->childHead), childPCB) == -1) {
        TracePrintf(1, "Failed to add child PCB to parent\n");
        childPCB->status = ERROR; 
        kernel_exit(childPCB); 
        uctxt->regs[0] = ERROR; 
        return;
    }
    childPCB -> parent = parentPCB; // mark parent for child
    if (addPCB(&readyHead, childPCB) == -1) {
        TracePrintf(1, "Failed to add child PCB to ready queue\n");
        childPCB->status = ERROR;
        kernel_exit(childPCB); 
        uctxt->regs[0] = ERROR; 
        return; 
    }
    uctxt->regs[0] = childPCB->pid; // return value for parent

    // cloning parent's kernel stack into child PCB
    int rc = KernelContextSwitch(KCCopy,childPCB, NULL);  
    if (rc == -1) {
        uctxt->regs[0] = ERROR;
        return; 
    }

    if (runningProcess->pid != parent_pid) {
        // using region 1 page table for current process
        WriteRegister(REG_PTBR1, (unsigned int) runningProcess -> pageTable); 
        WriteRegister(REG_PTLR1, (unsigned int) MAX_PT_LEN); 
        // Flush the TLB to remove stale mappings
        WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_1); 

        /*
            Handling child case in Fork()
        */
        runningProcess->uc.regs[0] = 0; // return value
    }
}

/* ********************************** kernel_exec ********************************** */ 
/*
 * Called when user process calls Exec. 
 */
void kernel_exec(PCB_t *curr_pcb) {

    TracePrintf(1, "In kernel_exec\n");
    UserContext* uctxt = &(curr_pcb->uc); 

    // gathering the arguments passed by the library call
    char* filename = (char*) uctxt->regs[0]; 
    char** arguments = (char**) uctxt->regs[1];
    int rc;

    if (arguments != NULL) {
        int i = 0;
        while (arguments[i] != NULL) {
            TracePrintf(2, "Argument %d: %s\n", i, arguments[i]);
            i++;
        }
        rc = LoadProgram(filename, arguments, curr_pcb);
        if (rc != 0) {
            TracePrintf(1, "Failed to load program\n");
            uctxt->regs[0] = ERROR;
            return;
        }
    } else {
        char *args[] = {filename};
        rc = LoadProgram(filename, args, curr_pcb);
        if (rc != 0) {
            TracePrintf(1, "Failed to load program\n");
            uctxt->regs[0] = ERROR;
            return;
        }
    }

    uctxt->regs[0] = 0;
}

/* ********************************** kernel_exit ********************************** */ 
/*
 * Called when user process calls Exit. 
 * If init calls exit, the kernel will halt. 
 */ 
void kernel_exit(PCB_t *curr_pcb) {

    TracePrintf(1, "In the kernel_exit handler\n");

    if (curr_pcb->pid == 0) {
        TracePrintf(1, "Init process exited.\n");
        Halt();
    }
    UserContext* uctxt = &(curr_pcb->uc); 

    int exit_status;
    if (curr_pcb->status == ERROR) {
        TracePrintf(2, "Error encountered\n");
        exit_status = ERROR;
    } else {
        exit_status = uctxt->regs[0]; 
    }

    TracePrintf(1, "Exit status: %d\n", exit_status);

    curr_pcb -> status = exit_status;

    // Calling helper function 
    helper_retire_pid(curr_pcb->pid); 

    // all resoures used by the calling process will be freed
    for (int i = 0; i < MAX_PT_LEN; i++) {
        if (curr_pcb->pageTable[i].valid == 1) {
            int frame_to_free = curr_pcb->pageTable[i].pfn;
            curr_pcb->pageTable[i].valid = 0;
            free_frames[frame_to_free] = 0;
            WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_1); 
        }
    }
    free(curr_pcb->pageTable);

    // indicate parent has exited to its children
    PCB_node_t* current = curr_pcb->childHead;
    while (current != NULL) {
        current->pcb->parent = NULL;
        current = current->next;
    }

    // if no parent, free the PCB
    if (curr_pcb->parent == NULL) {
        free(curr_pcb);
    } else {
        // moving to the defunct queue
        if (addPCB(&defunctHead, curr_pcb) == -1) {
            TracePrintf(1, "Failed to add PCB to defunct queue\n");
        }
    }

    // context switch into a ready PCB
    PCB_t* next_pcb_p = find_ready_pcb();
    removePCB(&readyHead, next_pcb_p);
    int rc = KernelContextSwitch(KCSwitch, (void*) (curr_pcb), (void *)(next_pcb_p));
    if (rc != 0) {
        TracePrintf(1, "Error in KernelContextSwitch\n");
    }
}

/* ********************************** kernel_wait ********************************** */ 
/*
* Called when user process calls Wait. 
*/ 
void kernel_wait(PCB_t *curr_pcb) {

    TracePrintf(1, "In the kernel_wait handler\n");
    UserContext* uctxt = &(curr_pcb->uc); 
    int *status_ptr = (int*) uctxt->regs[0]; 

    // no children left, return with error
    if (curr_pcb->childHead == NULL) {
        uctxt->regs[0] = ERROR;
        return;
    }

    // look through defunct queue to see if there is a process that has current process as a parent
    for (PCB_node_t* curr = defunctHead; curr != NULL; curr = curr->next) {
        if (curr->pcb->parent == curr_pcb) {
            *status_ptr = curr->pcb->status;
            uctxt->regs[0] = curr->pcb->pid;
            removePCB(&defunctHead, curr->pcb);
            free(curr->pcb);
            return;
        }
    }

    // calling process blocks until its next child calls exit or is aborted
    curr_pcb->wait = 1;
    if (addPCB(&blockedHead, curr_pcb) == -1) {
        TracePrintf(1, "Failed to add PCB to blocked queue\n");
        uctxt->regs[0] = ERROR;
        return;
    }
    PCB_t* next_pcb_p = find_ready_pcb();
    removePCB(&readyHead, next_pcb_p);
    int rc = KernelContextSwitch(KCSwitch, (void*) (curr_pcb), (void *)(next_pcb_p));
    if (rc != 0) {
        TracePrintf(1, "Error in KernelContextSwitch\n");
    }

    // using region 1 page table for current process
    WriteRegister(REG_PTBR1, (unsigned int) runningProcess -> pageTable); 
    WriteRegister(REG_PTLR1, (unsigned int) MAX_PT_LEN); 
    // Flush the TLB to remove stale mappings
    WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_1); 

    // call returns with the exit information of that child
    curr_pcb->wait = 0;
    for (PCB_node_t* curr = defunctHead; curr != NULL; curr = curr->next) {
        if (curr->pcb->parent == curr_pcb) {
            *status_ptr = curr->pcb->status;
            uctxt->regs[0] = curr->pcb->pid;
            removePCB(&defunctHead, curr->pcb);
            free(curr->pcb);
            return;
        }
    }
}
