#ifndef TRACY_H
#define TRACY_H

#include <sys/wait.h>
#include "ll.h"

#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/user.h>

#include <asm/ptrace.h>
#include "tracyarch.h"


/* Tracy options, pass them to tracy_init(). */
#define TRACY_TRACE_CHILDREN 1 << 0
#define TRACY_VERBOSE 1 << 1

#define TRACY_USE_SAFE_TRACE 1 << 31

struct tracy_child;

struct tracy_sc_args {
    long a0, a1, a2, a3, a4, a5;
    long return_code, syscall, ip, sp;
};

struct tracy_event {
    int type;
    struct tracy_child *child;
    long syscall_num;
    long signal_num;

    struct tracy_sc_args args;
};

typedef int (*tracy_hook_func) (struct tracy_event *s);

typedef void (*tracy_child_creation) (struct tracy_child *c);

/*
 * Special events. A set of functions to be called when certain things
 * happen. Currently contains:
 *
 * child_create(tracy_child *c);
 *
 *      To be used to initialise some values when a child is created.
 *      You cannot inject anything at this time and you shall not touch the
 *      event variable of the child.
 *
 *      If you want to mess with system calls and injection, simply wait for the
 *      first event of the child; as this will always be called before you
 *      recieve an event from the new child.
 *
 */
struct tracy_special_events {
    tracy_child_creation child_create;
} ;

struct tracy {
    struct soxy_ll *childs;
    struct soxy_ll *hooks;
    pid_t fpid;
    long opt;
    tracy_hook_func defhook;
    struct tracy_special_events se;
};


struct tracy_inject_data {
    int injecting, injected;
    int pre;
    int syscall_num;
    struct TRACY_REGS_NAME reg;
    tracy_hook_func cb;
};

struct tracy_child {
    pid_t pid;

    /* Switch indicating we attached to this child
     *
     * Processes we attached to shouldn't be killed by default
     * since we only came along to take a peek. Childs of processes
     * attached to, should inherit this flag.
     */
    int attached;

    /* PRE/POST syscall switch */
    int pre_syscall;

    /* File descriptor pointing to /proc/<pid>/mem, -1 if closed */
    int mem_fd;

    /* Last denied syscall */
    int denied_nr;

    void* custom;

    struct tracy* tracy;

    /* Asynchronous syscall injection info */
    struct tracy_inject_data inj;

    /* Last event that occurred */
    struct tracy_event event;

    /* Child PID acquired through controlled forking */
    pid_t safe_fork_pid;
};

/* Pointers for parent/child memory distinction */
typedef void *tracy_child_addr_t, *tracy_parent_addr_t;


#define TRACY_EVENT_NONE 1
#define TRACY_EVENT_SYSCALL 2
#define TRACY_EVENT_SIGNAL 3
#define TRACY_EVENT_INTERNAL 4
#define TRACY_EVENT_QUIT 5

/* Define hook return values */
#define TRACY_HOOK_CONTINUE 0
#define TRACY_HOOK_KILL_CHILD 1
#define TRACY_HOOK_ABORT 2
#define TRACY_HOOK_NOHOOK 3

/* Setting up and tearing down a tracy session */

/*
 * tracy_init
 *
 * tracy_init creates the tracy record and returns a pointer to this record on
 * success. Possible options for ``opt'':
 *
 *  -   TRACY_TRACY_CHILDREN (Trace children of the tracee created with fork,
 *      vfork or clone.)
 *  -   TRACY_USE_SAFE_TRACE (Do not rely on Linux' auto-trace on fork abilities
 *      and instead use our own safe implementation)
 *
 * Returns the tracy record created.
 */

struct tracy *tracy_init(long opt);
void tracy_free(struct tracy *t);

/*
 * tracy_quit
 *
 * tracy_quit frees all the structures, kills or detaches from all the
 * children and then calls exit() with *exitcode*. Use tracy_free if you want to
 * gracefully free tracy.
 *
 */

void tracy_quit(struct tracy* t, int exitcode);

/*
 * tracy_main
 *
 * tracy_main is a simple tracy-event loop.
 * Helper for RAD Tracy deployment
 *
 */
int tracy_main(struct tracy *tracy);

/* fork_trace, returns pid */
struct tracy_child *fork_trace_exec(struct tracy *t, int argc, char **argv);
struct tracy_child *tracy_attach(struct tracy *t, pid_t pid);

/*
 * tracy_attach
 * tracy_fork
 * tracy_fork_exec
 */

/*
 * tracy_wait_event
 *
 * tracy_wait_event waits for an event to occur on any child when pid is -1;
 * else on a specific child.
 *
 * tracy_wait_event will detect any new children and automatically add them to
 * the appropriate datastructures.
 *
 * An ``event'' is either a signal or a system call. tracy_wait_event populates
 * events with the right data; arguments; system call number, etc.
 *
 * Returns an event pointer or NULL.
 *
 * If NULL is returned, you should probably kill all the children and stop
 * tracy; NULL indicates something went wrong internally such as the inability
 * to allocate memory or an unsolvable ptrace error.
 */
struct tracy_event *tracy_wait_event(struct tracy *t, pid_t pid);

/*
 *  XXX:TODO tracy_destroy
 */

/* -- Basic functionality -- */

/*
 * tracy_continue
 *
 * tracy_continue continues the execution of the child that owns event *s*.
 * If the event was caused by a signal to the child, the signal
 * is passed along to the child, unless *sigoverride* is set to nonzero.
 *
 */
int tracy_continue(struct tracy_event *s, int sigoverride);

/*
 * tracy_kill_child
 *
 * tracy_kill_child attemps to kill the child *c*; it does so using ptrace with
 * the PTRACE_KILL argument.
 *
 * Return 0 upon success, -1 upon failure.
 */
int tracy_kill_child(struct tracy_child *c);

int tracy_remove_child(struct tracy_child *c);

int tracy_children_count(struct tracy* t);
int check_syscall(struct tracy_event *s);
char* get_syscall_name(int syscall);
char* get_signal_name(int signal);

/* -- Syscall hooks -- */
int tracy_set_hook(struct tracy *t, char *syscall, tracy_hook_func func);
int tracy_set_default_hook(struct tracy *t, tracy_hook_func f);

/*
 * tracy_execute_hook
 *
 *
 * Returns the return value of the hook. Hooks should return:
 *
 *  -   TRACY_HOOK_CONTINUE if everything is fine.
 *  -   TRACY_HOOK_KILL_CHILD if the child should be killed.
 *  -   TRACY_HOOK_ABORT if tracy should kill all childs and quit.
 *  -   TRACY_HOOK_NOHOOK is no hook is in place for this system call.
 *
 */
int tracy_execute_hook(struct tracy *t, char *syscall, struct tracy_event *e);

/* -- Child memory access -- */
int tracy_peek_word(struct tracy_child *c, long from, long* word);
ssize_t tracy_read_mem(struct tracy_child *c, tracy_parent_addr_t dest,
    tracy_child_addr_t src, size_t n);

int tracy_poke_word(struct tracy_child *c, long to, long word);
ssize_t tracy_write_mem(struct tracy_child *c, tracy_child_addr_t dest,
    tracy_parent_addr_t src, size_t n);

/* -- Child memory management -- */
int tracy_mmap(struct tracy_child *child, tracy_child_addr_t *ret,
        tracy_child_addr_t addr, size_t length, int prot, int flags, int fd,
        off_t pgoffset);
int tracy_munmap(struct tracy_child *child, long *ret,
       tracy_child_addr_t addr, size_t length);

/* -- Syscall management -- */

/* Synchronous injection */
int tracy_inject_syscall(struct tracy_child *child, long syscall_number,
        struct tracy_sc_args *a, long *return_code);

/* Asynchronous injection */
int tracy_inject_syscall_pre_start(struct tracy_child *child, long syscall_number,
        struct tracy_sc_args *a, tracy_hook_func callback);
int tracy_inject_syscall_pre_end(struct tracy_child *child, long *return_code);

int tracy_inject_syscall_post_start(struct tracy_child *child, long syscall_number,
        struct tracy_sc_args *a, tracy_hook_func callback);
int tracy_inject_syscall_post_end(struct tracy_child *child, long *return_code);

/* Modification and rejection */
int tracy_modify_syscall(struct tracy_child *child, long syscall_number,
        struct tracy_sc_args *a);
int tracy_deny_syscall(struct tracy_child* child);

/* Safe forking */
int tracy_safe_fork(struct tracy_child *c, pid_t *new_child);

#endif
