struct buf;
struct context;
struct file;
struct inode;
struct pipe;
struct proc;
struct rtcdate;
struct spinlock;
struct sleeplock;
struct stat;
struct superblock;
struct nonblockinglock;
//macro indicating this process is not on any cpus queue
#define NOCPU   777
// bio.c
void            binit(void);
struct buf*     bread(uint32, uint32);
void            brelse(struct buf*);
void            bwrite(struct buf*);
struct buf*   breada(uint32,uint32,uint32);

// console.c
void            consoleinit(void);
void            cprintf(char*, ...);
void            consoleintr(int(*)(void));
void            panic(char*) __attribute__((noreturn));
void            change_mode(int);
// exec.c
int             exec(char*, char**);

// file.c
struct file*    filealloc(void);
void            fileclose(struct file*);
struct file*    filedup(struct file*);
void            fileinit(void);
int             fileread(struct file*, char*, int n);
int             filestat(struct file*, struct stat*);
int             filewrite(struct file*, char*, int n);

// fs.c
void            readsb(int dev, struct superblock *sb);
int             dirlink(struct inode*, char*, uint32);
struct inode*   dirlookup(struct inode*, char*, uint32*,uint32);
struct inode*   ialloc(uint32, short);
struct inode*   idup(struct inode*);
void            iinit(int dev,int sbnum);
void            ilock(struct inode*);
void            iput(struct inode*);
void            iunlock(struct inode*);
void            iunlockput(struct inode*);
void            iupdate(struct inode*);
int             namecmp(const char*, const char*);
struct inode*   namei(uint32 dev,char*);
struct inode*   nameiparent(uint32 dev,char*, char*);
int             readi(struct inode*, char*, uint32, uint32);
void            stati(struct inode*, struct stat*);
int             writei(struct inode*, char*, uint32, uint32);
int             iputmount(struct inode *ip);

// ide.c
void            ideinit(void);
void            secondaryideinit(void);
void            ideintr(void);
void            ideintr2(void);
void            iderw(struct buf*,uint32 dev);

// ioapic.c
void            ioapicenable(int irq, int cpu);
extern uint8    ioapicid;
void            ioapicinit(void);

// kalloc.c
char*           kalloc(void);
void            kfree(char*);
void            kinit1(void*, void*);
void            kinit2(void*, void*);

// kbd.c
void            kbdintr(void);

// lapic.c
void            cmostime(struct rtcdate *r);
int             lapicid(void);
extern volatile uint32*    lapic;
void            lapiceoi(void);
void            lapicinit(void);
void            lapicstartap(uint8, uint32);
void            microdelay(int);

// log.c
void            initlog(int dev);
void            log_write(struct buf*);
void            begin_op();
void            end_op();

//mount.c
int             mount(uint32 dev, char *path);
int             unmount(char *mountpoint);

// mp.c
extern int      ismp;
void            mpinit(void);

// picirq.c
void            picenable(int);
void            picinit(void);

// pipe.c
int             pipealloc(struct file**, struct file**);
void            pipeclose(struct pipe*, int);
int             piperead(struct pipe*, char*, int);
int             pipewrite(struct pipe*, char*, int);

//PAGEBREAK: 16
// proc.c
int             cpuid(void);
void            exit(void);
int             fork(void);
int             freemem(void);
int             growproc(int);
int             kill(int);
struct cpu*     mycpu(void);
struct proc*    myproc();
void            pinit(void);
void            procdump(void);
void            scheduler(void) __attribute__((noreturn));
void            sched(void);
void            setproc(struct proc*);
void            sleep(void*, struct spinlock*);
void            userinit(void);
int             wait(void);
void            wakeup(void*);
void            yield(void);
int             sig(int,int);
void            sighandler(void (*)(int));
void            sigignore(int,int);

// swtch.S
void            swtch(struct context**, struct context*);

// spinlock.c
void            acquire(struct spinlock*);
void            getcallerpcs(void*, uint32*);
int             holding(struct spinlock*);
void            initlock(struct spinlock*, char*);
void            release(struct spinlock*);
void            pushcli(void);
void            popcli(void);

// sleeplock.c
void            acquiresleep(struct sleeplock*);
void            releasesleep(struct sleeplock*);
int             holdingsleep(struct sleeplock*);
void            initsleeplock(struct sleeplock*, char*);
void            initnonblockinglock(struct nonblockinglock *lk, char *name);
int             acquirenonblockinglock(struct nonblockinglock *lk);
void            releasenonblocking(struct nonblockinglock *lk);
// nonblockinglock.c


// string.c
int             memcmp(const void*, const void*, uint32);
void*           memmove(void*, const void*, uint32);
void*           memset(void*, int, uint32);
char*           safestrcpy(char*, const char*, int);
int             strlen(const char*);
int             strncmp(const char*, const char*, uint32);
char*           strncpy(char*, const char*, int);

// syscall.c
int             argint(int, int*);
int             argptr(int, char**, int);
int             argstr(int, char**);
int             fetchint(uint32, int*);
int             fetchstr(uint32, char**);
void            syscall(void);

// timer.c
void            timerinit(void);

// trap.c
void            idtinit(void);
extern uint32     ticks;
void            tvinit(void);
extern struct spinlock tickslock;

// uart.c
void            uartinit(void);
void            uartintr(void);
void            uartputc(int);

// vm.c
void            seginit(void);
void            kvmalloc(void);
pmde_t*          setupkvm(void);
char*           uva2ka(pmde_t*, char*);
int             allocuvm(pmde_t*, uint32, uint32,int);
int             deallocuvm(pmde_t*, uint32, uint32);
void            freevm(pmde_t*);
void            inituvm(pmde_t*, char*, uint32);
int             loaduvm(pmde_t*, char*, struct inode*, uint32, uint32);
pmde_t*          copyuvm(pmde_t*, uint32);
void            switchuvm(struct proc*);
void            switchkvm(void);
int             copyout(pmde_t*, uint32, void*, uint32);
void            clearpteu(pmde_t *pgdir, char *uva);

// number of elements in fixed-size array
#define NELEM(x) (sizeof(x)/sizeof((x)[0]))
#define NULL (void *) 0
