#include <alloca.h>
#include <elf.h>
#include <fcntl.h>
#include <libgen.h>
#include <linux/limits.h>
#include <linux/unistd.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/random.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysinfo.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <cstring>

#include "env.h"
#include "execute.h"
#include "mmu.h"

#include "syscalls.h"

namespace dbt
{
struct env::ElfImage {
    Elf32_Ehdr ehdr;
    uabi_ulong load_addr;
    uabi_ulong stack_start;
    uabi_ulong entry;
    uabi_ulong brk;
};
env::ElfImage env::exe_elf_image{};

struct env::Process {
    std::string fsroot;
    int exe_fd{-1};
    uabi_ulong brk{};
};
env::Process env::process{};

int env::Execute(CPUState *state)
{
    CPUState::SetCurrent(state);
    while (true) {
        dbt::Execute(state);
        switch (state->trapno) {
        case rv32::TrapCode::EBREAK:
            return 1;
        case rv32::TrapCode::ECALL:
            state->ip += 4;
            env::SyscallLinux(state);
            if (state->trapno == rv32::TrapCode::TERMINATED)
                return state->gpr[10];  // TODO: forward sys_exit* arg
            break;
        case rv32::TrapCode::ILLEGAL_INSN:
            return 1;
        default:
            unreachable("no handle for trap");
        }
    }
    CPUState::SetCurrent(nullptr);
}

void env::InitThread(CPUState *state, ElfImage *elf)
{
    assert(!(elf->stack_start & 15));
    state->gpr[2] = elf->stack_start;
    state->ip = elf->entry;
}

static void dbt_sigaction_memory(int signo UNUSED,
                                 siginfo_t *sinfo,
                                 void *uctx_raw UNUSED)
{
    if (!mmu::check_h2g(sinfo->si_addr))
        Panic("Memory fault in host address space");
    Panic("Memory fault in guest address space.");
}

// TODO: emulate signals
void env::InitSignals(CPUState *state UNUSED)
{
    struct sigaction sa;
    sigset_t sset;

    sigfillset(&sset);
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = dbt_sigaction_memory;

    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGBUS, &sa, nullptr);
}

static int HandleSpecialPath(char const *path, char *resolved)
{
    if (!strcmp(path, "/proc/self/exe")) {
        sprintf(resolved, "/proc/self/fd/%d", env::process.exe_fd);
        return 1;
    }
    return 0;
}

static int PathResolution(int dirfd, char const *path, char *resolved)
{
    char rp_buf[PATH_MAX];

    auto const &fsroot = env::process.fsroot;

    if (path[0] == '/') {
        snprintf(rp_buf, sizeof(rp_buf), "%s/%s", fsroot.c_str(), path);
    } else {
        if (dirfd == AT_FDCWD) {
            getcwd(rp_buf, sizeof(rp_buf));
        } else {
            char fdpath[64];
            sprintf(fdpath, "/proc/self/fd/%d", dirfd);
            if (readlink(fdpath, rp_buf, sizeof(rp_buf)) < 0)
                return -1;
        }
        size_t pref_sz = strlen(rp_buf);
        strncpy(rp_buf + pref_sz, path, sizeof(rp_buf) - pref_sz);
    }
    if (strncmp(rp_buf, fsroot.c_str(), fsroot.length()))
        Panic("Malformed fsroot");

    if (HandleSpecialPath(rp_buf + fsroot.length() + 1, resolved) > 0)
        return 0;

    // TODO: make it precise, resolve "/.." and symlinks
    if (!realpath(rp_buf, resolved))
        return -1;
    if (strncmp(resolved, fsroot.c_str(), fsroot.length()))
        Panic("Malformed fsroot");

    if (path[0] != '/')
        strcpy(resolved, path);

    return 0;
}

enum class SyscallID : u32 {
#define _(name, no) linux_##name = no,
    RV32_LINUX_SYSCALL_LIST
#undef _
        End,
};

[[noreturn]] static uabi_long SyscallUnhandled(uabi_ulong no)
{
    static char const *const names[to_underlying(SyscallID::End)] = {
#define _(name, no) [to_underlying(SyscallID::linux_##name)] = #name,
        RV32_LINUX_SYSCALL_LIST
#undef _
    };
    char const *name =
        (no > 0 && no < to_underlying(SyscallID::End)) ? names[no] : "UNKNOWN";

    Panic(std::string("Unhandled Linux syscall: ") + name);
}

namespace env_syscall
{
static inline uabi_long rcerrno(uabi_long rc)
{
    if (unlikely(rc < 0))
        return -errno;
    return rc;
}

static uabi_long linux_openat(uabi_int dfd,
                              const char *filename,
                              uabi_int flags,
                              mode_t mode)
{
    return rcerrno(openat(dfd, filename, flags, mode));
}

static uabi_long linux_close(uabi_uint fd)
{
    if (fd < 3)  // TODO: split file descriptors
        return 0;
    return rcerrno(close(fd));
}

static uabi_long linux_llseek(uabi_uint fd,
                              uabi_ulong offset_high,
                              uabi_ulong offset_low,
                              loff_t *result,
                              uabi_uint whence)
{
    off_t off = ((u64) offset_high << 32) | offset_low;
    int rc = lseek(fd, off, whence);
    if (rc >= 0)
        *result = rc;
    return 0;
}

static uabi_long linux_read(uabi_uint fd, char *buf, uabi_size_t count)
{
    return rcerrno(read(fd, buf, count));
}

static uabi_long linux_write(uabi_uint fd, const char *buf, uabi_size_t count)
{
    return rcerrno(write(fd, buf, count));
}

static uabi_long linux_readlinkat(uabi_int dfd,
                                  const char *path,
                                  char *buf,
                                  uabi_int bufsiz)
{
    char pathbuf[PATH_MAX];
    if (*path) {
        if (PathResolution(dfd, path, pathbuf) < 0)
            return (uabi_long) -errno;
    } else {
        pathbuf[0] = 0;
    }
    return rcerrno(readlinkat(dfd, pathbuf, buf, bufsiz));
}

using uabi_stat64 = struct stat;

static uabi_long linux_fstat64(uabi_uint fd, uabi_stat64 *statbuf)
{
    // TODO: verify!!!
    return rcerrno(fstatat(fd, "", statbuf, 0));
}

static uabi_long linux_set_tid_address(uabi_int *tidptr)
{
    int h_tidptr;
    long rc = syscall(SYS_set_tid_address, &h_tidptr);
    if (rc < 0)
        return -errno;
    *tidptr = h_tidptr;
    return rc;
}

static uabi_long linux_exit(uabi_int error_code)
{
    CPUState::Current()->trapno = rv32::TrapCode::TERMINATED;
    return error_code;
}

static uabi_long linux_exit_group(uabi_int error_code)
{
    return linux_exit(error_code);
}

static uabi_long linux_rt_sigaction(int,
                                    const struct uabi_sigaction *,
                                    struct sigaction *,
                                    uabi_size_t)
{
    return 0;
}

using uabi_new_utsname = struct utsname;

static uabi_long linux_uname(uabi_new_utsname *name)
{
    uabi_long rc = uname(name);
    strcpy(name->machine, "riscv32");
    return rcerrno(rc);
}

static uabi_long linux_getuid()
{
    return getuid();
}

static uabi_long linux_geteuid()
{
    return geteuid();
}

static uabi_long linux_getgid()
{
    return getgid();
}

static uabi_long linux_getegid()
{
    return getegid();
}

struct uabi_sysinfo {
    u32 uptime;
    u32 loads[3];
    u32 totalram;
    u32 freeram;
    u32 sharedram;
    u32 bufferram;
    u32 totalswap;
    u32 freeswap;
    u16 procs;
    u16 pad;
    u32 totalhigh;
    u32 freehigh;
    u32 mem_unit;
    char _f[20 - 2 * sizeof(u32) - sizeof(u32)];
};

static uabi_long linux_sysinfo(struct uabi_sysinfo *info)
{
    struct sysinfo host_info;
    uabi_long rc = sysinfo(&host_info);

    if (rc > 0) {
        info->uptime = host_info.uptime;
        for (int i = 0; i < 3; ++i) {
            info->loads[i] = host_info.loads[i];
        }
        info->totalram = 1_GB;
        info->freeram = 500_MB;
        info->sharedram = info->bufferram = info->totalswap = info->freeswap =
            1_MB;
        info->procs = host_info.procs;
        info->totalhigh = info->freehigh = 1_MB;
        info->mem_unit = 1;
    }
    return rcerrno(rc);
}

static uabi_long linux_brk(uabi_ulong newbrk)
{
    auto &brk = env::process.brk;

    if (newbrk <= brk)
        return brk;
    uabi_ulong brk_p = roundup(brk, mmu::PAGE_SIZE);
    if (newbrk <= brk_p) {
        if (newbrk != brk_p)
            memset(mmu::g2h(brk), 0, newbrk - brk);
        return brk = newbrk;
    }
    void *mem = mmu::mmap(brk_p, newbrk - brk_p, PROT_READ | PROT_WRITE,
                          MAP_ANON | MAP_PRIVATE | MAP_FIXED);
    if (mmu::h2g(mem) != brk_p) {
        munmap(mem, newbrk - brk_p);
        return brk;
    }
    memset(mmu::g2h(brk), 0, brk_p - brk);
    return brk = newbrk;
}

static uabi_long linux_munmap(uabi_ulong gaddr, uabi_size_t len)
{
    // TODO: implement in mmu
    return rcerrno(munmap(mmu::g2h(gaddr), len));
}

static uabi_long linux_mmap2(uabi_ulong gaddr,
                             uabi_size_t len,
                             uabi_ulong prot,
                             uabi_ulong flags,
                             uabi_uint fd,
                             uabi_ulong off)
{
    // TODO: file maps in mmu
    void *ret = mmu::mmap(gaddr, len, prot, flags, fd, off);
    if (ret == MAP_FAILED)
        return (uabi_long) -errno;
    uabi_long rc = mmu::h2g(ret);
    return rc;
}

static uabi_long linux_mprotect(uabi_ulong start,
                                uabi_size_t len,
                                uabi_ulong prot)
{
    // TODO: implement in mmu
    return rcerrno(mprotect(mmu::g2h(start), len, prot));
}

using uabi_pid_t = uabi_int;

struct uabi_rlimit64 {
    uint64_t rlim_cur;
    uint64_t rlim_max;
};

static uabi_long linux_prlimit64(uabi_pid_t pid,
                                 uabi_uint resource,
                                 const struct uabi_rlimit64 *new_rlim,
                                 struct uabi_rlimit64 *old_rlim)
{
    rlimit64 h_new_rlim, h_old_rlim, *h_new_rlim_p;

    if (new_rlim && !(resource == RLIMIT_AS || resource == RLIMIT_STACK ||
                      resource == RLIMIT_DATA)) {
        h_new_rlim.rlim_cur = new_rlim->rlim_cur;
        h_new_rlim.rlim_max = new_rlim->rlim_max;
        h_new_rlim_p = &h_new_rlim;
    } else {
        // just ignore
        h_new_rlim_p = nullptr;
    }

    long rc = syscall(SYS_prlimit64, (pid_t) pid, (uint) resource, h_new_rlim_p,
                      &h_old_rlim);
    if (rc < 0)
        return -errno;

    old_rlim->rlim_cur = h_old_rlim.rlim_cur;
    old_rlim->rlim_max = h_old_rlim.rlim_max;
    return rc;
}

static uabi_long linux_getrandom(char *buf, uabi_size_t count, uabi_uint flags)
{
    return rcerrno(getrandom(buf, count, flags));
}

using uabi_statx = struct statx;

static uabi_long linux_statx(uabi_int dfd,
                             const char *path,
                             unsigned flags,
                             unsigned mask,
                             uabi_statx *buffer)
{
    char pathbuf[PATH_MAX];
    if (*path) {
        if (PathResolution(dfd, path, pathbuf) < 0)
            return (uabi_long) -errno;
    } else {
        pathbuf[0] = 0;
    }
    return rcerrno(statx(dfd, pathbuf, flags, mask, buffer));
}

struct uabi__kernel_timespec {
    uabi_llong tv_sec;
    uabi_llong tv_nsec;
};

static uabi_long linux_clock_gettime64(clockid_t which_clock,
                                       uabi__kernel_timespec *ktp)
{
    timespec tp;
    auto rc = clock_gettime(which_clock, &tp);
    ktp->tv_sec = tp.tv_sec;
    ktp->tv_nsec = tp.tv_nsec;
    return rcerrno(rc);
}

}  // namespace env_syscall

void env::SyscallLinux(CPUState *state)
{
    state->trapno = rv32::TrapCode::NONE;
    std::array<uabi_long, 7> args = {
        (uabi_long) state->gpr[10], (uabi_long) state->gpr[11],
        (uabi_long) state->gpr[12], (uabi_long) state->gpr[13],
        (uabi_long) state->gpr[14], (uabi_long) state->gpr[15],
        (uabi_long) state->gpr[16]};
    uabi_long syscallno = state->gpr[17];

    auto do_syscall = [&args]<typename RV, typename... Args>(RV(*h)(Args...))
    {
        static_assert(sizeof...(Args) <= args.size());
        auto conv = []<typename A>(uabi_ulong in) -> A {
            if constexpr (std::is_pointer_v<A>)
                return (A) mmu::g2h(in);
            else
                return static_cast<A>(in);
        };
        return ([&]<size_t... Idx>(std::index_sequence<Idx...>) {
            return h(decltype(conv)().template operator()<Args>(args[Idx])...);
        }) (std::make_index_sequence<sizeof...(Args)>{});
    };

    auto dispatch = [&]() -> uabi_long {
        switch (SyscallID(syscallno)) {
#define HANDLE(name)      \
    case SyscallID::name: \
        return do_syscall(env_syscall::name);

#define HANDLE_SKIP(name) \
    case SyscallID::name: \
        return -ENOSYS;
            HANDLE(linux_openat)
            HANDLE(linux_close)
            HANDLE(linux_llseek)
            HANDLE(linux_read)
            HANDLE(linux_write)
            HANDLE(linux_readlinkat)
            HANDLE(linux_fstat64)
            HANDLE(linux_set_tid_address)
            HANDLE_SKIP(linux_set_robust_list)
            HANDLE(linux_exit)
            HANDLE(linux_exit_group)
            HANDLE(linux_rt_sigaction)
            HANDLE(linux_uname)
            HANDLE(linux_getuid)
            HANDLE(linux_geteuid)
            HANDLE(linux_getgid)
            HANDLE(linux_getegid)
            HANDLE(linux_sysinfo)
            HANDLE(linux_brk)
            HANDLE(linux_munmap)
            HANDLE(linux_mmap2)
            HANDLE(linux_mprotect)
            HANDLE(linux_prlimit64)
            HANDLE(linux_getrandom)
            HANDLE(linux_statx)
            HANDLE(linux_clock_gettime64)
#undef HANDLE
        default:
            SyscallUnhandled(syscallno);
        }
    };

    uabi_long rc = dispatch();
    state->gpr[10] = rc;
}

void env::BootElf(const char *path, ElfImage *elf)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        Panic("No such ELF file");

    {  // TODO: for AT_FDCWD resolution, remove it
        char buf[PATH_MAX];
        strncpy(buf, path, sizeof(buf));
        chdir(dirname(buf));
    }

    LoadElf(fd, elf);
    process.exe_fd = fd;
    process.brk = elf->brk;  // TODO: move it out

    // switch to 32 * mmu::PAGE_SIZE if debugging
    static constexpr u32 stk_size = 8_MB;
    // ASAN somehow breaks MMap lookup if it's not MAP_FIXED
    void *stk_ptr =
        mmu::mmap(mmu::ASPACE_SIZE - mmu::PAGE_SIZE - stk_size, stk_size,
                  PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE | MAP_FIXED);
    elf->stack_start = mmu::h2g(stk_ptr) + stk_size;
}

// -march=rv32i -O2 -fpic -fpie -static
// -march=rv32i -O2 -fpic -fpie -static -ffreestanding -nostartfiles -nolibc
void env::LoadElf(int fd, ElfImage *elf)
{
    auto &ehdr = elf->ehdr;

    if (pread(fd, &elf->ehdr, sizeof(ehdr), 0) != sizeof(ehdr))
        Panic("Cannot read ELF header");
    if (memcmp(ehdr.e_ident,
               "\x7f"
               "ELF",
               4))
        Panic("It is not ELF");
    if (ehdr.e_machine != EM_RISCV)
        Panic("ELF's machine does not match");
    if (ehdr.e_type != ET_EXEC)
        Panic("Unuspported ELF type");

    ssize_t phtab_sz = sizeof(Elf32_Phdr) * ehdr.e_phnum;
    auto *phtab = (Elf32_Phdr *) alloca(phtab_sz);
    if (pread(fd, phtab, phtab_sz, ehdr.e_phoff) != phtab_sz)
        Panic("Cannot read phtab");
    elf->load_addr = -1;
    elf->brk = 0;
    elf->entry = elf->ehdr.e_entry;

    for (size_t i = 0; i < ehdr.e_phnum; ++i) {
        auto *phdr = &phtab[i];
        if (phdr->p_type != PT_LOAD)
            continue;

        int prot = 0;
        if (phdr->p_flags & PF_R)
            prot |= PROT_READ;
        if (phdr->p_flags & PF_W)
            prot |= PROT_WRITE;
        if (phdr->p_flags & PF_X)
            prot |= PROT_EXEC;

        auto vaddr = phdr->p_vaddr;
        auto vaddr_ps = rounddown(phdr->p_vaddr, mmu::PAGE_SIZE);
        auto vaddr_po = vaddr - vaddr_ps;

        if (phdr->p_filesz != 0) {
            u32 len = roundup(phdr->p_filesz + vaddr_po, mmu::PAGE_SIZE);
            // shared flags
            mmu::mmap(vaddr_ps, len, prot, MAP_FIXED | MAP_PRIVATE, fd,
                      phdr->p_offset - vaddr_po);
            if (phdr->p_memsz > phdr->p_filesz) {
                auto bss_start = vaddr + phdr->p_filesz;
                auto bss_end = vaddr_ps + phdr->p_memsz;
                auto bss_start_nextp = roundup(bss_start, (u32) mmu::PAGE_SIZE);
                auto bss_len =
                    roundup(bss_end - bss_start, (u32) mmu::PAGE_SIZE);
                mmu::mmap(bss_start_nextp, bss_len, prot,
                          MAP_FIXED | MAP_PRIVATE | MAP_ANON);
                u32 prev_sz = bss_start_nextp - bss_start;
                if (prev_sz != 0)
                    memset(mmu::g2h(bss_start), 0, prev_sz);
            }
        } else if (phdr->p_memsz != 0) {
            u32 len = roundup(phdr->p_memsz + vaddr_po, (u32) mmu::PAGE_SIZE);
            mmu::mmap(vaddr_ps, len, prot, MAP_FIXED | MAP_PRIVATE | MAP_ANON);
        }

        elf->load_addr = std::min(elf->load_addr, vaddr - phdr->p_offset);
        elf->brk = std::max(elf->brk, vaddr + phdr->p_memsz);
    }
}

static uabi_ulong AllocAVectorStr(uabi_ulong stk, void const *str, u16 sz)
{
    stk -= sz;
    memcpy(mmu::g2h(stk), str, sz);
    return stk;
}

static inline uabi_ulong AllocAVectorStr(uabi_ulong stk, char const *str)
{
    return AllocAVectorStr(stk, str, strlen(str) + 1);
}

// TODO: refactor
void env::InitAVectors(ElfImage *elf, int argv_n, char **argv)
{
    uabi_ulong stk = elf->stack_start;

    uabi_ulong foo_str_g = stk = AllocAVectorStr(stk, "__foo_str__");
    uabi_ulong lc_all_str_g = stk = AllocAVectorStr(stk, "LC_ALL=C");
    char auxv_salt[16] = {0, 1, 2, 3, 4, 5, 6};
    uabi_ulong auxv_salt_g = stk =
        AllocAVectorStr(stk, auxv_salt, sizeof(auxv_salt));

    u32 *argv_strings_g = (u32 *) alloca(sizeof(char *) * argv_n);
    for (int i = 0; i < argv_n; ++i) {
        argv_strings_g[i] = stk = AllocAVectorStr(stk, argv[i]);
    }

    stk &= -4;

    int envp_n = 1;
    int auxv_n = 64;

    int stk_vsz = argv_n + envp_n + auxv_n + 3;
    stk -= stk_vsz * sizeof(uabi_ulong);
    stk &= -16;
    uabi_ulong argc_p = stk;
    uabi_ulong argv_p = argc_p + sizeof(uabi_ulong);
    uabi_ulong envp_p = argv_p + sizeof(uabi_ulong) * (argv_n + 1);
    uabi_ulong auxv_p = envp_p + sizeof(uabi_ulong) * (envp_n + 1);

    auto push_avval = [](uint32_t &vec, uint32_t val) {
        *(uabi_ulong *) mmu::g2h(vec) = (val);
        vec += sizeof(uabi_ulong);
    };
    auto push_auxv = [&](uint16_t idx, uint32_t val) {
        push_avval(auxv_p, idx);
        push_avval(auxv_p, val);
    };

    push_avval(argc_p, argv_n);

    for (int i = 0; i < argv_n; ++i) {
        push_avval(argv_p, argv_strings_g[i]);
    }
    push_avval(argv_p, 0);

    push_avval(envp_p, lc_all_str_g);
    push_avval(envp_p, 0);

    push_auxv(AT_PHDR, elf->ehdr.e_phoff + elf->load_addr);
    push_auxv(AT_PHENT, sizeof(Elf32_Phdr));
    push_auxv(AT_PHNUM, elf->ehdr.e_phnum);
    push_auxv(AT_PAGESZ, mmu::PAGE_SIZE);
    push_auxv(AT_BASE, 0);
    push_auxv(AT_FLAGS, 0);
    push_auxv(AT_ENTRY, elf->entry);

    push_auxv(AT_UID, getuid());
    push_auxv(AT_GID, getgid());
    push_auxv(AT_EUID, geteuid());
    push_auxv(AT_EGID, getegid());

    push_auxv(AT_EXECFN, foo_str_g);
    push_auxv(AT_SECURE, false);
    push_auxv(AT_HWCAP, 0);
    push_auxv(AT_CLKTCK, sysconf(_SC_CLK_TCK));
    push_auxv(AT_RANDOM, auxv_salt_g);

    push_auxv(AT_NULL, 0);

    elf->stack_start = stk;

    /* setup file system root */
    char buf[PATH_MAX];
    getcwd(buf, sizeof(buf));
    process.fsroot = std::string(buf) + "/";
}

}  // namespace dbt
