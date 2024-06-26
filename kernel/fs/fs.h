// On-disk file system format.
// Both the kernel and user programs use this header file.

/*
 * The root inode is stored as a global variable in unix systems in order to always be able to map
 * inodes from root no matter where you are in the file system.
 */
#define ROOTINO 1  // root i-number

#define BSIZE 512  // block size

// Disk layout:
// [ boot block | super block | log | inode blocks |
//                                          free bit map | data blocks]
//
// mkfs computes the super block and builds an initial file system. The
// super block describes the disk layout:
struct superblock {
  uint32 size;         // Size of file system image (blocks)
  uint32 nblocks;      // Number of data blocks
  uint32 ninodes;      // Number of inodes.
  uint32 nlog;         // Number of log blocks
  uint32 logstart;     // Block number of first log block
  uint32 inodestart;   // Block number of first inode block
  uint32 bmapstart;    // Block number of first free map block
};

#define NDIRECT 12
#define NINDIRECT (BSIZE / sizeof(uint32))
#define MAXFILE (NDIRECT + NINDIRECT)

// On-disk inode structure
struct dinode {
  short type;           // File type
  short major;          // Major device number (T_DEV only)
  short minor;          // Minor device number (T_DEV only)
  short nlink;          // Number of links to inode in file system
  uint32 size;            // Size of file (bytes)
  uint32 addrs[NDIRECT+1];   // Data block addresses
};

// Inodes per block.
#define IPB           (BSIZE / sizeof(struct dinode))

// Block containing inode i
#define IBLOCK(i, sb)     ((i) / IPB + sb.inodestart)

// Bitmap bits per block
#define BPB           (BSIZE*8)

// Block of free map containing bit for block b
#define BBLOCK(b, sb) (b/BPB + sb.bmapstart)

// Directory is a file containing a sequence of dirent structures.
#define DIRSIZ 14

struct dirent {
  uint16 inum;
  char name[DIRSIZ];
};

