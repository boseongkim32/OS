/*
* syscall_pipe.c 
*
* This file contains the implementation of the pipe system calls. 
*/

#include <hardware.h>
#include <kernel_func.h>
#include <yalnix.h>
#include <ykernel.h>
#include <load_program.h>
#include <syscalls.h>

/* ********************************** kernel_pipe_init ********************************** */ 
/*
 * Creating the pipe 
 */
void kernel_pipe_init(PCB_t *curr_pcb) {

    TracePrintf(1, "In the kernel_pipe_init handler\n");
    UserContext* uctxt = &(curr_pcb->uc); 
    int *pipe_id = (int*) uctxt->regs[0]; 
    total_pipes -= 1;
    *pipe_id = total_pipes; // saving identifier of pipe

    // creating a new pipe
    pipe_t *pipe = malloc(sizeof(pipe_t));

    // in case of error, return ERROR
    if (pipe == NULL) {
        TracePrintf(1, "Failed to allocate memory for pipe\n");
        uctxt->regs[0] = ERROR;
        return;
    }
    
    // add pipe to the list of pipes
    if (addPipe(&pipeHead, pipe) == -1) {
        TracePrintf(1, "Failed to add pipe to the list of pipes\n");
        uctxt->regs[0] = ERROR;
        return;
    }

    pipe->id = total_pipes;
    pipe->buffer = malloc(PIPE_BUFFER_LEN);
    // in case of error, return ERROR
    if (pipe->buffer == NULL) {
        TracePrintf(1, "Failed to allocate memory for pipe buffer\n");
        uctxt->regs[0] = ERROR;
        return;
    }
    pipe->readIndex = 0;
    pipe->writeIndex = 0;

    TracePrintf(2, "Pipe id: %d\n", pipe->id);
    TracePrintf(2, "Pipe write index: %d\n", pipe->writeIndex);
    TracePrintf(2, "Pipe read index: %d\n", pipe->readIndex);

    uctxt->regs[0] = 0; 
}

/* ********************************** kernel_pipe_read ********************************** */ 
/*
 * Reading from the pipe. If the pipe is empty, the process is blocked. 
 */ 
void kernel_pipe_read(PCB_t *curr_pcb) {

    TracePrintf(1, "In the kernel_pipe_read handler\n");
    UserContext* uctxt = &(curr_pcb->uc);

    // collecting arguments passed to the library call
    int pipe_id = (int) uctxt->regs[0]; 
    void* buffer = (void *) uctxt->regs[1];
    int len = (int) uctxt->regs[2]; 

    // Check if the pipe exists 
    pipe_t *pipe = NULL;
    for (pipe_node_t *curr = pipeHead; curr != NULL; curr = curr->next) {
        if (curr->pipe->id == pipe_id) {
            pipe = curr->pipe;
        }
    }
    if (pipe == NULL) {
        TracePrintf(1, "Pipe not found\n");
        uctxt->regs[0] = ERROR;
        return;
    }

    int plen;
    if (pipe->writeIndex < pipe->readIndex) {
        plen = (PIPE_BUFFER_LEN - pipe->readIndex) + pipe->writeIndex;
    } else {
        plen = pipe->writeIndex - pipe->readIndex;
    }
    TracePrintf(2, "Pipe length: %d\n", plen);

    // if pipe is empty, block the caller
    if (plen == 0) {
        curr_pcb->read = 1;
        curr_pcb->pipe_id = pipe_id;
        if (addPCB(&blockedHead, curr_pcb) == -1) {
            TracePrintf(1, "Failed to add process to blocked list\n");
            uctxt->regs[0] = ERROR;
            return;
        }
        // find a readyPCB to context switch into
        PCB_t* next_pcb_p = find_ready_pcb();
        removePCB(&readyHead, next_pcb_p);
        TracePrintf(2, "The pipe is empty! Blocking the caller. Switching to another ready process!\n");
        KernelContextSwitch(KCSwitch, (void*) (curr_pcb), (void *)(next_pcb_p));
    }
    
    TracePrintf(2, "The pipe is not empty! Reading from the pipe!\n");

    // process is now unblocked
    if (pipe->writeIndex < pipe->readIndex) {
        plen = (PIPE_BUFFER_LEN - pipe->readIndex) + pipe->writeIndex;
    } else {
        plen = pipe->writeIndex - pipe->readIndex;
    }
    TracePrintf(2, "Pipe length: %d\n", plen);

    curr_pcb->read = 0;

    // using region 1 page table for current process
    WriteRegister(REG_PTBR1, (unsigned int) runningProcess -> pageTable); 
    WriteRegister(REG_PTLR1, (unsigned int) MAX_PT_LEN); 
    WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_1); 

    int bytes_read = 0;  
    // if requested length is >= pipe's length give remaining bytes in pipe to the caller's buffer
    if (len >= plen) {
        if (pipe->writeIndex < pipe->readIndex) {
            // reading to the end of the pipe
            memcpy(buffer, pipe->buffer + pipe->readIndex, PIPE_BUFFER_LEN - pipe->readIndex);

            // reading from the beginning of the pipe to the write index
            memcpy(buffer + PIPE_BUFFER_LEN - pipe->readIndex, pipe->buffer, pipe->writeIndex);
            pipe->readIndex = (pipe->readIndex + plen) % PIPE_BUFFER_LEN;
        } else {
            TracePrintf(2, "Length of pipe: %d Length of buffer: %d\n", plen, len); 
            memcpy(buffer, pipe->buffer + pipe->readIndex, plen);
            pipe->readIndex += plen;
        }
        bytes_read += plen;
    // if requested length is < pipe's length give requested bytes in pipe to the caller's buffer
    } else {
        if (pipe->readIndex + len > PIPE_BUFFER_LEN) {
            TracePrintf(2, "Read needs to wrap around!\n");
            // reading to the end of the pipe
            memcpy(buffer, pipe->buffer + pipe->readIndex, PIPE_BUFFER_LEN - pipe->readIndex);

            // reading from the beginning of the pipe to the write index
            memcpy(buffer + PIPE_BUFFER_LEN - pipe->readIndex, pipe->buffer, len - (PIPE_BUFFER_LEN - pipe->readIndex));
            pipe->readIndex = (pipe->readIndex + len) % PIPE_BUFFER_LEN;
        } else {
            memcpy(buffer, pipe->buffer + pipe->readIndex, len);
            pipe->readIndex += len;
        }
        bytes_read += len;
    }

    // returning the bytes read
    uctxt->regs[0] = bytes_read;
}


/* ********************************** kernel_pipe_write ********************************** */ 
/*
* Writing to the pipe. If the pipe is full, the process is blocked. 
*/
void kernel_pipe_write(PCB_t *curr_pcb) {

    TracePrintf(1, "In the kernel_pipe_write handler\n");
    UserContext* uctxt = &(curr_pcb->uc);

    // collecting arguments passed to the library call
    int pipe_id = (int) uctxt->regs[0]; 
    void* buffer = (void *) uctxt->regs[1];
    int len = (int) uctxt->regs[2]; 

    // Check if the pipe exists and is held by the process
    pipe_t *pipe = NULL;
    for (pipe_node_t *curr = pipeHead; curr != NULL; curr = curr->next) {
        if (curr->pipe->id == pipe_id) {
            pipe = curr->pipe;
        }
    }
    if (pipe == NULL) {
        TracePrintf(1, "Pipe not found\n");
        uctxt->regs[0] = ERROR;
        return;
    }

    int plen;
    if (pipe->writeIndex < pipe->readIndex) {
        plen = (PIPE_BUFFER_LEN - pipe->readIndex) + pipe->writeIndex;
    } else {
        plen = pipe->writeIndex - pipe->readIndex;
    }
    if (plen + len >= PIPE_BUFFER_LEN) {
        TracePrintf(1, "Pipe is full\n");
        uctxt->regs[0] = ERROR;
        return;
    }   

    // write len bytes from buffer to pipe
    if (pipe->writeIndex + len > PIPE_BUFFER_LEN) {
        TracePrintf(2, "Write needs to wrap around!\n");
        // writing to the end of the pipe
        memcpy(pipe->buffer + pipe->writeIndex, buffer, PIPE_BUFFER_LEN - pipe->writeIndex);

        // writing from the beginning of the pipe to the read index
        memcpy(pipe->buffer, buffer + PIPE_BUFFER_LEN - pipe->writeIndex, len - (PIPE_BUFFER_LEN - pipe->writeIndex));
        pipe->writeIndex = (pipe->writeIndex + len) % PIPE_BUFFER_LEN;
    } else {
        memcpy(pipe->buffer + pipe->writeIndex, buffer, len);
        pipe->writeIndex += len;
    }

    // updating the length of the pipe
    if (pipe->writeIndex < pipe->readIndex) {
        plen = (PIPE_BUFFER_LEN - pipe->readIndex) + pipe->writeIndex;
    } else {
        plen = pipe->writeIndex - pipe->readIndex;
    }

    // return the number of bytes written
    uctxt->regs[0] = len;
}
