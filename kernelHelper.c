/*
 * kernelHelper.c 
 *
 * This file contains helper functions used by the kernel. 
 */

#include <hardware.h> 
#include <ylib.h> 
#include <yalnix.h>
#include <ykernel.h>
#include <kernel_func.h>
#include <load_program.h> 

/****************************** findNextEmptySpace ******************************/ 
/*
 * findNextEmptySpace finds the next empty space in the free_frames array. 
 * It merely iterates through the array, checking for a valid bit of 0. It returns 0 if successful, -1 if not. 
 */
int findNextEmptySpace() {
    
    // If we exit for loop without changing the index, it means we found nothing and it is full. 
    int index = -1;

    for (int i = 0; i < free_frames_len; i++) {
        // If we found a free frame, we can return the index 
        if (free_frames[i] == 0) {
            index = i;
            // Mark free frame as used
            free_frames[i] = 1;
            break;
        } 
    }

    // If no free frame is found, we should context switch to idlePCB and remove the other processes from ready queue. 
    if (index == -1) {
        TracePrintf(1, "Error: No free frames available\n");
        PCB_node_t* curr = readyHead; 
        while (curr != NULL) {
            removePCB(&readyHead, curr->pcb);
            curr = curr->next; 
        }
        KernelContextSwitch(KCSwitch, (void*) (runningProcess), (void*) idlePCB);
    }
    return index; 
}

/****************************** find_empty_page ******************************/ 
/*
 * find_empty_page finds the next empty page in the page table. 
 * It merely iterates through the page table, checking for a valid bit of 0. It returns the index if successful, -1 if not. 
 */ 
int find_empty_page(pte_t* pageTable) {
    
    // If we exit for loop without changing the index, it means we found nothing and it is full. 
    int index = -1;

    for (int i = _first_kernel_text_page; i < MAX_PT_LEN; i++) {
        if (pageTable[i].valid == 0) {
            index = i;
            break;
        } 
    }

    return index; 
}

/* *********************************** setupUserPageTable *********************************** */
/*
 * setupUserPageTable initializes a page table for a new process. 
 * It sets all the valid bits to 0 and the protection bits to 0. 
 * It returns a pointer to the page table. 
 */ 
pte_t* setupUserPageTable() {

    pte_t* pageTable = malloc(sizeof(pte_t) * MAX_PT_LEN);
    if (pageTable == NULL) {
        TracePrintf(1, "Could not allocate memory for new page table\n");
        return NULL;
    }

    for (int i = 0; i < MAX_PT_LEN; i++) {
        pageTable[i].valid = 0;
        pageTable[i].prot = 0;
        pageTable[i].pfn = 0;
    }
    return pageTable; 
}

/****************************** add_to_region0_pageTable ******************************/ 
/*
 * This is a helper function to add a new entry to the kernel page table with permissions and index given as arguments. 
 */
void* add_to_region0_pageTable(int index, int permissions) {

    // Check if the index and permissions are valid 
    if (index >= MAX_PT_LEN || index < 0 || permissions < 0 || permissions > 7) {
        TracePrintf(1, "Error: Invalid arguments!\n");
        return NULL;
    } 

    // Declaring a new page table entry 
    TracePrintf(2, "Adding entry to kernel page table at index %d\n", index);  
    pte_t entry; 

    // Setting the values of the page table entry
    entry.valid = 1; 
    entry.prot =  permissions; 

    // Depending on if virtual memory is enabled, we need to set the pfn differently. 
    int virtualMem = ReadRegister(REG_VM_ENABLE);
    if (virtualMem == 0) {
        entry.pfn = index; 
        // Mark the frame as used 
        free_frames[index] = 1; 

        // Add the entry to the global page table 
        TracePrintf(2, "Adding translation for index %d to %d\n", index, entry.pfn); 
        kernelPageTable[index] = entry; 
    } else {
        int freeFrame = findNextEmptySpace();
        if (freeFrame == -1) {
            return NULL;
        }
        entry.pfn = freeFrame; 
        
        // Mark the frame as used 
        free_frames[freeFrame] = 1;

        // Add the entry to the page table stored at the memory address (page table currently in memory)
        TracePrintf(2, "Adding translation for index %d to %d\n", index, entry.pfn); 
        char* entryAddr = (char*)&(kernelPageTable[index]);
        memcpy(entryAddr, &entry, sizeof(pte_t));
    }
}

/* ********************************** KCCopy ********************************** */
/*
 * Helper function to copy the current kernel context of init process into the idlePCB. 
 */ 
KernelContext *KCCopy(KernelContext *kc_in, void *new_pcb_p_void, void *not_used) {

    // check if the arguments are valid 
    if (kc_in == NULL || new_pcb_p_void == NULL) {
        TracePrintf(1, "Error: KCCopy called with NULL arguments\n");
        return NULL;
    }

    PCB_t *new_pcb_p = (PCB_t *)new_pcb_p_void;
    
    // copy the current KernelContext into idlePCB
    new_pcb_p->kc = *kc_in;

    // obtain physical frame locations of new_pcb_p
    int new_kernel_stack_first = new_pcb_p->firstKernelStackFrame;
    int new_kernel_stack_second = new_pcb_p->secondKernelStackFrame;
    TracePrintf(2, "frames I will copy to are %d-%d\n", new_kernel_stack_first, new_kernel_stack_second);

    // create a temporary page in kernel 
    int temporary_mapping_page = (KERNEL_STACK_BASE >> PAGESHIFT) - 1;
    
    pte_t entry_kernel_first; 
    pte_t entry_kernel_second; 
    // adding temporary mapping for first kernel page
    entry_kernel_first.valid = 1; 
    entry_kernel_first.prot =  PROT_READ | PROT_WRITE; 
    entry_kernel_first.pfn = new_kernel_stack_first;
    kernelPageTable[temporary_mapping_page] = entry_kernel_first; 

    // adding temporary mapping for second kernel page
    entry_kernel_second.valid = 1; 
    entry_kernel_second.prot =  PROT_READ | PROT_WRITE; 
    entry_kernel_second.pfn = new_kernel_stack_second;
    kernelPageTable[temporary_mapping_page - 1] = entry_kernel_second; 

    // writing to physical frames of new_pcb_p (copying current process kernel stack)
    int total_kstack_pages = (KERNEL_STACK_LIMIT >> PAGESHIFT) - (KERNEL_STACK_BASE >> PAGESHIFT);
    for (int i = 0; i < total_kstack_pages; i++) {
        memcpy((void*)((temporary_mapping_page - i) << PAGESHIFT), (void*)(((KERNEL_STACK_LIMIT >> PAGESHIFT) - i - 1) << PAGESHIFT), PAGESIZE);
    }

    // remove temporary mapping
    for (int i = 0; i < total_kstack_pages; i++) {
        kernelPageTable[temporary_mapping_page - i].valid = 0;
    }

    // flushing the kernel space
    WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);
    return kc_in;
}

/* ********************************** KCSwitch ********************************** */ 
/*
 * KCSwitch is a helper function to switch the kernel context from current pcb to the next pcb. 
 */ 
KernelContext *KCSwitch(KernelContext *kc_in, void *curr_pcb_p_void, void *next_pcb_p_void) {

    // check if the arguments are valid 
    if (kc_in == NULL || curr_pcb_p_void == NULL || next_pcb_p_void == NULL) {
        TracePrintf(1, "Error: KCSwitch called with NULL arguments\n");
        return NULL;
    } 

    // cast PCB arguments as type PCB_t
    PCB_t *curr_pcb_p = (PCB_t *)curr_pcb_p_void;
    PCB_t *next_pcb_p = (PCB_t *)next_pcb_p_void;

    // copy the current kernel context into the old PCB
    curr_pcb_p->kc = *kc_in;

    // change the Region 0 kernel stack mappings to those for the new PCB
    kernelPageTable[(KERNEL_STACK_LIMIT >> PAGESHIFT) - 1].pfn = next_pcb_p->firstKernelStackFrame;
    kernelPageTable[KERNEL_STACK_BASE >> PAGESHIFT].pfn = next_pcb_p->secondKernelStackFrame;
    TracePrintf(1, "In KCSwitch\n");

    // Flush the TLB to remove stale mappings (kernel stack)
    WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0); 

    runningProcess = next_pcb_p; 

    // return a pointer to the KernelContext in the new PCB
    return &(next_pcb_p->kc);
}

/* *********************************** initializePCB *********************************** */ 
/*
 * initializePCB initializes a new PCB with the given user context, kernel context, and page table. 
 */
PCB_t* initializePCB(UserContext uc, KernelContext kc, pte_t* pageTable) {

    if (pageTable == NULL) {
        TracePrintf(1, "Error: initializePCB called with NULL page table\n");
        return NULL;
    } 

    PCB_t *pcb = malloc(sizeof(PCB_t)); 
    if (pcb == NULL) {
        TracePrintf(1, "Could not allocate memory for new PCB\n");
        return NULL;
    }

    pcb->uc = uc; 
    pcb->pid = helper_new_pid(pageTable); 
    pcb->status = 0; 
    pcb->kc = kc; 
    pcb->pageTable = pageTable; 
    pcb->firstKernelStackFrame = findNextEmptySpace();
    pcb->secondKernelStackFrame = findNextEmptySpace();

    if (pcb->firstKernelStackFrame == -1 || pcb->secondKernelStackFrame == -1) {
        TracePrintf(1, "Error: Could not find empty space for kernel stack\n");
        return NULL;
    }

    pcb->parent = NULL;
    pcb->childHead = NULL;
    pcb->goTime = 0;
    pcb->delay = 0;
    pcb->wait = 0;
    pcb->read = 0;
    pcb->transmit = 0;
    pcb->terminalWrite = 0;
    pcb->terminalRead = 0; 
    pcb->tty_id = -1;
    pcb->terminal_transmit_busy = -1;
    pcb->lock_id = 0; 
    pcb->lock = 0; 
    return pcb; 
}

/* ********************************** addPCB ********************************** */ 
/*
 * addPCB adds a PCB to the head of an arbitary queue. 
 */ 
int addPCB(PCB_node_t **head, PCB_t *pcb) {

    // PID 1 belongs to the idlePCB. We don't want to be adding this to any queue. 
    if (pcb->pid == 1) {
        return 0; 
    }

    if (head == NULL || pcb == NULL) {
        TracePrintf(1, "Error: addPCB called with NULL arguments\n");
        return -1;
    }

    PCB_node_t* new_node = (PCB_node_t *)malloc(sizeof(PCB_node_t));
    if (new_node == NULL) {
        TracePrintf(1, "Could not allocate new PCB\n");
        return -1;
    }

    new_node -> pcb = pcb;
    new_node -> next = *head;
    *head = new_node;
    
    return 0; 
}

/* ********************************** addPipe ********************************** */ 
/*
 * addPipe adds a pipe to the head of the pipe queue. 
 */
int addPipe(pipe_node_t **head, pipe_t *pipe) {

    if (head == NULL || pipe == NULL) {
        TracePrintf(1, "Error: addPipe called with NULL arguments\n");
        return -1;
    }

    pipe_node_t* new_node = (pipe_node_t *)malloc(sizeof(pipe_node_t));
    if (new_node == NULL) {
        TracePrintf(1, "Could not allocate new pipe\n");
        return -1; 
    }

    new_node -> pipe = pipe;
    new_node -> next = *head;
    *head = new_node;

    return 0;  
}

/* ********************************** removePCB ********************************** */ 
/*
 * removePCB removes a PCB from the end of an arbitary queue. This is for fairness of round robin scheduling.  
 */
PCB_t* removePCB(PCB_node_t **head, PCB_t *pcb) {
    
    PCB_node_t* curr = *head;
    PCB_node_t* prev = NULL;

    while (curr != NULL) {
        if (curr->pcb == pcb) {
            if (prev == NULL) {
                *head = curr->next;
            } else {
                prev->next = curr->next;
            }
            return curr->pcb; 
        }
        prev = curr;
        curr = curr->next;
    }

    return NULL; 
}

/* ********************************** find_ready_pcb ********************************** */ 
/*
 * find_ready_pcb finds the next ready PCB in the ready queue. If none exist, idlePCB is returned.  
 */
PCB_t* find_ready_pcb() {

    if (readyHead == NULL) {
        return idlePCB; 
    }

    PCB_node_t* curr = readyHead; 

    while (curr->next != NULL) {
        curr = curr->next; 
    }

    return curr->pcb; 
}



/* ********************************** Helper Functions Debugging ********************************** */ 

/* ********************************** printKernelTable ********************************** */ 
void printKernelTable() {
    for (int i = 0; i < MAX_PT_LEN; i++) {
        char* entryAddr = (char*)(&kernelPageTable[i]); 
        pte_t entry; 
        memcpy(&entry, entryAddr, sizeof(pte_t)); 
        TracePrintf(1, "Page Table Entry %d : %d, valid: %d, prot:%d\n", i, entry.pfn, entry.valid, entry.prot); 
    }
}

void displayReadyQueue() {
    PCB_node_t* curr = readyHead; 
    while (curr != NULL) {
        TracePrintf(1, "Ready Queue: %d\n", curr->pcb->pid);
        curr = curr->next; 
    }
}

void displayBlockedQueue() {
    PCB_node_t* curr = blockedHead; 
    while (curr != NULL) {
        TracePrintf(1, "Blocked Queue: %d\n", curr->pcb->pid);
        curr = curr->next; 
    }
}
