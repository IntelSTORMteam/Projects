/*
 * Intel provides this code “as-is” and disclaims all express and implied
 * warranties, including without limitation, the implied warranties of
 * merchantability, fitness for a particular purpose, and non-infringement, as
 * well as any warranty arising from course of performance, course of dealing,
 * or usage in trade. No license (express or implied, by estoppel or otherwise)
 * to any intellectual property rights is granted by Intel providing this code.
 * This code is preliminary, may contain errors and is subject to change without
 * notice. Intel technologies' features and benefits depend on system
 * configuration and may require enabled hardware, software or service
 * activation. Performance varies depending on system configuration. Any
 * differences in your system hardware, software or configuration may affect
 * your actual performance.  No product or component can be absolutely secure.
 *
 * Intel and the Intel logo are trademarks of Intel Corporation in the United
 * States and other countries.
 *
 * Other names and brands may be claimed as the property of others.
 *
 * © Intel Corporation
 */

/*
 * Licensed under the GPL v2
 */

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/user.h>
#include <unistd.h>
#include <bfd.h>
#include <link.h>
#include <string.h>
#include <math.h>

#define WARN(format, ...) \
    fprintf(stderr, "shaker: " format "\n", ##__VA_ARGS__)

char entry_pattern[8] = { 0x41, 0xff, 0xe3, 0x0f, 0xb, 0x90, 0x90, 0x90 };

int read_byte(char *byte, int pid, Elf64_Addr addr)
{
    Elf64_Addr value;

    value = ptrace(PTRACE_PEEKDATA, pid, addr, 0);
    if (errno) {
         return 1;
    }
    *byte = value & 0xff;

    return 0;
}

int read_entry(char * byte, int pid, Elf64_Addr addr)
{
    unsigned int i;
    for (i = 0; i < 16; i++) {
        if (read_byte(byte + i, pid, addr + i)) {
            WARN("read_memory error.");
            return 1;
        }
    }
    return 0;
}

int is_last_entry(char * entry)
{
    if (strncmp(entry, entry_pattern, 8)!=0) return 1;
    if (strncmp(entry+8, entry_pattern, 8)==0) return 2;
    return 0;
}

int find_thunk_start(int pid, Elf64_Addr addr, long unsigned int *final_address)
{
    char entry[16];
    int i;

    for (i = 0; i <= (8 * 0xfff8); i++){
        if (read_entry(entry, pid, addr)) return 1;
        if (is_last_entry(entry) == 0) {
            *final_address = addr - (0xfff7 * 8);
            return 0;
        };
        addr++;
    }

    return 2;
}

int attach(int pid)
{
    int status;

    if (ptrace(PTRACE_ATTACH, pid, NULL, NULL)) {
        WARN("PTRACE_ATTACH error.\n");
        return 1;
    }

    usleep(1000);
    if (waitpid(-1, &status, __WALL) == -1) {
        WARN("waitpid error (pid %d).", pid);
        return 2;
    }

    return 0;
}

int detach(int pid)
{
    if (ptrace(PTRACE_DETACH, pid, NULL, NULL))
    {
        WARN("PTRACE_DETACH error.");
        return 1;
    }
    return 0;
}

int set_regs(int pid, struct user_regs_struct *regs)
{
    if (ptrace(PTRACE_SETREGS, pid, NULL, regs)) {
        WARN("PTRACE_SETREGS error.");
        return 1;
    }
    return 0;
}

int get_regs(int pid, struct user_regs_struct *regs)
{
    if (ptrace(PTRACE_GETREGS, pid, NULL, regs)) {
        WARN("PTRACE_GETREGS error.");
        return 1;
    }
    return 0;
}

int check_args(int argc, char **argv)
{
  if (argc == 0) return 0;

  fprintf(stderr, "Use: %s <pid>\n", argv[0]);
  return 1;
}

int main(int argc, char **argv)
{
    int pid;
    struct user_regs_struct regs;
    Elf64_Addr thunk_entry;

    if (check_args(argc, argv)) return 1;
    pid = atoi(argv[1]);

    if (attach(pid)) {
        WARN("Unable to attach to %d.", pid);
        return 2;
    }

    if (get_regs(pid, &regs)) {
        WARN("Unable to get registers.\n");
        return 3;
    }

    fprintf(stderr, " - old R13: 0x%llx\n", regs.r13);

    if (find_thunk_start(pid, regs.r13, &thunk_entry)) {
        WARN("Unable to find trunk start.");
        return 4;
    }

    srand(regs.r13);
    regs.r13 = thunk_entry + (rand() & 0xfff8);

    fprintf(stderr, " - new R13: 0x%llx\n", regs.r13);

    if (set_regs(pid, &regs)) {
        WARN("Unable to set registers.");
        return 4;
    }

    if (detach(pid)) {
        WARN("Unable to dettach.");
        return 6;
    }

    return 0;
}
