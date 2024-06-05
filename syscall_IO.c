/*
* syscall_IO.c 
*
* This file contains the implementation of the system calls for IO operations.
*/

#include <hardware.h>
#include <kernel_func.h>
#include <yalnix.h>
#include <ykernel.h>
#include <load_program.h>
#include <syscalls.h>

/* ********************************** kernel_tty_read ********************************** */ 
/*
 * This function is called when the kernel receives a TtyRead system call. 
 * If there is no input in the terminal buffer, the process is blocked until a new line of input is available. 
 * It manages the kernel buffer that takes in the input from the terminal. 
 */
void kernel_tty_read(PCB_t *curr_pcb) {

    TracePrintf(1, "In the kernel_tty_read handler\n");
    UserContext* uctxt = &(curr_pcb->uc); 
    int tty_id = (int) uctxt->regs[0]; 
    void* buffer = (void *) uctxt->regs[1];
    int len = (int) uctxt->regs[2]; 
    terminal_t* terminal = terminal_array[tty_id];

    // If the terminal position of the next character to read is 0, it suggests that there is no input in the terminal buffer. 
    if (terminal->pos_of_next_char == 0) {
        // Nothing in the terminal, block until new line of input available 
        PCB_t* next_pcb_p = find_ready_pcb(); 
        if (addPCB(&blockedHead, curr_pcb) == -1) {
            TracePrintf(1, "Error adding PCB to blocked list\n");
            uctxt->regs[0] = ERROR;
            return; 
        } else {
            removePCB(&readyHead, next_pcb_p); 
        }

        // Note that this PCB is waiting 
        curr_pcb->terminalRead = 1;
        curr_pcb->tty_id = tty_id;

        // Switching to next process that is ready 
        int rc = KernelContextSwitch(KCSwitch, (void*) (curr_pcb), (void *)(next_pcb_p)); 
    }

    // Using region 1 page table for current process 
    WriteRegister(REG_PTBR1, (unsigned int) curr_pcb -> pageTable); 
    WriteRegister(REG_PTLR1, (unsigned int) MAX_PT_LEN); 
    WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_1); 

    // After every read, we will shift the contents of the buffer up by the number of bytes read. 
    // First, we must find the actual length of the line we can read 

    int actualLength = 1; 
    while ((terminal->buffer[actualLength - 1] != '\n') && actualLength < len) {
        actualLength++; 
    }
    
    // Copy the line from the terminal buffer to the user buffer 
    memcpy(buffer, terminal->buffer, actualLength);

    // Shift the contents of the terminal buffer up by the number of bytes read 
    memmove(terminal->buffer, terminal->buffer + actualLength, terminal->pos_of_next_char - actualLength);
    for (int i = terminal->pos_of_next_char - actualLength; i < terminal->pos_of_next_char; i++) {
        terminal->buffer[i] = '\0';
    }
    terminal->pos_of_next_char -= actualLength; 

    // Return number of bytes written 
    uctxt->regs[0] = actualLength; 
} 

/* ********************************** kernel_tty_write ********************************** */ 
/*
 * This function is called when the kernel receives a TtyWrite system call. 
 * If the terminal is busy, the process is blocked until the terminal is ready. 
 * It calls a TtyTransmit, but blocks the process until the transmit is complete. 
 */ 
void kernel_tty_write(PCB_t *curr_pcb) {

    TracePrintf(1, "In the kernel_tty_write handler\n");

    // get the user context
    UserContext* uctxt = &(curr_pcb->uc);

    // get the arguments from the user context
    int tty_id = uctxt->regs[0];
    char* user_buf = (char*) uctxt->regs[1];
    int user_len = uctxt->regs[2];
    terminal_t *terminal = terminal_array[tty_id];
    
    curr_pcb->tty_id = tty_id;
    // currently we are not waiting on anything
    curr_pcb->terminalWrite = 0;
    curr_pcb->transmit = 0;
    curr_pcb->terminal_transmit_busy = 0;

    TracePrintf(1, "We will transmit bytes to terminal with tty_id: %d!\n", tty_id);

    while (user_len > 0) {
        // terminal is handling transmit from other process or someone else will write to the terminal and is on the ready queue
        if (terminal->terminal_status == 1) {
            TracePrintf(1, "Terminal is busy. We will block the current process with pid: %d\n", curr_pcb->pid); 

            // Block the process and wait for the terminal to be ready
            PCB_t* next_pcb_p = find_ready_pcb();
            if (addPCB(&blockedHead, curr_pcb) == -1) {
                TracePrintf(1, "Error adding PCB to blocked list\n");
                uctxt->regs[0] = ERROR;
                return;
            } else {
                removePCB(&readyHead, next_pcb_p);
            }

            // Indicate that the pcb is waiting for the terminal to be ready 
            curr_pcb->terminal_transmit_busy = 1; 
            curr_pcb->terminalWrite = 1; 
            curr_pcb->tty_id = tty_id; 

            // Switch to a new process
            int rc = KernelContextSwitch(KCSwitch, (void*) (curr_pcb), (void *)(next_pcb_p));

            // using region 1 page table for current process
            WriteRegister(REG_PTBR1, (unsigned int) curr_pcb -> pageTable); 
            WriteRegister(REG_PTLR1, (unsigned int) MAX_PT_LEN); 
            WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_1); 
        }

        TracePrintf(2, "We are process with PID: %d. We just woke up since the terminal is not busy anymore.\n", curr_pcb->pid);
        TracePrintf(2, "Process %d transmitting bytes to terminal\n", curr_pcb->pid); 
        
        terminal->terminal_status = 1; // indicate that terminal is busy 

        // not waiting for another transmit anymore, not waiting to write currently
        curr_pcb->terminal_transmit_busy = 0;
        curr_pcb->terminalWrite = 0;

        char* kernel_buf;
        // allocate buffer in kernel space
        if (user_len >= TERMINAL_MAX_LINE) {
            kernel_buf = (char*) malloc(TERMINAL_MAX_LINE);
            if (kernel_buf == NULL) {
                TracePrintf(1, "Error allocating buffer\n");
                uctxt->regs[0] = ERROR;
                return;
            }
            memcpy(kernel_buf, user_buf, TERMINAL_MAX_LINE);

            // start the transmit of bytes to the terminal
            TtyTransmit(tty_id, kernel_buf, TERMINAL_MAX_LINE);
        } else {
            TracePrintf(1, "Allocating buffer of size %d\n", user_len);
            TracePrintf(1, "User buffer: %s", user_buf);
            kernel_buf = (char*) malloc(user_len);
            if (kernel_buf == NULL) {
                TracePrintf(1, "Error allocating buffer\n");
                uctxt->regs[0] = ERROR;
                return;
            }
            memcpy(kernel_buf, user_buf, user_len);

            // start the transmit of bytes to the terminal
            TtyTransmit(tty_id, kernel_buf, user_len);
        }
        // calling process is blocked until the transmit is completed
        PCB_t* next_pcb_p = find_ready_pcb();
        if (addPCB(&blockedHead, curr_pcb) == -1) {
            TracePrintf(1, "Error adding PCB to blocked list\n");
            uctxt->regs[0] = ERROR;
            return;
        } else {
            removePCB(&readyHead, next_pcb_p);
        }

        curr_pcb->transmit = 1;
        curr_pcb->terminalWrite = 1; 
        curr_pcb->tty_id = tty_id; 

        // Update for the while loop 
        user_len -= TERMINAL_MAX_LINE;
        user_buf += TERMINAL_MAX_LINE;
        
        int rc = KernelContextSwitch(KCSwitch, (void*) (curr_pcb), (void *)(next_pcb_p));

        // using region 1 page table for current process
        WriteRegister(REG_PTBR1, (unsigned int) curr_pcb -> pageTable); 
        WriteRegister(REG_PTLR1, (unsigned int) MAX_PT_LEN); 
        WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_1); 
    }

    // transmit is complete!
    TracePrintf(2, "We are process %d and our transmit is complete!\n", curr_pcb->pid); 

    // return number of bytes written
    uctxt->regs[0] = uctxt->regs[2];
}
