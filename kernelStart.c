/*
 * kernelStart.c 
 *
 * Yalnix boots into this file so this contains functions that it will need to begin the OS.
 * KernelStart initializes the kernel data structures and the first user processes, while SetKernelBrk manages 
allocating more memory in the kernel. 
*/

#include <hardware.h>
#include <ylib.h>
#include <kernel_func.h>
#include <yalnix.h>
#include <ykernel.h> 
#include <load_program.h>

/* ********************************** Global Variables ********************************** */
int* free_frames;
int free_frames_len; 
pte_t kernelPageTable[MAX_PT_LEN];
void* vector[TRAP_VECTOR_SIZE];
int curr_break_page;
int curr_break_addr;
PCB_t *runningProcess = NULL;
PCB_node_t *readyHead = NULL; 
PCB_node_t *blockedHead = NULL; 
PCB_node_t *defunctHead = NULL;
pipe_node_t *pipeHead = NULL;
PCB_t* idlePCB;
int total_pipes = -1;
int locks_num = 2; 
lock_node_t *lockHead = NULL;
cvar_node_t *cvarHead = NULL; 
int cvars_num = 1; 
terminal_t* terminal_array[NUM_TERMINALS];

/* ********************************** KernelStart ********************************** */ 
/* 
 * KernelStart sets up the kernel data structures and initializes the first user process. If any errors occur, we abort. 
 */
void KernelStart (char* cmd_args[], unsigned int pmem_size, UserContext *uctxt) {

    TracePrintf(1, "Entering KernelStart\n");

    if (uctxt == NULL || cmd_args == NULL) {
        helper_abort("Error: KernelStart given NULL arguments\n"); 
    }

    // If no program arguments are given, default to init 
    char* initial_prog_name;
    if (cmd_args[0] == NULL) {
        initial_prog_name = "test/init";
    } else {
        initial_prog_name = cmd_args[0];
    }
    
    // Initialize the tracking of free frames  
    free_frames = malloc((pmem_size / PAGESIZE) * sizeof(int));
    if (free_frames == NULL) {
        helper_abort("Error: malloc failed in KernelStart\n"); 
    }
    free_frames_len = pmem_size / PAGESIZE;

    // Mark frames as initially free
    for (int i = 0; i < free_frames_len; i++) {
        free_frames[i] = 0; 
    }

    // Assigning memory for Kernel Text Segment (Readable and Executable)
    for (int i = _first_kernel_text_page; i < _first_kernel_data_page; i++) {
        if (add_to_region0_pageTable(i, (PROT_READ | PROT_EXEC)) == NULL) {
            helper_abort("Error: add_to_region0_pageTable failed in KernelStart\n"); 
        }
    }

    // Assigning memory for Kernel Data Segment & initial Kernel Heap Segment
    for (int i = _first_kernel_data_page; i < _orig_kernel_brk_page; i++) {
        if (add_to_region0_pageTable(i, (PROT_READ | PROT_WRITE)) == NULL) {
            helper_abort("Error: add_to_region0_pageTable failed in KernelStart\n"); 
        }
    }

    // Assign 2 pages to the Kernel Stack
    for (int i = KERNEL_STACK_BASE >> PAGESHIFT; i < (KERNEL_STACK_LIMIT >> PAGESHIFT) ; i++) {
        if (add_to_region0_pageTable(i, (PROT_READ | PROT_WRITE)) == NULL) {
            helper_abort("Error: add_to_region0_pageTable failed in KernelStart\n"); 
        }
    }

    // Setting up the interrupt vector table
    vector[TRAP_CLOCK] = &handle_trap_clock;
    vector[TRAP_KERNEL] = &handle_trap_kernel;
    vector[TRAP_ILLEGAL] = &handle_trap_illegal;
    vector[TRAP_MEMORY] = &handle_trap_memory;
    vector[TRAP_MATH] = &handle_trap_math;
    vector[TRAP_TTY_RECEIVE] = &handle_trap_tty_receive; 
    vector[TRAP_TTY_TRANSMIT] = &handle_trap_tty_transmit; 

    for (int i = 0; i < TRAP_VECTOR_SIZE; i++) {
        if (i != TRAP_CLOCK && i != TRAP_KERNEL && i != TRAP_ILLEGAL && i != TRAP_MEMORY && i != TRAP_MATH && i != TRAP_TTY_RECEIVE && i != TRAP_TTY_TRANSMIT) {
            vector[i] = &other_trap;
        }
    }

    // curr_break_page is where the brk resides in kernel mode 
    curr_break_page = _orig_kernel_brk_page;
    curr_break_addr = _orig_kernel_brk_page << PAGESHIFT;

    // Writing the location of the kernel page table
    WriteRegister(REG_PTBR0, (unsigned int) (&kernelPageTable)); 
    WriteRegister(REG_PTLR0, (unsigned int) MAX_PT_LEN); 

    // Enabling virtual memory
    WriteRegister(REG_VM_ENABLE, (unsigned int) 1); 

    // Specifying the location of the interrupt vector table
    WriteRegister(REG_VECTOR_BASE, (unsigned int) &vector);

    // initialize array of terminal structs
    for (int i = 0; i < NUM_TERMINALS; i++) {

        terminal_t *terminal = malloc(sizeof(terminal_t));
        if (terminal == NULL) {
            helper_abort("Error: malloc failed in KernelStart\n"); 
        }
        terminal->tty_id = i;
        terminal->pos_of_next_char = 0;
        // initialize buffer to null
        terminal->buffer = (char*) malloc((sizeof(char) * TERMINAL_BUFFER_SIZE)); 
        if (terminal->buffer == NULL) {
            helper_abort("Error: malloc failed in KernelStart\n"); 
        }
        for (int j = 0; j < TERMINAL_BUFFER_SIZE; j++) {
            terminal->buffer[j] = '\0';
        }
        terminal->terminal_buffer_size = TERMINAL_BUFFER_SIZE; 
        terminal_array[i] = terminal;
    }

    // Create the region 1 page table for idlePCB and initPCB 
    pte_t* initPageTable = setupUserPageTable(); 
    pte_t* idlePageTable = setupUserPageTable(); 

    if (initPageTable == NULL || idlePageTable == NULL) {
        helper_abort("Error: setupUserPageTable failed in KernelStart\n"); 
    }

    // Setting up the initPCB (same kernel stack frames as initial frames)
    KernelContext initKernelContext;
    PCB_t* initPCB = initializePCB(*uctxt, initKernelContext, initPageTable);
    if (initPCB == NULL) {
        helper_abort("Error: initializePCB failed in KernelStart\n"); 
    }
    free_frames[initPCB->firstKernelStackFrame] = 0; 
    free_frames[initPCB->secondKernelStackFrame] = 0;
    initPCB -> firstKernelStackFrame = (KERNEL_STACK_LIMIT >> PAGESHIFT) - 1;
    initPCB -> secondKernelStackFrame = KERNEL_STACK_BASE >> PAGESHIFT;
    runningProcess = initPCB; 
    
    // Loading initial program into initPCB
    WriteRegister(REG_PTBR1, (unsigned int) initPageTable); 
    WriteRegister(REG_PTLR1, (unsigned int) MAX_PT_LEN); 
    int rc = LoadProgram(initial_prog_name, cmd_args, initPCB);

    if (rc != 0) {
        helper_abort("Error: LoadProgram failed in KernelStart\n"); 
    }

    // Setting up the idlePCB
    KernelContext idleKernelContext; 
    idlePCB = initializePCB(*uctxt, idleKernelContext, idlePageTable);
    if (idlePCB == NULL) {
        helper_abort("Error: initializePCB failed in KernelStart\n"); 
    }

    // Loading test program into into idlePCB
    WriteRegister(REG_PTBR1, (unsigned int) idlePageTable); 
    WriteRegister(REG_PTLR1, (unsigned int) MAX_PT_LEN); 
    WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_1); 

    rc = LoadProgram("test/idle", cmd_args, idlePCB);
    if (rc != 0) {
        helper_abort("Error: LoadProgram failed in KernelStart\n"); 
    }
    
    // cloning initPCB's kernel stack into idlePCB
    rc = KernelContextSwitch(KCCopy, idlePCB, NULL); 
    if (rc != 0) {
        helper_abort("Error: KernelContextSwitch failed in KernelStart\n"); 
    }

    // loading in the page tables of the currently running pcb
    WriteRegister(REG_PTBR1, (unsigned int) runningProcess->pageTable); 
    WriteRegister(REG_PTLR1, (unsigned int) MAX_PT_LEN);
    WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_1); 

    // specifying user context for currently running pcb
    *uctxt = runningProcess->uc; 

    TracePrintf(1, "Leaving KernelStart\n"); 
}

/* ******************************** SetKernelBrk ******************************** */ 
/*
 * SetKernelBrk sets the current break address to the address given. 
 */
int SetKernelBrk (void *addr) {

    TracePrintf(1, "Entering SetKernelBrk\n");

    // Which page is the address in
    long addr_page = ((long) addr & PAGEMASK) / PAGESIZE;

    TracePrintf(2, "new_addr: %ld\n", (long) addr);
    TracePrintf(2, "addr_page: %ld\n", addr_page);

    // VM is not enabled
    if (ReadRegister(REG_VM_ENABLE) == 0) {
        // everything is interpreted as a physical address
        if (addr_page >= curr_break_page) {

            // add new page mapping(s) to the heap section
            for (int i = (int) (curr_break_page); i <= (int) (addr_page); i++) {
                TracePrintf(2, "Adding new page: %d\n", i);
                if (add_to_region0_pageTable(i, (PROT_READ | PROT_WRITE)) == NULL) {
                    return ERROR; 
                }
            }
            // first location after new heap
            curr_break_page = addr_page + 1;
            TracePrintf(2, "curr_break_page now @ page %d\n", curr_break_page);
        } else {
            // should not be shrinking heap if VM is not enabled
            TracePrintf(1, "Error: Shrink heap not allowed when VM is not enabled\n"); 
            return ERROR; 
        }
    }
    // VM is enabled
    else {
        // if the address is at the same or higher page than the current brk_page
        if (addr_page >= curr_break_page) {

            if (addr_page >= (KERNEL_STACK_BASE >> PAGESHIFT) - 1) {
                TracePrintf(1, "You have grown your heap near, into, or past the stack\n");
                return ERROR; 
            }
            
            // add new page mapping(s) to the heap section
            for (int i = (int) (curr_break_page); i <= (int) (addr_page); i++) {
                TracePrintf(2, "Adding new page: %d\n", i);
                if (add_to_region0_pageTable(i, (PROT_READ | PROT_WRITE)) == NULL) {
                    return ERROR; 
                }
            }
            // setting curr_break_page to first unused page after increasing heap
            curr_break_page = addr_page + 1;
            TracePrintf(2, "Set new brk to %d\n", curr_break_page);
        }
        // address is at a lower page than the current brk_page
        else {

            if (addr_page <= _orig_kernel_brk_page) {
                TracePrintf(1, "You are setting your brk too low\n");
                return ERROR; 
            }
            // remove page mapping(s) from heap section
            for (int i = (int) (addr_page); i < (int) (curr_break_page); i++) {
                TracePrintf(2, "Removing page %d from heap\n", i);

                char* entryAddr = (char*)&(kernelPageTable[i]);
                pte_t entry;
                memcpy(&entry, entryAddr, sizeof(pte_t));
                TracePrintf(2, "Virtual Address-Physical Address Mapping: %d-%d\n", i, entry.pfn);

                // clearing the physical memory at that location
                bzero((void*)(i*PAGESIZE), PAGESIZE);
                
                // making the entry invalid
                entry.valid = 0;
                memcpy(entryAddr, &entry, sizeof(pte_t));
                free_frames[i] = 0;
            }
            // Flush the TLB to remove stale mappings
            WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0); 

            // setting the new brk after removing frames
            curr_break_page = addr_page;
            TracePrintf(1, "Set new brk to %d\n", curr_break_page);
        }
    }
    // updating the current break address
    curr_break_addr = (int)addr;
    return 0;
}
