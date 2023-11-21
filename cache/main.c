#define _DEFAULT_SOURCE
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <assert.h>
#include <pthread.h>
#include <string.h>
#include <poll.h>
#include <errno.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/ucontext.h>
#include <fcntl.h>             /* Definition of O_* constants */
#include <sys/syscall.h>       /* Definition of SYS_* constants */
#include <linux/userfaultfd.h> /* Definition of UFFD_* constants */

#include "seccomp-bpf.h"

#ifdef NDEBUG
#define DEBUG(...) ((void)0)

#undef assert
#define assert(cond) ((cond) ? ((void)0) : abort())

#else
#define DEBUG(...) printf("[+] " __VA_ARGS__)
#endif

#define INPUTFD 41
#define OUTPUTFD 42

#define PROT_INTERNAL 0x80

#define REGION_BASE 0x100000000000uLL
#define REGION_PROTECTED_SIZE (0x1000uLL * 0x10000uLL)
#define REGION_PROTECTED_END (REGION_BASE + REGION_PROTECTED_SIZE)
#define REGION_ALLOC_START (REGION_BASE + 0x10000uLL)
#define REGION_PTE_START (REGION_BASE + (REGION_PROTECTED_SIZE >> 1))
#define REGION_PTE_END REGION_PROTECTED_END
#define REGION_END  0x200000000000uLL

#define PHYSICAL_BASE 0x200000000000uLL
#define PHYSICAL_END  0x300000000000uLL
#define TABLE_BASE REGION_BASE

#define PAGE_SIZE 0x1000
#define PAGE_MASK 0xfff

/* TUNABLES */

#define PAGING_LEVELS 2

/* END TUNABLES*/

/*
    == what is happening ==

    the game is given a number on fd 3 and must calculate # of collatz steps.
    by default it will calculate incorrectly, which gives you a score of 0.
    
    == how are you scored ==

    firstly, you must get the right answer, otherwise you will get 0 points.
    then, your returned score is based on how many memory accesses you make.
*/

/*
    TTEs here are 64b in width:

    [32-64] - cache count
    [12-32] - page mapping bits, split depending on # of levels
    [4-12] - unused
    [3-3] - cache disabled
        * 0 => YES_CACHE
        * 1 => NO_CACHE
    [1-2] - memory protections
        * 0 => PROT_NONE
        * 1 => PROT_READ
        * 2 => PROT_WRITE
        * 3 => PROT_READ | PROT_WRITE
    [0] - valid bit, must be set on every step
*/

static pthread_t thread;

static void *region = NULL;
static void *physical = NULL; 
static volatile int g_uffd = -1;
static struct uffdio_api uffd_api;
static struct uffdio_register uffd_reg;

struct tlb_entry {
    uint32_t cache;
    uint64_t key;
    uint64_t val;
    uint8_t access_flags;
    uint8_t populated;
};

uint16_t g_tlb_write = 0;
#define G_TLB_SIZE 0x10000
static volatile struct tlb_entry *g_tlb = NULL;
#define GET_TLB_CACHE(idx) (g_tlb[(uint16_t)(idx)].cache)
#define SET_TLB_IDX(idx, k, v, a, c) do { \
    uint16_t myidx = (uint16_t)(idx); \
    g_tlb[myidx].key = (k); \
    g_tlb[myidx].val = (v); \
    g_tlb[myidx].access_flags = (a); \
    g_tlb[myidx].cache = (c); \
    g_tlb[myidx].populated = 1; \
} while(0);
#define SET_TLB_CACHE(idx, val) do { \
    uint16_t myidx = (uint16_t)(idx); \
    g_tlb[myidx].cache = val; \
    g_tlb[myidx].populated = 1; \
} while(0);

struct collatz_game {
    volatile uint32_t *input;
    volatile uint32_t *output;
    volatile uint32_t *pages;
    volatile uint32_t *pages_cnt;
    volatile uint32_t *counts;
    volatile uint32_t *quicksolver;
};

static volatile struct collatz_game *collatz_state = NULL;

void dump_tlb(void);

void *safe_memset(void *buf, int c, size_t len) {
    if (len & 3) {
        printf("unaligned.\n");
        exit(1);
    }
    // we do this to enforce alignment
    for(uint32_t x = 0; x < len; x += 4) {
        *(volatile uint32_t *)&buf[x] = c;
    }
    return buf;
}

uint64_t resolve_with_tlb(uint64_t addr, uint8_t *access_flags) {
    for(int x = 0; x < G_TLB_SIZE; x++) {
        if ((g_tlb[x].key == addr) && (g_tlb[x].populated == 1)) {
            g_tlb[x].cache -= 1;
            if (g_tlb[x].cache == 0) {
                // flush the entry
                DEBUG("flushing entry %d (0x%lx -> 0x%lx)\n", x, g_tlb[x].key, g_tlb[x].val);
                safe_memset((void *)&g_tlb[x], 0x00, sizeof(g_tlb[x]));
                continue;
            }
            DEBUG("\tvalue is... 0x%lx\n", g_tlb[x].val);
            assert(g_tlb[x].key >= REGION_BASE);
            assert(g_tlb[x].key < REGION_END);
            assert(g_tlb[x].val >= PHYSICAL_BASE);
            assert(g_tlb[x].val < PHYSICAL_END);

            // update the tlb cache bits as appropriate
            uint16_t cache1 = GET_TLB_CACHE(g_tlb_write);
            uint32_t cache1_val = GET_TLB_CACHE(cache1);
            uint16_t cache2 = GET_TLB_CACHE(g_tlb_write + 1);
            uint32_t cache2_val = GET_TLB_CACHE(cache2);
            uint16_t cache3 = GET_TLB_CACHE(g_tlb_write + 2);
            uint16_t new_idx = g_tlb_write + 3;
#ifndef NDEBUG
            if (cache1 == 0x4141) {
                DEBUG("saw bailout value. bye\n");
                dump_tlb();
                exit(0);
            }
#endif
            DEBUG("\tmem[%x] (%x) = %x (%x) - %x (%x)\n", g_tlb_write, cache1_val - cache2_val, cache1, cache1_val, cache2, cache2_val);
            if (cache1_val <= cache2_val) {
                new_idx = cache3;
                DEBUG("\t%x <= %x, next instr %u\n", cache1_val, cache2_val, new_idx);
            } else {
                DEBUG("\t%x > %x, next instr %u\n", cache1_val, cache2_val, new_idx);
            }
#ifdef ADDLEQ
            SET_TLB_CACHE(cache1, cache1_val + cache2_val);
#elif SUBLEQ
            SET_TLB_CACHE(cache1, cache1_val - cache2_val);
#else
#error "unknown game"
#endif
            g_tlb_write = new_idx;

            if (access_flags) {
                *access_flags = g_tlb[x].access_flags;
            }
            return g_tlb[x].val;
        }
    }
    return 0;
}

uint64_t resolve_address(uint64_t addr, uint8_t *access_flags, int skip_tlb) {

    DEBUG("resolving addr 0x%lx\n", addr);
    uint64_t tlb = resolve_with_tlb(addr, access_flags);
    if (tlb) {
        DEBUG("\t-> resolved via tlb = 0x%lx\n", tlb);
        return tlb;
    }

    assert(addr >= REGION_BASE);
    assert(addr < REGION_END);

    // mask to the base of the page
    addr &= ~(0xfff);

    int paging_depth = PAGING_LEVELS;

    uint64_t currbase = PHYSICAL_BASE;
    uint8_t af = 0;
    uint32_t cachehint = 0;
    int disable_cache = 0;

    for(int x = 0; x < paging_depth; x++) {
        assert(currbase >= PHYSICAL_BASE);
        assert(currbase < PHYSICAL_END);
        // get the appropriate bits of this address for our current level
        uint64_t bitmask = (20 >> (paging_depth - 1));
        uint64_t pos = (20 - ((x+1)*bitmask)) + 12;
        bitmask = (1uLL << bitmask) - 1uLL;
        DEBUG("pos %lx bitmask %lx\n", pos, bitmask);
        uint64_t offset = (addr >> pos) & bitmask;
        DEBUG("pos %lx bitmask %lx -> offset %lx\n", pos, bitmask, offset);
        uint64_t *entry_p = (uint64_t *)((currbase & ~(0xfff)) + (offset << 3));
        DEBUG("\tentry_p = %p\n", entry_p);
        uint64_t entry = *entry_p;
        
        currbase = PHYSICAL_BASE + (entry & 0xffffffff);

        if ((currbase & 1) == 0) {
            DEBUG("\t-> entry at %p is invalid (0x%lx)\n", entry_p, currbase);
            printf("[!] invalid entry detected, aborting\n");
            exit(1);
        }
        af = (currbase >> 1) & 3;
        disable_cache = (currbase >> 3) & 1;
        cachehint = entry >> 32;
        currbase &= ~(0xfff);
        DEBUG("\t-> currbase = 0x%lx\n", currbase);
    }

    currbase &= ~(0xfff);
    assert(currbase >= PHYSICAL_BASE);
    assert(currbase < PHYSICAL_END);
    DEBUG("currbase - %lx cachehint - %x\n", currbase, cachehint);

    if (disable_cache || skip_tlb) {
        DEBUG("skipping TLB insertion\n");
    } else {
        // insert into TLB
        DEBUG("inserting 0x%lx -> 0x%lx (af: %x cache %x) into TLB at 0x%x\n", addr, currbase, af, cachehint, g_tlb_write);
        SET_TLB_IDX(g_tlb_write++, addr, currbase, af, cachehint);
    }

    if (access_flags)
        *access_flags = af;

    // return our now-resolved value
    return currbase;
}

/* DEBUG FUNCTIONS */
#ifndef NDEBUG
void dump_tlb() {
    DEBUG(" == dump tlb ==\n");
    for(int x = 0; x < G_TLB_SIZE; x++) {
        if(g_tlb[x].populated || g_tlb[x].cache) {
            DEBUG("tlb idx 0x%x 0x%lx -> 0x%lx af %x (cache: 0x%x)\n", x, g_tlb[x].key, g_tlb[x].val, g_tlb[x].access_flags, g_tlb[x].cache);
        }
    }
}

uint64_t debug_resolve_addr(uint64_t addr) {
    DEBUG("debug_resolve_addr(0x%lx)\n", addr);
    return resolve_address(addr, NULL, 1);
}
#endif
/* END DEBUG FUNCTIONS */

void * handler_thread(void *arg) {

    struct uffdio_copy uffdio_copy;

    struct uffd_msg *msg = (struct uffd_msg *)arg;

    DEBUG("received event %d from userfaultfd\n", msg->event);

    if (msg->event != UFFD_EVENT_PAGEFAULT) {
        DEBUG("unexpected userfaultfd event %d\n", msg->event);
        exit(EXIT_FAILURE);
    }

    /* Display info about the page-fault event. */

    DEBUG("    UFFD_EVENT_PAGEFAULT event: ");
    DEBUG("flags = %llx address - 0x%llx\n", msg->arg.pagefault.flags, msg->arg.pagefault.address);

    uint64_t resolved_addr = resolve_address(msg->arg.pagefault.address, NULL, 0);

    /* Copy the page pointed to by 'page' into the faulting
    region. Vary the contents that are copied in, so that it
    is more obvious that each fault is handled separately. */

    uffdio_copy.src = (unsigned long)resolved_addr;

    /* We need to handle page faults in units of pages(!).
    So, round faulting address down to page boundary. */

    uffdio_copy.dst = (unsigned long) msg->arg.pagefault.address & ~(0xfff);
    uffdio_copy.len = PAGE_SIZE;
    uffdio_copy.mode = 0;
    uffdio_copy.copy = 0;

    if (mprotect((void *)uffdio_copy.dst, PAGE_SIZE, PROT_NONE) < 0) {
        exit(1);
    }

    if (ioctl(g_uffd, UFFDIO_COPY, &uffdio_copy) == -1) {
        DEBUG("userfault fd response failed errno %d\n", errno);
        perror(NULL);
        exit(1);
    }

    if (uffdio_copy.copy != PAGE_SIZE) {
        DEBUG("kernel failed to copy the full page? fatal...\n");
        exit(1);
    }

    free(msg);

    return NULL;
}

void * monitor_thread(void *arg) {
    assert(arg == NULL);

    // we don't want to be in a weird situation of handling our own faults here 
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGSEGV);
    sigaddset(&mask, SIGTRAP);
    sigprocmask(SIG_BLOCK, &mask, NULL);

    while(g_uffd > 0) {
        struct pollfd pollfd[1];
		pollfd[0].fd = g_uffd;
		pollfd[0].events = POLLIN;
        pollfd[0].revents = 0;
		int nready = poll((struct pollfd *)&pollfd, 1, -1);
		if (nready == -1) break;

        if (pollfd[0].revents) {
            // paging request
            struct uffd_msg *msg = calloc(sizeof(struct uffd_msg), 1);
            int nread = read(g_uffd, msg, sizeof(*msg));
            if (nread != sizeof(*msg)) {
                DEBUG("error %d reading from g_uffd\n", nread);
                break;
            }

            // spawn a handler thread and allow it to process the even
            pthread_t helper;
            if (pthread_create(&helper, NULL, handler_thread, msg)) {
                break;
            }
        } else {
            DEBUG("error, unknown poll wakeup reason?\n");
            break;
        }
    }

    if (g_uffd > 0) {
        printf("[!] this is an error, not part of the game, alert an admin.\n");
        exit(1);
    }

    return NULL;
}

void setup_uffd() {

    int ret = -1;

    // no return value - failure here is a challenge issue
    g_uffd = syscall(SYS_userfaultfd, UFFD_USER_MODE_ONLY);
    DEBUG("g_uffd is %d\n", g_uffd);
    assert(g_uffd > -1);

    uffd_api.api = UFFD_API;
    uffd_api.features = 0;
    
    ret = ioctl(g_uffd, UFFDIO_API, &uffd_api);
    assert(ret == 0);

    // create our region for the game
    uint64_t region_length = REGION_END - REGION_BASE;

    region = mmap((void *)REGION_BASE, region_length, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    DEBUG("region is %p\n", region);
    assert((uint64_t)region == REGION_BASE);
    
    physical = mmap((void *)PHYSICAL_BASE, region_length, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    DEBUG("physical is %p\n", physical);
    assert((uint64_t)physical == PHYSICAL_BASE);

    // map our base page
    *(volatile uint64_t *)REGION_BASE = (PROT_READ | PROT_WRITE) << 1 | 1;

    uffd_reg.range.start = (unsigned long)region;
    uffd_reg.range.len = region_length;
    uffd_reg.mode = UFFDIO_REGISTER_MODE_MISSING;
    ret = ioctl(g_uffd, UFFDIO_REGISTER, &uffd_reg);
    assert(ret == 0);

    // create handler thread

    ret = pthread_create(&thread, NULL, monitor_thread, NULL);
    assert (ret == 0);

    if (ret != 0) {
        exit(1);
    }

    ret = madvise((void *)REGION_BASE, region_length, MADV_NOHUGEPAGE);
    assert (ret == 0);
    ret = madvise((void *)PHYSICAL_BASE, region_length, MADV_NOHUGEPAGE);
    assert (ret == 0);

    DEBUG("uffd is initialized.\n");

}

static _Thread_local uint64_t fault_depth = 0;

struct fault_pair {
    uint64_t virt;
    uint64_t phys;
};

static _Thread_local struct fault_pair faulting_addrs[0x100] = {0};

static volatile uint64_t access_count = 0;

#define X86_EFLAGS_TF (1u << 8u)
void signal_handler(int signal, siginfo_t *info, void *ucontext) {

    DEBUG("signal_handler: thread %d handling signal %02d fault_depth %02lx\n", gettid(), signal, fault_depth);

    if (signal == SIGSEGV && info->si_code == SEGV_ACCERR) {
        // check the fault address
        uint64_t addr = (uint64_t)info->si_addr & ~(0xfff);
        if (addr >= REGION_BASE && addr < REGION_END) {

            // increment the access count
            access_count++;

            uint8_t access_flags = 0;
            ucontext_t *context = (ucontext_t *)ucontext;

            faulting_addrs[fault_depth].virt = addr;
            faulting_addrs[fault_depth].phys = resolve_address(addr, &access_flags, 0);
                
            if (access_flags == PROT_NONE) {
                printf("page is mapped PROT_NONE\n");
                exit(1);
            }
            
            // make the virtual page match the physical one
            if (mprotect((void *)addr, PAGE_SIZE, PROT_READ | PROT_WRITE) < 0) {
                exit(1);
            }
            memcpy((void *)faulting_addrs[fault_depth].virt, (void *)faulting_addrs[fault_depth].phys, PAGE_SIZE);

            // mark the page as the appropriate permissions
            mprotect((void *)addr, PAGE_SIZE, access_flags);

            // make the thread single step
            context->uc_mcontext.gregs[REG_EFL] |= X86_EFLAGS_TF;

            // check for overflow
            assert(fault_depth < (sizeof(faulting_addrs)/sizeof(faulting_addrs[0])));
#ifndef NDEBUG
            char *af_to_str[] = {"PROT_NONE", "PROT_READ", "PROT_WRITE", "PROT_READ | PROT_WRITE"};
#endif
            DEBUG("\t0x%lx -> %s\n", addr, af_to_str[access_flags]);
            fault_depth++;
            return;
        }
    } else if (signal == SIGTRAP) {
        if (fault_depth > 0) { 

            // copy the page back

            // mark the page as prot_none again
            fault_depth -= 1;
            DEBUG("\t0x%lx -> PROT_NONE\n", faulting_addrs[fault_depth].virt);
            memcpy((void *)faulting_addrs[fault_depth].phys, (void *)faulting_addrs[fault_depth].virt, PAGE_SIZE);
            if (mprotect((void *)faulting_addrs[fault_depth].virt, PAGE_SIZE, PROT_NONE) < 0) {
                exit(1);
            }
            faulting_addrs[fault_depth].virt = 0;
            faulting_addrs[fault_depth].phys = 0;

            // clear the single-step flag
            ucontext_t *context = (ucontext_t *)ucontext;
            context->uc_mcontext.gregs[REG_EFL] &= ~X86_EFLAGS_TF;
            return;
        }
        
    }
    // we don't expect to handle this fault. bye
    printf("unexpected signal.\n");
    exit(1);
}

void setup_signalhandler() {
    struct sigaction sa={0};
    sa.sa_sigaction = signal_handler;
    sa.sa_flags = SA_NODEFER | SA_SIGINFO;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGTRAP, &sa, NULL);
}

static uint64_t next_phys_page = PHYSICAL_BASE + PAGE_SIZE;

uint64_t get_next_phys_page(uint64_t num) {
    // retrieve the next available physical page
    // bump allocator, never resets for the lifetime of the program
    assert((next_phys_page + num*PAGE_SIZE) < PHYSICAL_END);
    uint64_t ret = next_phys_page;
    next_phys_page += (num*PAGE_SIZE);
    return ret;
}

static uint64_t next_pte_page = 0;
uint64_t get_pte_entry_location() {
    // we maintain a list of pages we have allocated for PTEs
    // from among these pages, I choose the one based on LIFO
    uint64_t page = REGION_PTE_START + ((next_pte_page++) * PAGE_SIZE);
    if (page > REGION_PTE_END) {
        page = REGION_PTE_START;
        next_pte_page = 0;
    }
    return page - REGION_BASE;
}

void menu() {
    printf("=== cache ===\n");
    printf("resolving pages since 1996\n");
    printf("using our new PageRank algorithm!\n");
    printf("1. add some page\n");
    printf("2. remove some page\n");
    printf("3. write some page\n");
    printf("4. read some page\n");
    printf("5. done\n");
};

uint8_t g_userbuf[PAGE_SIZE];
uint32_t read_data(int nbytes) {
    printf("# ");
    memset(g_userbuf, 0, nbytes);
    uint32_t x = 0;
    for(x = 0; x < nbytes; x++) {
        int ret = read(0, &g_userbuf[x], 1);
        if (ret != 1) break;
    }
    return x;
}

uint64_t read_choice() {
    printf("# ");
    char buf[0x10] = {0};
    for(int x = 0; x < sizeof(buf); x++) {
        int ret = read(0, &buf[x], 1);
        if (ret != 1) break;
        if (buf[x]== 0x0a) break;
    }

    // if we read nothing, the socket is probably closed
    if (buf[0] == '\0') exit(1);

    return strtol(buf, NULL, 16);
}

static uint32_t g_cacheseed = 0x1337;

uint32_t get_random_cache() {
    uint32_t x = g_cacheseed;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g_cacheseed = x;
    return g_cacheseed;
}

uint64_t insert_address(uint64_t addr, uint8_t access_flags, int fixed) {
    // check if the address is invalid
    if (addr >= REGION_BASE && addr < REGION_END) {
        if ((access_flags & PROT_READ) && (addr < REGION_PROTECTED_END) && ((access_flags & PROT_INTERNAL) == 0)) {
            printf("cannot allocate within readable zone\n");
            return 0;
        } else {
            // insert our mapping for the address
            int paging_depth = PAGING_LEVELS;
            uint64_t currbase = PHYSICAL_BASE;

            uint64_t cache_bits = (uint64_t)get_random_cache();
            uint64_t physaddr = get_next_phys_page(1);
            uint64_t entry = (cache_bits << 32) | (physaddr - PHYSICAL_BASE) | (access_flags << 1) | 1;
            /*if (access_flags & PROT_INTERNAL) {
                entry |= (1 << 3); // NO_CACHE
            }*/
            for(int x = 0; x < paging_depth; x++) {
                // get the appropriate bits of this address for our current level
                assert(currbase >= PHYSICAL_BASE);
                assert(currbase < PHYSICAL_END);
                uint64_t bitmask = (20 >> (paging_depth - 1));
                uint64_t pos = (20 - ((x+1)*bitmask)) + 12;
                bitmask = (1uLL << bitmask) - 1uLL;
                DEBUG("pos %lx bitmask %lx\n", pos, bitmask);
                uint64_t offset = (addr >> pos) & bitmask;
                DEBUG("pos %lx bitmask %lx -> offset %lx\n", pos, bitmask, offset);

                uint64_t *entry_p = (uint64_t *)((currbase & ~(0xfff)) + (offset << 3));
                DEBUG("entry_p - %p (curr: 0x%lx)\n", entry_p, *entry_p);

                if ((x + 1) == paging_depth) {
                    // this is the final page, so we can just insert our entry
                    if ((*entry_p & 1) && fixed == 0) {
                        // there is already a page here... lets try the next page
                        return insert_address(addr + PAGE_SIZE, access_flags, fixed);
                    }
                    DEBUG("inserting entry 0x%lx at 0x%lx\n", entry, (uint64_t)entry_p);
                    *entry_p = entry;    
                } else {
                    uint64_t next_page = 0;
                    if (*entry_p & 1) {
                        // we already have another level of TTE, so just use it
                        next_page = (*entry_p) & 0xfffff000;
                    } else {
                        // get a new page
                        next_page = get_pte_entry_location();
                        *entry_p = next_page | ((PROT_READ | PROT_WRITE) << 1) | 1;
                        DEBUG("inserting pte entry 0x%lx at 0x%lx\n", *entry_p, (uint64_t)entry_p);
                    }
                    currbase = PHYSICAL_BASE + next_page;
                    DEBUG("\t-> currbase = 0x%lx\n", currbase);
                }
            }
            DEBUG("currbase - %lx\n", currbase);
        }
    }
    return addr;
}

uint64_t allocate_internal(uint64_t size) {
    if ((size < PAGE_SIZE) || (size % PAGE_SIZE != 0)) {
        printf("size must be multiple of page size and at least PAGE_SIZE\n");
        exit(1);
    }

    // find the first free page 
    uint64_t addr = insert_address(REGION_ALLOC_START, PROT_READ | PROT_WRITE | PROT_INTERNAL, 0);
    DEBUG("allocate_internal attempting to start at 0x%lx for %lu bytes\n", addr, size);
    
    // if we ran into the PTEs, we are probably dead
    if (addr >= REGION_PTE_START) {
        DEBUG("ran into PTEs, bail out\n");
        exit(1);
    }

    // ok, since this is a naive bump allocator w/o frees, we can be reasonably
    // confident that the next few pages are also free
    for(int x = 1; x < (size >> 12); x++) {
        uint64_t res = insert_address(addr + (x * PAGE_SIZE), PROT_READ | PROT_WRITE | PROT_INTERNAL, 1);
        if (res != (addr + (x * PAGE_SIZE))) {
            DEBUG("allocate_internal failed: expected 0x%lx got 0x%lx\n", addr + (x * PAGE_SIZE), res);
            exit(1);
        }
    }

    return addr;
}

int user_allocation_cnt = 0;
static uint64_t user_addresses_menu[0x100] = {0};
void add_p4ge() {

    if (user_allocation_cnt >= (sizeof(user_addresses_menu)/sizeof(user_addresses_menu[0]))) {
        printf("too many active allocations\n");
        return;
    };

    printf("addr?\n");
    uint64_t addr = read_choice();
    if (addr & 0xfff) {
        printf("address is malformed\n");
        return;
    }
    if (addr < REGION_PROTECTED_END) {
        printf("addr is in protected area\n");
        return;
    }

    addr &= 0x3ffffffff000uLL;

    // check if we have already allocated this
    for(int x = 0; x < sizeof(user_addresses_menu)/sizeof(user_addresses_menu[0]); x++) {
        if (user_addresses_menu[x] == addr) {
            printf("address is already allocated\n");
            return;
        }
    }
    
    // ok, guess not, so insert it
    // this is technically a bug; because insert_address can fail below, we can unintentionally
    // insert a stale entry. I might leave this, it may be exploitable (?)
    int inserted = 0;
    for(int x = 0; x < sizeof(user_addresses_menu)/sizeof(user_addresses_menu[0]); x++) {
        if (user_addresses_menu[x] == 0) {
            user_addresses_menu[x] = addr;
            inserted = 1;
            break;
        }
    }
    if (inserted == 0) {
        DEBUG("somehow failed to insert? wtf?\n");
        exit(1);
    }

    printf("permissions:\n");
    printf("\t0 = PROT_NONE\n");
    printf("\t1 = PROT_READ\n");
    printf("\t2 = PROT_WRITE\n");
    printf("\t3 = PROT_READ | PROT_WRITE\n");
    uint8_t access_flags = (uint8_t)read_choice();
    if (access_flags > 3) {
        printf("invalid permissions\n");
        return;
    }
    insert_address(addr, access_flags, 1);
}

void remove_p4ge() {
    printf("addr?\n");
    uint64_t addr = read_choice();
    if (addr & 0xfff) {
        printf("address is malformed\n");
        return;
    }

    int present = 0;
    for(int x = 0; x < sizeof(user_addresses_menu)/sizeof(user_addresses_menu[0]); x++) {
        if (user_addresses_menu[x] == addr) {
            present = 1;
            user_addresses_menu[x] = 0;
            break;
        }
    }

    if (present == 0) {
        printf("address is not present\n");
        return;
    }
    
    int paging_depth = PAGING_LEVELS;
    uint64_t currbase = PHYSICAL_BASE;

    uint64_t entry;
    uint64_t *entry_p = NULL;
    for(int x = 0; x < paging_depth; x++) {
        assert(currbase >= PHYSICAL_BASE);
        assert(currbase < PHYSICAL_END);
        // get the appropriate bits of this address for our current level
        uint64_t bitmask = (20 >> (paging_depth - 1));
        uint64_t pos = (20 - ((x+1)*bitmask)) + 12;
        bitmask = (1uLL << bitmask) - 1uLL;
        DEBUG("pos %lx bitmask %lx\n", pos, bitmask);
        uint64_t offset = (addr >> pos) & bitmask;
        DEBUG("pos %lx bitmask %lx -> offset %lx\n", pos, bitmask, offset);
        entry_p = (uint64_t *)((currbase & ~(0xfff)) + (offset << 3));
        DEBUG("\tentry_p = %p\n", entry_p);
        entry = *entry_p;
        
        currbase = PHYSICAL_BASE + (entry & 0xffffffff);

        if ((currbase & 1) == 0) {
            DEBUG("\t-> entry at %p is invalid (0x%lx)\n", entry_p, currbase);
            printf("[!] invalid entry detected, aborting\n");
            exit(1);
        }
        currbase &= ~(0xfff);
        DEBUG("\t-> currbase = 0x%lx\n", currbase);
    }
    *entry_p = 0;
    DEBUG("clearing entry 0x%lx, was at %p -> 0\n", entry, entry_p);
}

void write_p4ge() {
    printf("addr?\n");
    uint64_t addr = read_choice();

    // check if we have already allocated this
    int present = 0;
    for(int x = 0; x < sizeof(user_addresses_menu)/sizeof(user_addresses_menu[0]); x++) {
        if (user_addresses_menu[x] == addr) {
            present = 1;
        }
    }
    if (present == 0) {
        DEBUG("unknown address\n");
        return;
    }

    printf("nbytes (hex)?\n");
    uint32_t nbytes = (uint32_t)read_choice();
    if (nbytes > PAGE_SIZE) {
        printf("too long, sorry\n");
        return;
    }
    if ((nbytes % 8) != 0) {
        printf("data must be multiple of 8\n");
        return;
    }
    
    if (addr < REGION_BASE) {
        printf("bad address.\n");
        return;
    }
    if (addr > PHYSICAL_END) {
        printf("bad address.\n");
        return;
    }
    
    printf("data> ");
    nbytes = read_data(nbytes);
    uint8_t *addr_ptr = (uint8_t *)addr;
    for(uint64_t x = 0; x < nbytes; x += 8) {
        *(uint64_t *)&addr_ptr[x] = *(uint64_t *)&g_userbuf[x]; 
    }
}

void read_p4ge() {
    printf("addr?\n");
    uint64_t addr = read_choice();

    // check if we have already allocated this
    int present = 0;
    for(int x = 0; x < sizeof(user_addresses_menu)/sizeof(user_addresses_menu[0]); x++) {
        if (user_addresses_menu[x] == addr) {
            present = 1;
        }
    }
    if (present == 0) {
        DEBUG("unknown address\n");
        return;
    }

    if (addr < REGION_BASE) {
        printf("bad address.\n");
        return;
    }
    if (addr > PHYSICAL_END) {
        printf("bad address.\n");
        return;
    }

    printf("nbytes (hex)?\n");
    uint32_t nbytes = (uint32_t)read_choice();
    if (nbytes > PAGE_SIZE) {
        printf("too long, sorry\n");
        return;
    }

    assert(nbytes <= sizeof(g_userbuf));
    printf("data follows newline\n");
    memcpy(g_userbuf, (void *)addr, nbytes);
    write(1, (void *)g_userbuf, nbytes);
}

void receive_user_input() {
    while(1) {
        menu();
        switch(read_choice()) {
            case 1:
                add_p4ge(); 
                break;
            case 2:
                remove_p4ge();
                break;
            case 3:
                write_p4ge();
                break;
            case 4:
                read_p4ge();
                break;
            case 5:
                return;
            default:
                printf("unknown choice\n");
                break;
        } 
    }
}

void setup_collatz_memory() {
    // setup functions are called before user data is consumed

    collatz_state = calloc(sizeof(struct collatz_game), 1);

    collatz_state->input = (uint32_t *)allocate_internal(PAGE_SIZE);
    collatz_state->output = (uint32_t *)allocate_internal(PAGE_SIZE);
    collatz_state->pages = (uint32_t *)allocate_internal(0x2 * PAGE_SIZE);
    collatz_state->counts = (uint32_t *)allocate_internal(PAGE_SIZE);
    collatz_state->quicksolver = (uint32_t *)allocate_internal(PAGE_SIZE);
    collatz_state->pages_cnt = calloc(1, 0x1000); // (uint32_t *)allocate_internal(PAGE_SIZE);
    collatz_state->pages_cnt[0] = 0x800;
}

#ifdef NDEBUG
// input something which will almost always be wrong
uint32_t quicksolver_cache[0x80] = {0};
#else
uint32_t quicksolver_cache[] = {
	  1,   0,   1,   7,   2,   5,   8,  16,   3,  19,   6,  14,   9,   9,  17,  17,
	  4,  12,  20,  20,   7,   7,  15,  15,  10,  23,  10, 111,  18,  18,  18, 106,
	  5,  26,  13,  13,  21,  21,  21,  34,   8, 109,   8,  29,  16,  16,  16, 104,
	 11,  24,  24,  24,  11,  11, 112, 112,  19,  32,  19,  32,  19,  19, 107, 107,
	  6,  27,  27,  27,  14,  14,  14, 102,  22, 115,  22,  14,  22,  22,  35,  35,
	  9,  22, 110, 110,   9,   9,  30,  30,  17,  30,  17,  92,  17,  17, 105, 105,
	 12, 118,  25,  25,  25,  25,  25,  87,  12,  38,  12, 100, 113, 113, 113,  69,
	 20,  12,  33,  33,  20,  20,  33,  33,  20,  95,  20,  46, 108, 108, 108,  46,
};
#endif

void play_game_collatz() {

    printf("let me play my favorite game\n");

    // first int tells us how many rounds to play
    uint32_t num_rounds = 0;
#ifdef NDEBUG
    if (read(INPUTFD, (void *)&num_rounds, 4) != 4) {
        printf("failed to get number of rounds\n");
        exit(1);
    }
#else
    num_rounds = 1;
#endif

    for(int rnd = 0; rnd < num_rounds; rnd++) {
        // read the current collatz number
        uint32_t start = 0;
#ifdef NDEBUG
        if (read(INPUTFD, (void *)&start, 4) != 4) {
            printf("failed to get number\n");
            exit(1);
        }
#else
        start = 1234;
#endif
        collatz_state->input[rnd] = start;
#define COLLATZ (collatz_state->input[rnd])

        for(int x = 0; x < (*(volatile uint32_t *)collatz_state->pages_cnt); x++) {
            collatz_state->pages[x] = 0;
        }

        for(int x = 0; x < 0x80; x++) {
            collatz_state->quicksolver[x] = quicksolver_cache[x];
        }

        // we use this to receive the result at the end of the game
        // it will be populated based on the in-memory store, which players can influence
        uint32_t solution = 0;

        // this is a monotonic, safe counter that increments once per loop
        // we use this to store in memory to inform players about what round
        // it is
        uint32_t rounds_safe = 0;
        while(COLLATZ != 1) {
            for(int x = (*(volatile uint32_t *)collatz_state->pages_cnt) - 2; x >= 0; x -= 1) {
                uint32_t next = collatz_state->pages[x];
                collatz_state->pages[x + 1] = next;
            }
            collatz_state->pages[0] = rounds_safe;
			if (COLLATZ < 0x80) {
				// we have precalculated how much is left
                uint32_t val = collatz_state->quicksolver[COLLATZ];
                printf("detected quicksolver of %x\n", val);
				collatz_state->output[rnd] += val;
				break;
			}
            if (COLLATZ % 2)
                COLLATZ = 3 * COLLATZ + 1;
            else
                COLLATZ >>= 1;
            collatz_state->output[rnd]++;
            rounds_safe++;
        }
        solution = collatz_state->output[rnd];
        printf("access %lu rounds %u access/rnd = %lu\n", access_count, rounds_safe, access_count / rounds_safe);

#ifdef NDEBUG
        if (write(OUTPUTFD, (void *)&solution, 4) != 4) {
            printf("failed to reply with solution\n");
            exit(1);
        }
#else
        DEBUG("solution: %d\n", solution);
#endif
    }

}

void setup_seccomp() {

    struct sock_filter filter[] = {
        VALIDATE_ARCHITECTURE,
        EXAMINE_SYSCALL,
        ALLOW_SYSCALL(rt_sigreturn),
#ifdef __NR_sigreturn
        ALLOW_SYSCALL(sigreturn),
#endif
        ALLOW_SYSCALL(exit_group),
        ALLOW_SYSCALL(exit),
        ALLOW_SYSCALL(mprotect),
        ALLOW_SYSCALL(gettid),
        ALLOW_SYSCALL(read),
        ALLOW_SYSCALL(write),
        KILL_PROCESS,
    };

    struct sock_fprog prog = {
        .len = (unsigned short)(sizeof(filter)/sizeof(filter[0])),
        .filter = filter,
    };

    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0)) {
        perror("failed prctl");
        exit(1);
    }

    if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog)) {
        perror("failed seccomp");
        exit(1);
    }
    DEBUG("enforcing seccomp\n");
}

int main() {

    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);

    // 2 minutes
    alarm(60 * 2);

    DEBUG("main - 0x%lx\n", ((uint64_t)main) & ~(0xfff));
    DEBUG("sizeof(struct tlb_entry) == %lu\n", sizeof(struct tlb_entry));

    setup_uffd();
    setup_signalhandler();

    setup_seccomp();
    // seccomp is enabled at this point

    // initialize our TLB
    uint64_t total_tlb_size = G_TLB_SIZE * sizeof(struct tlb_entry);
    total_tlb_size = (total_tlb_size & (~0xfff)) + PAGE_SIZE;
    g_tlb = (struct tlb_entry *)get_next_phys_page(total_tlb_size);
    DEBUG("g_tlb is at %p (0x%lx bytes)\n", g_tlb, total_tlb_size);

    setup_collatz_memory();

    // first phase - receive mappings from the user
    receive_user_input();
    // second phase - play the game

    // reset the access count
    access_count = 0;
    play_game_collatz();

    printf("final access count: %lu\n", access_count);
    if (write(OUTPUTFD, (void *)&access_count, 4) != 4) {
        printf("failed to reply with access count\n");
        exit(1);
    }

	return 0;
}


