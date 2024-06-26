// File system implementation.  Five layers:
//   + Blocks: allocator for raw disk blocks.
//   + Log: crash recovery for multi-step updates.
//   + Files: inode allocator, reading, writing, metadata.
//   + Directories: inode with special contents (list of other inodes!)
//   + Names: paths like /usr/rtm/xv6/fs.c for convenient naming.
//
// This file contains the low-level file system manipulation
// routines.  The (higher-level) system call implementations
// are in sysfile.c.

#include "../../user/types.h"
#include "../defs/defs.h"
#include "../defs/param.h"
#include "../../user/stat.h"
#include "../arch/x86_32/mem/mmu.h"
#include "../lock/spinlock.h"
#include "../sched/proc.h"
#include "../lock/sleeplock.h"
#include "fs.h"
#include "buf.h"
#include "file.h"
#include "mount.h"

#define min(a, b) ((a) < (b) ? (a) : (b))

static void itrunc(struct inode *);

// there should be one superblock per disk device, but we run with
// only one device
struct superblock sb;
//will be for our second virtual device for testing the mounting / unmounting features I am adding
struct superblock sb2;

// Read the super block.
void
readsb(int dev, struct superblock *sb) {
    struct buf *bp;

    bp = bread(dev, 1);
    memmove(sb, bp->data, sizeof(*sb));
    brelse(bp);
}

// Zero a block.
static void
bzero(int dev, int bno) {
    struct buf *bp;

    bp = bread(dev, bno);
    memset(bp->data, 0, BSIZE);
    log_write(bp);
    brelse(bp);
}

// Blocks.

// Allocate a zeroed disk block.
static uint32
balloc(uint32 dev) {
    struct superblock *superb;

    int b, bi, m;
    struct buf *bp;
    if (dev == 2) {
        bp = 0;
        for (b = 0; b < sb2.size; b += BPB) {
            bp = bread(dev, BBLOCK(b, sb2));
            for (bi = 0; bi < BPB && b + bi < sb2.size; bi++) {
                m = 1 << (bi % 8);
                if ((bp->data[bi / 8] & m) == 0) {  // Is block free?
                    bp->data[bi / 8] |= m;  // Mark block in use.
                    log_write(bp);
                    brelse(bp);
                    bzero(dev, b + bi);
                    return b + bi;
                }
            }
            brelse(bp);
        }
        panic("balloc: out of blocks");
    } else {
        bp = 0;
        for (b = 0; b < sb.size; b += BPB) {
            bp = bread(dev, BBLOCK(b, sb));
            for (bi = 0; bi < BPB && b + bi < sb.size; bi++) {
                m = 1 << (bi % 8);
                if ((bp->data[bi / 8] & m) == 0) {  // Is block free?
                    bp->data[bi / 8] |= m;  // Mark block in use.
                    log_write(bp);
                    brelse(bp);
                    bzero(dev, b + bi);
                    return b + bi;
                }
            }
            brelse(bp);
        }
        panic("balloc: out of blocks");
    }


}

// Free a disk block.
static void
bfree(int dev, uint32 b) {
    struct buf *bp;
    int bi, m;
    if (dev == 2) {
        bp = bread(dev, BBLOCK(b, sb2));
    } else {
        bp = bread(dev, BBLOCK(b, sb));
    }
    bi = b % BPB;
    m = 1 << (bi % 8);
    if ((bp->data[bi / 8] & m) == 0)
        panic("freeing free block");
    bp->data[bi / 8] &= ~m;
    log_write(bp);
    brelse(bp);
}

// Inodes.
//
// An inode describes a single unnamed file.
// The inode disk structure holds metadata: the file's type,
// its size, the number of links referring to it, and the
// list of blocks holding the file's content.
//
// The inodes are laid out sequentially on disk at
// sb.startinode. Each inode has a number, indicating its
// position on the disk.
//
// The kernel keeps a cache of in-use inodes in memory
// to provide a place for synchronizing access
// to inodes used by multiple processes. The cached
// inodes include book-keeping information that is
// not stored on disk: ip->ref and ip->valid.
//
// An inode and its in-memory representation go through a
// sequence of states before they can be used by the
// rest of the file system code.
//
// * Allocation: an inode is allocated if its type (on disk)
//   is non-zero. ialloc() allocates, and iput() frees if
//   the reference and link counts have fallen to zero.
//
// * Referencing in cache: an entry in the inode cache
//   is free if ip->ref is zero. Otherwise ip->ref tracks
//   the number of in-memory pointers to the entry (open
//   files and current directories). iget() finds or
//   creates a cache entry and increments its ref; iput()
//   decrements ref.
//
// * Valid: the information (type, size, &c) in an inode
//   cache entry is only correct when ip->valid is 1.
//   ilock() reads the inode from
//   the disk and sets ip->valid, while iput() clears
//   ip->valid if ip->ref has fallen to zero.
//
// * Locked: file system code may only examine and modify
//   the information in an inode and its content if it
//   has first locked the inode.
//
// Thus a typical sequence is:
//   ip = iget(dev, inum)
//   ilock(ip)
//   ... examine and modify ip->xxx ...
//   iunlock(ip)
//   iput(ip)
//
// ilock() is separate from iget() so that system calls can
// get a long-term reference to an inode (as for an open file)
// and only lock it for short periods (e.g., in read()).
// The separation also helps avoid deadlock and races during
// pathname lookup. iget() increments ip->ref so that the inode
// stays cached and pointers to it remain valid.
//
// Many internal file system functions expect the caller to
// have locked the inodes involved; this lets callers create
// multi-step atomic operations.
//
// The icache.lock spin-lock protects the allocation of icache
// entries. Since ip->ref indicates whether an entry is free,
// and ip->dev and ip->inum indicate which i-node an entry
// holds, one must hold icache.lock while using any of those fields.
//
// An ip->lock sleep-lock protects all ip-> fields other than ref,
// dev, and inum.  One must hold ip->lock in order to
// read or write that inode's ip->valid, ip->size, ip->type, &c.

struct {
    struct spinlock lock;
    struct inode inode[NINODE];
} icache;

struct {
    struct spinlock lock;
    struct inode inode[NINODE];
} icache2;

void
iinit(int dev, int sbnum) {
    int i = 0;

    if (dev == 1) {
        initlock(&icache.lock, "icache");
        for (i = 0; i < NINODE; i++) {
            initsleeplock(&icache.inode[i].lock, "inode");
        }
        readsb(dev, &sb);
        cprintf("sb: size %d nblocks %d ninodes %d nlog %d logstart %d inodestart %d bmap start %d\n", sb.size,
                sb.nblocks, sb.ninodes, sb.nlog, sb.logstart, sb.inodestart, sb.bmapstart);

    } else if (dev == 2) {
        initlock(&icache2.lock, "icache");
        for (i = 0; i < NINODE; i++) {
            initsleeplock(&icache2.inode[i].lock, "inode");
        }

        readsb(dev, &sb2);
        cprintf("sb: size %d nblocks %d ninodes %d nlog %d logstart %d inodestart %d bmap start %d\n", sb2.size,
                sb2.nblocks, sb2.ninodes, sb2.nlog, sb2.logstart, sb2.inodestart, sb2.bmapstart);

    }


}

static struct inode *iget(uint32 dev, uint32 inum);

//PAGEBREAK!
// Allocate an inode on device dev.
// Mark it as allocated by  giving it type type.
// Returns an unlocked but allocated and referenced inode.
struct inode *
ialloc(uint32 dev, short type) {
    int inum;
    struct buf *bp;
    struct dinode *dip;

    if (dev == 2) {
        for (inum = 1; inum < sb2.ninodes; inum++) {
            bp = bread(dev, IBLOCK(inum, sb2));
            dip = (struct dinode *) bp->data + inum % IPB;
            if (dip->type == 0) {  // a free inode
                memset(dip, 0, sizeof(*dip));
                dip->type = type;
                log_write(bp);   // mark it allocated on the disk
                brelse(bp);
                return iget(dev, inum);
            }
            brelse(bp);;
        }
        panic("ialloc: no inodes");
    } else {

        for (inum = 1; inum < sb.ninodes; inum++) {
            bp = bread(dev, IBLOCK(inum, sb));
            dip = (struct dinode *) bp->data + inum % IPB;
            if (dip->type == 0) {  // a free inode
                memset(dip, 0, sizeof(*dip));
                dip->type = type;
                log_write(bp);   // mark it allocated on the disk
                brelse(bp);
                return iget(dev, inum);
            }
            brelse(bp);
        }
        panic("ialloc: no inodes");
    }

}

// Copy a modified in-memory inode to disk.
// Must be called after every change to an ip->xxx field
// that lives on disk, since i-node cache is write-through.
// Caller must hold ip->lock.
void
iupdate(struct inode *ip) {
    //don't update mount points
    if (ip->is_mount_point == 1) {
        return;
    }
    struct buf *bp;
    struct dinode *dip;

    if (ip->dev == 2) {
        bp = bread(ip->dev, IBLOCK(ip->inum, sb2));
    } else {
        bp = bread(ip->dev, IBLOCK(ip->inum, sb));
    }


    dip = (struct dinode *) bp->data + ip->inum % IPB;
    dip->type = ip->type;
    dip->major = ip->major;
    dip->minor = ip->minor;
    dip->nlink = ip->nlink;
    dip->size = ip->size;
    memmove(dip->addrs, ip->addrs, sizeof(ip->addrs));
    log_write(bp);
    brelse(bp);
}

// Find the inode with number inum on device dev
// and return the in-memory copy. Does not lock
// the inode and does not read it from disk.
static struct inode *
iget(uint32 dev, uint32 inum) {
    struct inode *ip, *empty;
    if (dev == 2) {
        acquire(&icache2.lock);
    } else {
        acquire(&icache.lock);
    }


    // Is the inode already cached?
    empty = 0;
    if (dev == 2) {
        for (ip = &icache2.inode[0]; ip < &icache2.inode[NINODE]; ip++) {
            if (ip->ref > 0 && ip->dev == dev && ip->inum == inum) {
                ip->ref++;
                release(&icache2.lock);
                return ip;
            }
            if (empty == 0 && ip->ref == 0)    // Remember empty slot.
                empty = ip;
        }
    } else {
        for (ip = &icache.inode[0]; ip < &icache.inode[NINODE]; ip++) {
            if (ip->ref > 0 && ip->dev == dev && ip->inum == inum) {
                ip->ref++;
                release(&icache.lock);
                return ip;
            }
            if (empty == 0 && ip->ref == 0)    // Remember empty slot.
                empty = ip;
        }
    }


    // Recycle an inode cache entry.
    if (empty == 0)
        panic("iget: no inodes");

    ip = empty;
    ip->dev = dev;
    ip->inum = inum;
    ip->ref = 1;
    ip->valid = 0;

    if (dev == 2) {
        release(&icache2.lock);
    } else {
        release(&icache.lock);
    }

    return ip;
}

// Increment reference count for ip.
// Returns ip to enable ip = idup(ip1) idiom.
struct inode *
idup(struct inode *ip) {

    if (ip->dev == 2) {
        acquire(&icache2.lock);
        ip->ref++;
        release(&icache2.lock);
    } else {
        acquire(&icache.lock);
        ip->ref++;
        release(&icache.lock);
    }

    return ip;
}

// Lock the given inode.
// Reads the inode from disk if necessary.
void
ilock(struct inode *ip) {
    struct buf *bp;
    struct dinode *dip;

    if (ip == 0 || ip->ref < 1)
        panic("ilock");

    acquiresleep(&ip->lock);

    if (ip->valid == 0) {
        if (ip->dev == 2) {
            bp = bread(ip->dev, IBLOCK(ip->inum, sb2));
        } else {
            bp = bread(ip->dev, IBLOCK(ip->inum, sb));

        }
        dip = (struct dinode *) bp->data + ip->inum % IPB;
        ip->type = dip->type;
        ip->major = dip->major;
        ip->minor = dip->minor;
        ip->nlink = dip->nlink;
        ip->size = dip->size;
        memmove(ip->addrs, dip->addrs, sizeof(ip->addrs));
        brelse(bp);
        ip->valid = 1;
        if (ip->type == 0){
            panic("ilock: no type");
        }

    }
}

// Unlock the given inode.
void
iunlock(struct inode *ip) {
    if (ip == 0 || !holdingsleep(&ip->lock) || ip->ref < 1)
        panic("iunlock");

    releasesleep(&ip->lock);
}

// Drop a reference to an in-memory inode.
// If that was the last reference, the inode cache entry can
// be recycled.
// If that was the last reference and the inode has no links
// to it, free the inode (and its content) on disk.
// All calls to iput() must be inside a transaction in
// case it has to free the inode.
void
iput(struct inode *ip) {
    acquiresleep(&ip->lock);
    int r;

    if (ip->valid && ip->nlink == 0) {
        if (ip->dev == 2) {
            acquire(&icache.lock);
            r = ip->ref;
            release(&icache.lock);
        } else {
            acquire(&icache2.lock);
            r = ip->ref;
            release(&icache2.lock);
        }

        if (r == 1) {
            // inode has no links and no other references: truncate and free.
            itrunc(ip);
            ip->type = 0;
            iupdate(ip);
            ip->valid = 0;
        }
    }
    releasesleep(&ip->lock);
    if (ip->dev == 2) {
        acquire(&icache2.lock);
        ip->ref--;
        release(&icache2.lock);
    } else {
        acquire(&icache.lock);
        ip->ref--;
        release(&icache.lock);
    }

}

// Common idiom: unlock, then put.
void
iunlockput(struct inode *ip) {
    iunlock(ip);
    iput(ip);
}

/*
 * Custom mount iput it will just make sure there are no unaccounted-for references when an unmount operation occurs.
 * There should only be 1 reference when unmounting a filesystem for obvious reasons.
 */
int
iputmount(struct inode *ip) {
    //Should be 1 reference, mount root in the mounttable.
    if (ip->ref > 1) {
        return -1;
    }
    iput(ip);
    return 0;
}

//PAGEBREAK!
// Inode content
//
// The content (data) associated with each inode is stored
// in blocks on the disk. The first NDIRECT block numbers
// are listed in ip->addrs[].  The next NINDIRECT blocks are
// listed in block ip->addrs[NDIRECT].

// Return the disk block address of the nth block in inode ip.
// If there is no such block, bmap allocates one.
static uint32
bmap(struct inode *ip, uint32 bn) {
    uint32 addr, *a;
    struct buf *bp;

    if (bn < NDIRECT) {
        if ((addr = ip->addrs[bn]) == 0)
            ip->addrs[bn] = addr = balloc(ip->dev);
        return addr;
    }
    bn -= NDIRECT;

    if (bn < NINDIRECT) {
        // Load indirect block, allocating if necessary.
        if ((addr = ip->addrs[NDIRECT]) == 0)
            ip->addrs[NDIRECT] = addr = balloc(ip->dev);
        bp = bread(ip->dev, addr);
        a = (uint32 *) bp->data;
        if ((addr = a[bn]) == 0) {
            a[bn] = addr = balloc(ip->dev);
            log_write(bp);
        }
        brelse(bp);
        return addr;
    }

    panic("bmap: out of range");
}

// Truncate inode (discard contents).
// Only called when the inode has no links
// to it (no directory entries referring to it)
// and has no in-memory reference to it (is
// not an open file or current directory).
static void
itrunc(struct inode *ip) {
    int i, j;
    struct buf *bp;
    uint32 *a;

    for (i = 0; i < NDIRECT; i++) {
        if (ip->addrs[i]) {
            bfree(ip->dev, ip->addrs[i]);
            ip->addrs[i] = 0;
        }
    }

    if (ip->addrs[NDIRECT]) {
        bp = bread(ip->dev, ip->addrs[NDIRECT]);
        a = (uint32 *) bp->data;
        for (j = 0; j < NINDIRECT; j++) {
            if (a[j])
                bfree(ip->dev, a[j]);
        }
        brelse(bp);
        bfree(ip->dev, ip->addrs[NDIRECT]);
        ip->addrs[NDIRECT] = 0;
    }

    ip->size = 0;
    iupdate(ip);
}

// Copy stat information from inode.
// Caller must hold ip->lock.
void
stati(struct inode *ip, struct stat *st) {
    if (ip->is_mount_point) {
        st->dev = mounttable.mount_root->dev;
        st->ino = mounttable.mount_root->inum;
        st->type = mounttable.mount_root->type;
        st->nlink = mounttable.mount_root->nlink;
        st->size = mounttable.mount_root->size;
    }
    st->dev = ip->dev;
    st->ino = ip->inum;
    st->type = ip->type;
    st->nlink = ip->nlink;
    st->size = ip->size;
}

//PAGEBREAK!
// Read data from inode.
// Caller must hold ip->lock.
int
readi(struct inode *ip, char *dst, uint32 off, uint32 n) {

    uint32 tot, m;
    struct buf *bp;

    if (ip->type == T_DEV) {
        if (ip->major < 0 || ip->major >= NDEV || !devsw[ip->major].read)
            return -1;
        return devsw[ip->major].read(ip, dst, n);
    }

    if (off > ip->size || off + n < off)
        return -1;
    if (off + n > ip->size)
        n = ip->size - off;

    for (tot = 0; tot < n; tot += m, off += m, dst += m) {
        bp = bread(ip->dev, bmap(ip, off / BSIZE));
        m = min(n - tot, BSIZE - off % BSIZE);
        memmove(dst, bp->data + off % BSIZE, m);
        brelse(bp);
    }



    /*
    * Read ahead if applicable, if the caller is reading a lot we will want to read ahead and store the upcoming blocks in the buffer cache early.
     * This isn't going to work as it exists here so I will comment it out for now.
    */
    /*
    if(ip->type == T_FILE){
        int blocks_ahead = ((ip->size - off) / BSIZE);
        cprintf("BLOCKS AHEAD %d FILE SIZE %d OFFSET %d\n", blocks_ahead,ip->size,off);
        if (blocks_ahead > 4) {
            blocks_ahead = 4;
        }
        if (blocks_ahead > 0) {
            struct buf *read_ahead_buffers;
           read_ahead_buffers = breada(ip->dev, bmap(ip, off / BSIZE), blocks_ahead);


        }
    }
     */


    return n;
}

// PAGEBREAK!
// Write data to inode.
// Caller must hold ip->lock.
int
writei(struct inode *ip, char *src, uint32 off, uint32 n) {
    uint32 tot, m;
    struct buf *bp;

    if (ip->type == T_DEV) {
        if (ip->major < 0 || ip->major >= NDEV || !devsw[ip->major].write)
            return -1;
        return devsw[ip->major].write(ip, src, n);
    }

    if (off > ip->size || off + n < off)
        return -1;
    if (off + n > MAXFILE * BSIZE)
        return -1;

    for (tot = 0; tot < n; tot += m, off += m, src += m) {
        bp = bread(ip->dev, bmap(ip, off / BSIZE));
        m = min(n - tot, BSIZE - off % BSIZE);
        memmove(bp->data + off % BSIZE, src, m);
        log_write(bp);
        brelse(bp);
    }

    if (n > 0 && off > ip->size) {
        ip->size = off;
        iupdate(ip);
    }

    return n;
}

//PAGEBREAK!
// Directories

int
namecmp(const char *s, const char *t) {
    return strncmp(s, t, DIRSIZ);
}

// Look for a directory entry in a directory.
// If found, set *poff to byte offset of entry.
struct inode *
dirlookup(struct inode *dp, char *name, uint32 *poff, uint32 leaving_mount) {

    if (!leaving_mount) {
        if (dp->is_mount_point) {
            dp = mounttable.mount_root;
        }
    }

    uint32 off, inum;
    struct dirent de;

    if (dp->type != T_DIR)
        panic("dirlookup not DIR");

    for (off = 0; off < dp->size; off += sizeof(de)) {
        if (readi(dp, (char *) &de, off, sizeof(de)) != sizeof(de))
            panic("dirlookup read");
        if (de.inum == 0)
            continue;
        if (namecmp(name, de.name) == 0) {
            // entry matches path element
            if (poff)
                *poff = off;
            inum = de.inum;
            return iget(dp->dev, inum);
        }
    }

    return 0;
}

// Write a new directory entry (name, inum) into the directory dp.
int
dirlink(struct inode *dp, char *name, uint32 inum) {
    if (dp->is_mount_point) {
        dp = mounttable.mount_root;
    }
    int off;
    struct dirent de;
    struct inode *ip;

    // Check that name is not present.
    if ((ip = dirlookup(dp, name, 0,0)) != 0) {
        iput(ip);
        return -1;
    }

    // Look for an empty dirent.
    for (off = 0; off < dp->size; off += sizeof(de)) {
        if (readi(dp, (char *) &de, off, sizeof(de)) != sizeof(de))
            panic("dirlink read");
        if (de.inum == 0)
            break;
    }

    strncpy(de.name, name, DIRSIZ);
    de.inum = inum;
    dp->type = 1;
    if (writei(dp, (char *) &de, off, sizeof(de)) != sizeof(de))
        panic("dirlink");

    iupdate(dp);
    return 0;
}

//PAGEBREAK!
// Paths

// Copy the next path element from path into name.
// Return a pointer to the element following the copied one.
// The returned path has no leading slashes,
// so the caller can check *path=='\0' to see if the name is the last one.
// If no name to remove, return 0.
//
// Examples:
//   skipelem("a/bb/c", name) = "bb/c", setting name = "a"
//   skipelem("///a//bb", name) = "bb", setting name = "a"
//   skipelem("a", name) = "", setting name = "a"
//   skipelem("", name) = skipelem("////", name) = 0
//
static char *
skipelem(char *path, char *name) {
    char *s;
    int len;

    while (*path == '/')
        path++;
    if (*path == 0)
        return 0;
    s = path;
    while (*path != '/' && *path != 0)
        path++;
    len = path - s;
    if (len >= DIRSIZ)
        memmove(name, s, DIRSIZ);
    else {
        memmove(name, s, len);
        name[len] = 0;
    }
    while (*path == '/')
        path++;
    return path;
}

// Look up and return the inode for a path name.
// If parent != 0, return the inode for the parent and copy the final
// path element into name, which must have room for DIRSIZ bytes.
// Must be called inside a transaction since it calls iput().
static struct inode *
namex(uint32 dev, char *path, int nameiparent, char *name) {
    struct inode *ip, *next;

    if (*path == '/') {
        ip = iget(dev, ROOTINO);
    } else
        ip = idup(myproc()->cwd);


    while ((path = skipelem(path, name)) != 0) {
        ilock(ip);
        if (ip->type != T_DIR) {
            iunlockput(ip);
            return 0;
        }

        if (nameiparent && *path == '\0') {
            // Stop one level early.
            iunlock(ip);
            return ip;
        }

        if ((next = dirlookup(ip, name, 0,1)) == 0) {
            iunlockput(ip);
            return 0;
        }
        if (!nameiparent && next->is_mount_point) {
            next = idup(mounttable.mount_root);
        }
        iunlockput(ip);
        ip = next;
    }
    if (nameiparent) {
        iput(ip);
        return 0;
    }

    return ip;
}

struct inode *
namei(uint32 dev, char *path) {
    if (dev == 0) {
        dev = myproc()->cwd->dev;
    }
    char name[DIRSIZ];

    //Is this inside the root of a mount point referencing the parent inode? If so, we need to move across mount points
    // I should get this set up to get the parent however I will need to write a new function I believe since all getting of parent inode is done via path, which I won't have a path just
    //an inode pointer. This solution is fine for now.
    if (dev > 1 && (*path == '.' && path[1] == '.') && (myproc()->cwd->inum == ROOTINO)) {
        struct inode *old_cwd = myproc()->cwd;
        myproc()->cwd = mounttable.mount_point;
        iput(old_cwd);
        struct inode *new = namex(1, "../..", 1, name);
        myproc()->cwd = new;
        iput(old_cwd);
        return new;
    }
    return namex(dev, path, 0, name);
}

struct inode *
nameiparent(uint32 dev, char *path, char *name) {
    return namex(dev, path, 1, name);
}

struct inode *
iparent(struct inode *parent) {

}