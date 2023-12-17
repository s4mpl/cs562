/**
 * @file stat.h
 * @author Stephen Marz (sgm@utk.edu)
 * @brief File system stat structures.
 * @version 0.1
 * @date 2022-05-19
 *
 * @copyright Copyright (c) 2022
 *
 */
#pragma once

#include <stdint.h>

#define MAX_FILE_NAME      60

#define S_FMT(x)       (S_IFMT & (x))

#define S_IFMT        0170000 /* These bits determine file type.  */

/* File types.  */
#define S_IFDIR       0040000 /* Directory.  */
#define S_IFCHR       0020000 /* Character device.  */
#define S_IFBLK       0060000 /* Block device.  */
#define S_IFREG       0100000 /* Regular file.  */
#define S_IFIFO       0010000 /* FIFO.  */
#define S_IFLNK       0120000 /* Symbolic link.  */
#define S_IFSOCK      0140000 /* Socket.  */

/* Protection bits.  */

#define S_ISUID       04000   /* Set user ID on execution.  */
#define S_ISGID       02000   /* Set group ID on execution.  */
#define S_ISVTX       01000   /* Save swapped text after use (sticky).  */
#define S_IREAD       0400    
#define S_IWRITE      0200    
#define S_IEXEC       0100    


#define S_ISLNK(m)    (((m) & S_IFMT) == S_IFLNK)
#define S_ISREG(m)    (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m)    (((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m)    (((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m)    (((m) & S_IFMT) == S_IFBLK)
#define S_ISFIFO(m)    (((m) & S_IFMT) == S_IFIFO)
#define S_ISSOCK(m)    (((m) & S_IFMT) == S_IFSOCK)
#define S_IRWXU 00700
#define S_IRUSR 00400 /* Read by owner.  */
#define S_IWUSR 00200 /* Write by owner.  */
#define S_IXUSR 00100 /* Execute by owner.  */
#define S_IRWXG 00070
#define S_IRGRP 00040
#define S_IWGRP 00020
#define S_IXGRP 00010
#define S_IRWXO 00007
#define S_IROTH 00004
#define S_IWOTH 00002
#define S_IXOTH 00001

#define WRITE_PERMISSION (S_IWUSR | S_IWGRP | S_IWOTH)
#define READ_PERMISSION (S_IRUSR | S_IRGRP | S_IROTH)

typedef unsigned long  dev_t;
typedef unsigned int uid_t;
typedef unsigned int gid_t;
typedef unsigned int gid_t;
typedef long blkcnt_t;
typedef unsigned long ino_t;
typedef signed long    off_t;

typedef struct m_stat {
  dev_t      st_dev;      //This field represents the ID of the device containing the file. Obtained from the filesystem or device driver.(needs a function)
  ino_t      st_ino;      //The inode number. From the filesystem's management of inodes. (needs a function)
  unsigned short st_mode;     //File type and mode (permissions). This directly corresponds to the mode field in Inode.   
  unsigned long st_nlink;    //Number of hard links. This corresponds to the nlinks field in Inode. 
  uid_t      st_uid;      //User ID of the file's owner. This maps to the uid field in Inode.
  gid_t      st_gid;      //Group ID of the owner. This maps to the gid field in Inode.
  off_t      st_size;     //Total size of the file in bytes. This corresponds to the size field in Inode.
  long       st_blksize;  //Preferred block size for filesystem I/O. Determined by the filesystem or storage device characteristics. (needs a function) (might not need)
  blkcnt_t   st_blocks;   //Number of 512-byte blocks allocated. Calculate this based on the zones array in Inode. 
  uint32_t   st_atim;     //Time of last access. This and the others modified from the actual timespec struct because we don't have nanoseconds to deal with. 
  uint32_t   st_mtim;     //Time of last modification 
  uint32_t   st_ctim;     //Time of last status change. 
} m_stat;
