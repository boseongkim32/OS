# OS

A portion of a simplified single-core OS built in fulfillment of the OS course at Dartmouth College.

With the help of support software provided by the class, a user-level program running in 32-bit mode on a Linux OS on an Intel machine will be made to behave like a kernel.
It appears that we are running our own operating system, but in reality everything is running as a user-process on a virtual Linux machine.
This OS supports multiple processes, each having their own virtual address space and can also be linked to standard library functions.

I did not write the support software, and as such did not upload it to this repository. As a consequence, this OS will not compile, and have deleted the previous commits to prevent violating that. I have attached GIFs to demonstrate how this program works.

The hardware invokes `KernelStart()` to boot the system, and when the function returns, the machine begins running in user-mode at a specified UserContext.

### Syscalls, Interrupts, Exceptions

When such events occur, the hardware will throw a trap, switch to kernel mode and invoke the trap-handler we told it to invoke.
When the handler returns, the hardware returns to user mode.

User processes request services from the kernel by executing a "syscall".

### Core syscalls

```c
int Fork(void)
int Exec(char *filename, char **argvec)
void Exit(int status)
int Wait(int *status_ptr)
int GetPid(void)
int Brk(void *addr)
int Delay(int clock_ticks)
```

The OS provides services such as creating new processes (Fork), loading programs, killing processes, etc.
Some of this functionality is shown below.

### IO Syscalls

```c
int TtyRead(int tty_id, void *buf, int len)
int TtyWrite(int tty_id, void *buf, int len)
```

In a virtual machine with IO support, the OS can also write to and read from termiansl, as shown below.

### IPC Syscalls

```c
int PipeInit(int *pipe_idp)
int PipeRead(int pipe_id, void *buf, int len)
int PipeWrite(int pipe_id, void *buf, int len)
```

The OS provides support for basic pipe functionality of creating and reading/writing to it.

### Synchronization Syscalls

```c
int LockInit(int *lock_idp)
int Acquire(int lock_id)
int Release(int lock_id)
int CvarInit(int *cvar_idp)
int CvarSignal(int cvar_id)
int CvarBroadcast(int cvar_id)
int CvarWait(int cvar_id, int lock_id)
int Reclaim(int id)
```

Condition variables are also supported by this OS.

### Interrupt, Exception, Trap Handling

```c
TRAP_KERNEL
TRAP_CLOCK
TRAP_ILLEGAL
TRAP_MEMORY
TRAP_MATH
TRAP_TTY_RECEIVE
TRAP_TTY_TRANSMIT
TRAP_DISK
```

The hardware traps that occur include syscall calling, clock interrupts, illegal memory usage, writing/reading to terminal completion, etc.

The kernel is also responsible for all aspects of memory management, both for user processes executing on the system and for the kernel's own use of memory.
The kernel also uses a simple round-robin scheduling algorithm with no such thing as priorities.
