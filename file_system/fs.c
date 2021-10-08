#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "disk.h"
#include "fs.h"

#define MAX_FILES 64
#define MAX_FILDES 32 
#define MAX_FILESIZE (1 << 20) // file size = 1MB
#define MAX_FILENAME 15

/* data structures */
/* information about where to find the file system and its data structures */
struct superblock
{
    uint16_t block_bitmap_size;
    uint16_t block_bitmap_offset;
    uint16_t inode_bitmap_size;
    uint16_t inode_bitmap_offset;
    uint16_t dir_entry_size;
    uint16_t dir_entry_offset;
    uint16_t inode_offset;
    uint16_t inode_size;
    uint16_t data_block_offset;
};

/* attributes of inode/file */
struct inode 
{
    int file_type;
    int size;
    int direct_offset;  // offset to direct blocks (1MB/4kB)
    //int single_indirect_offset; // offset to a single direct block
};

/* if the inode points to a directory entry 
when taking data out of this block interpret it as a dir entry (root)
*/
struct dir_entry
{
    bool used;
    int inode_num;
    char name[MAX_FILENAME + 1];
};

/* if the inode points to a file */
struct fd_t
{
    bool used;
    int inode_num; // first block of the file that fd refers to
    int offset; // offset to the byte being looked at in the file (track position)
};

/* global variables */
uint8_t blocks_bitmap[1024]; // free list for used disk blocks (DISK_BLOCKS/8)
struct inode inode_bitmap[MAX_FILES]; // inode table (array cache/in-core copy of inodes)
struct fd_t fd[MAX_FILDES]; // array of open file descriptors 
struct superblock sb; // current state of the superblock (to know block offsets)
struct dir_entry DIR[MAX_FILES]; // array of directory entries
static bool mounted = false;


/* 
 * Helper Functions
 */

int next_block(int curr, char idx){
    char *buf = calloc(1, BLOCK_SIZE);

    if (curr < BLOCK_SIZE){
        block_read(sb.data_block_offset, buf);
        for(int i = curr + 1; i < BLOCK_SIZE; i++) {
            if (buf[i] == (idx + 1)){
                return i;
            }
        }
    } else {
        block_read(sb.data_block_offset + 1, buf);
        for(int i = curr - BLOCK_SIZE + 1; i < BLOCK_SIZE; i++) {
            if (buf[i] == (idx + 1)){
                return i + BLOCK_SIZE;
            }
        }
    }

    return -1; // no next block
}

/* 
 * Management Routines
 */

/* creates a fresh (and empty) file system on the virtual disk */
int make_fs(const char *disk_name)
{   
    if (make_disk(disk_name) == -1) {
        perror("ERROR: make_disk");
        return -1;
    }

    if (open_disk(disk_name) == -1) {
        perror("ERROR: open_disk");
        return -1;
    }

    /* initialize meta-information */
    struct superblock sb_;
    sb_.dir_entry_size = (sizeof(struct dir_entry) * MAX_FILES) % BLOCK_SIZE == 0 ? \
                         (sizeof(struct dir_entry) * MAX_FILES) / BLOCK_SIZE : \
                         (sizeof(struct dir_entry) * MAX_FILES) / BLOCK_SIZE + 1;
    sb_.dir_entry_offset = 1; 

    sb_.inode_bitmap_size = sizeof(inode_bitmap) % BLOCK_SIZE == 0 ? \
                            sizeof(inode_bitmap) / BLOCK_SIZE : \
                            sizeof(inode_bitmap) / BLOCK_SIZE + 1;
    sb_.inode_bitmap_offset = sb_.dir_entry_offset + sb_.dir_entry_size; 

    sb_.block_bitmap_size = sizeof(blocks_bitmap) % BLOCK_SIZE == 0 ? \
                            sizeof(blocks_bitmap) / BLOCK_SIZE : \
                            sizeof(blocks_bitmap) / BLOCK_SIZE + 1;
    sb_.block_bitmap_offset = sb_.inode_bitmap_offset + sb_.inode_bitmap_size;

    sb_.inode_size = (sizeof(struct inode) * MAX_FILES) % BLOCK_SIZE == 0 ? \
                     (sizeof(struct inode) * MAX_FILES) / BLOCK_SIZE : \
                     (sizeof(struct inode) * MAX_FILES) / BLOCK_SIZE + 1;
    sb_.inode_offset = sb_.block_bitmap_offset + sb_.block_bitmap_size;

    sb_.data_block_offset = sb_.inode_offset + sb_.inode_size;

    /* copy meta-information to disk blocks */
    char *buffer = calloc(1, BLOCK_SIZE);

    /* block write superblock */
    memcpy((void *)buffer, (void *)&sb, sizeof(struct superblock));
    if (block_write(0, buffer) == -1) {
        perror("ERROR: block_read");
        return -1;
    }

    /* block write DIR */
    struct dir_entry dir = {.used = false, .name[0] = '\0', .inode_num = 0}; // init empty entry
    for (int i = 0; i < MAX_FILES; i++) {
        memcpy((void *)(buffer + i * sizeof(struct dir_entry)), (void *)&dir, sizeof(struct dir_entry)); // populate single directory block buffer with empty entries
    }
    if (block_write(sb_.dir_entry_offset, buffer) == -1) {
        perror("ERROR: block_write");
        return -1;
    }

    /* block write inode bitmap*/
    struct inode in = {.file_type = 0, .size = 0, .direct_offset = 0};
    for (int i = 0; i < MAX_FILES; i++) {
        memcpy((void *)(buffer + i * sizeof(struct inode)), (void *)&in, sizeof(struct inode)); // populate buffer with empty inode entries
    }
    if (block_write(sb_.inode_bitmap_offset, buffer) == -1) {
        perror("ERROR: block_write");
        return -1;
    }

    /* block write blocks bitmap*/
    memcpy((void *)buffer, (void *)&blocks_bitmap, sizeof(blocks_bitmap));
    if (block_write(sb_.block_bitmap_offset, buffer) == -1) {
        perror("ERROR: block_read");
        return -1;
    }


    if (close_disk(disk_name) == -1) {
        perror("ERROR: close");
        return -1;
    }

    return 0;
}

/* mounts a file system on virtual disk */
int mount_fs(const char *disk_name)
{   
    if (open_disk(disk_name) == -1) {
        perror("ERROR: open_disk");
        return -1;
    }

    if (mounted == true) {
        perror("ERROR: disk already mounted");
        return -1;
    }

    char *buffer = calloc(1, BLOCK_SIZE);
    
    /* mount superblock */
    if (block_read(0, buffer) == -1) {
        perror("ERROR: block_read");
        return -1;
    }
    memcpy((void *)&sb, (void *)buffer, sizeof(struct superblock));

    /* mount DIR */
    if (block_read(sb.dir_entry_offset, buffer) == -1) {
        perror("ERROR: block_read");
        return -1;
    }
    for (int i = 0; i < MAX_FILES; i++) {
        memcpy((void *)DIR, (void *)buffer, sizeof(struct dir_entry));
    }

    /* mount inode bitmap */
    if (block_read(sb.inode_bitmap_offset, buffer) == -1) {
        perror("ERROR: block_read");
        return -1;
    }
    memcpy((void *)inode_bitmap, (void *)buffer, MAX_FILES);

    /* mount disk blocks bitmap */
    if (block_read(sb.block_bitmap_offset, buffer) == -1) {
        perror("ERROR: block_read");
        return -1;
    }
    memcpy((void *)blocks_bitmap, (void *)buffer, sizeof(blocks_bitmap));

    /* initialize file descriptor array for local use */
    for (int i = 0; i < MAX_FILDES; i++) {
        fd[i].used = false;
        fd[i].inode_num = -1;
        fd[i].offset = 0;
    }

    mounted = true;
    return 0;
}

/* unmounts a file system stored on virtual disk */
int umount_fs(const char *disk_name) 
{   
    char* buffer = calloc(1, BLOCK_SIZE);

    /* write back superblock */
    memcpy((void *)buffer, (void *)&sb, sizeof(struct superblock));
    if (block_write(0, buffer) == -1) {
        perror("ERROR: block_write");
        return -1;
    }

    /* write back DIR */
    for (int i = 0; i < MAX_FILES; i++) {
        memcpy((void *)(buffer + i * sizeof(struct dir_entry)), (void *)DIR, sizeof(struct dir_entry)); 
    }
    if (block_write(sb.dir_entry_offset, buffer) == -1) {
        perror("ERROR: block_write");
        return -1;
    }

    /* write back inode bitmap */
    for (int i = 0; i < MAX_FILES; i++) {
        memcpy((void *)(buffer + i * sizeof(struct inode)), (void *)inode_bitmap, sizeof(struct inode));
    }
    if (block_write(sb.inode_bitmap_offset, buffer) == -1) {
        perror("ERROR: block_write");
        return -1;
    }

    /* write back disk blocks bitmap */
    memcpy((void *)buffer, (void *)blocks_bitmap, sizeof(blocks_bitmap));
    if (block_write(sb.block_bitmap_offset, buffer) == -1) {
        perror("ERROR: block_write");
        return -1;
    }

    /* fd not written since not persistent across mounts */

    if (close_disk(disk_name) == -1) {
        perror("ERROR: close");
        return -1;
    }

    return 0;
}


/* 
 * File System Functions 
 */

/* file is opened for reading and writing 
file descriptor corresponding to this file is returned */
int fs_open(const char *name) 
{
    /* find the first unused fd */
    int idx;
    for (idx = 0; idx < MAX_FILDES; idx++) {
        if (fd[idx].used == false) {
            break;
        }
        else if (idx == MAX_FILDES - 1) {
            perror("ERROR: max open fds");
            return -1;
        }
    }

    /* find in directory */
    for (int i = 0; i < MAX_FILES; i++) {
        if (strcmp(name, DIR[i].name) == 0){
            fd[idx].used = true;
            fd[idx].inode_num = idx;
            fd[idx].offset = 0;
            break;
        }
        else if (i == MAX_FILES - 1) {
            perror("ERROR: filename not found");
            return -1;
        }
    }

    return idx; 
}

/* file descriptor fd is closed */
int fs_close(int fds)
{   
    if (fds < 0 || fds >= MAX_FILDES || !fd[fds].used) {
        return -1;
    }

    fd[fds].used = false;
    fd[fds].inode_num = -1;
    fd[fds].offset = 0;
    
    return 0;
}

/* creates a new file in the root directory of file system */
int fs_create(const char *name) 
{
    if (strlen(name) > MAX_FILENAME) {
        perror("ERROR: filename too long");
        return -1;
    }

    int i, fc = 0;
    for (i = 0; i < MAX_FILES; i++) {
        if (DIR[i].used == true) {
            if (strcmp(name, DIR[i].name) == 0) {
                perror("ERROR: filename already exists");
                return -1;
            }
            fc++;
        }
    }

    if (fc == MAX_FILES) {
        perror("ERROR: max files created");
        return -1;
    }

    for (i = 0; i < MAX_FILES; i++) {
        if (DIR[i].used == false) {
            DIR[i].used = true;
            strcpy(DIR[i].name, name);
            DIR[i].inode_num = i;
            break;
        }
        else if (i == MAX_FILES - 1) {
            perror("ERROR: root dir full");
            return -1;
        }
    }

    return 0;
}

/*  deletes the file with name name from the root directory of file system 
frees all data blocks and meta-information that correspond to that file */
int fs_delete(const char *name)
{
    int idx;
    for (idx = 0; idx < MAX_FILES; idx++) {
        if (strcmp(DIR[idx].name, name) == 0) {
            if (fd[idx].used == true) {
                perror("ERROR: open files with name");
                return -1;
            }
            break;
        }
        else if (idx == MAX_FILES - 1) {
            perror("ERROR: filename does not exist");
            return -1;
        }
    }

    /* clear blocks from used block bitmap */
    for (int i = 0; i < inode_bitmap[idx].direct_offset; i++) {
        //blocks_bitmap[idx] = 0;
    }


    /* clear file descriptor */
    for (int i = 0; i < MAX_FILES; i++) {
        if (DIR[i].inode_num == i) {
            fd[i].used = false;
            fd[i].inode_num = -1;
            fd[i].offset = 0;
        }
    }

    /* clear directory entry */
    DIR[idx].used = false;
    memset(DIR[idx].name, '\0', MAX_FILENAME);
    DIR[idx].inode_num = -1;  

    return 0;
}

/* attempts to read nbyte bytes of data from the file 
referenced by the descriptor fd into the buffer pointed to by buf */
int fs_read(int fds, void *buf, size_t nbyte)
{
    if (fds < 0 || fds >= MAX_FILDES || !fd[fds].used) {
        perror("ERROR: invalid fd fs_read");
        return -1;
    }

    if (nbyte <= 0) {
        perror("ERROR: invalid nbyte");
        return -1;
    }

    int idx;
    for (idx = 0; idx < MAX_FILES; idx++) {
        if (DIR[idx].inode_num == fds) { // index of directory referred to by fds
            break;
        }
    }

    char* buffer = calloc(1, BLOCK_SIZE * ((inode_bitmap[idx].size - 1) / BLOCK_SIZE + 1)); // allocate in multiples of blocks

    for (int i = 0; i < (inode_bitmap[idx].size - 1)/BLOCK_SIZE + 1; i++) { // iter over number of blocks in buffer
        if (block_read(DIR[idx].inode_num, buffer + i * BLOCK_SIZE) == -1) {
            perror("ERROR: block_read");
            return -1;
        }
    }
    memcpy(buf, (void *)(buffer + fd[fds].offset), nbyte);

    if (fd[fds].offset + nbyte > inode_bitmap[idx].size) { // read does not go out of bounds of filesize
        //int offset = fd[fds].offset;
        fd[fds].offset = inode_bitmap[idx].size;
        //return inode_bitmap[idx].size - offset;
    }

    fd[fds].offset += nbyte;
    return (int)nbyte;
}

/* attempts to write nbyte bytes of data from the file 
referenced by the descriptor fd into the buffer pointed to by buf */
int fs_write(int fds, void *buf, size_t nbyte)
{   
    if (fds < 0 || fds >= MAX_FILDES || !fd[fds].used) {
        perror("ERROR: invalid fd fs_write");
        return -1;
    }

    int idx;
    for (idx = 0; idx < MAX_FILES; idx++) {
        if (DIR[idx].inode_num == fds) { // index of directory referred to by fds
            break;
        }
    }

    /* check if nbyte exceeds file size limit of 1MB */
    if (fd[fds].offset + nbyte > MAX_FILESIZE) {
        nbyte = MAX_FILESIZE - fd[fds].offset;
    }

    /* new file size */
    if (fd[fds].offset + nbyte > inode_bitmap[idx].size) {
        inode_bitmap[idx].size = fd[fds].offset + nbyte;
    }

    char *buffer = calloc(1, BLOCK_SIZE * ((inode_bitmap[idx].size - 1) / BLOCK_SIZE + 1));  // allocate as multiples of blocks

    /* copy existing blocks over */
    for (int i = 0; i < (inode_bitmap[idx].size - 1) / BLOCK_SIZE + 1; i++) {  // empty blocks do not matter
        if (block_read(sb.inode_bitmap_offset, buffer + i * BLOCK_SIZE) == -1) {   
            perror("fs_write: block_read()");
            return -1;
        }
    }
    /* memcpy new bytes to write into buffer */
    memcpy((void *)(buffer + fd[fds].offset), buf, nbyte);

    /* write entire buffer back to respective blocks */
    for (int i = 0; i < (inode_bitmap[idx].size - 1) / BLOCK_SIZE + 1; ++i) { 
        if (block_write(sb.inode_bitmap_offset, buffer + i * BLOCK_SIZE) == -1) { 
            perror("fs_write: block_write()");
            return -1;
        }
    }

    /* increment offset for next op */
    fd[fds].offset += nbyte;
    return nbyte;
}

/* returns the current size of the file referenced by the file descriptor fd */
int fs_get_filesize(int fds)
{
    if (fds < 0 || fds > MAX_FILDES || !fd[fds].used) {
        perror("ERROR: invalid fd");
        return -1;
    }

    int idx;
    for (idx = 0; idx < MAX_FILES; idx++) {
        if (DIR[idx].inode_num == fds) { // index of directory referred to by fds
            break;
        }
    }

    if (idx == -1) {
        perror("ERROR: fs_get_filesize");
        return -1;
    }

    return inode_bitmap[idx].size;
}

/* creates and populates an array of all filenames currently known to the file system */
int fs_listfiles(char ***files)
{
    char **list = calloc(MAX_FILES, sizeof(char *));
    int idx = 0;
    for (int i = 0; i < MAX_FILES; ++i) {
        if (DIR[i].used == true) {
            list[idx] = calloc(MAX_FILENAME, sizeof(char));
            strcpy(list[idx], DIR[i].name);
            idx++;
        }
    }

    list[idx] = NULL;
    *files = list;

    return 0;
}

/*sets the file pointer (the offset used for read and write operations) 
associated with the file descriptor fd to the argument offset */
int fs_lseek(int fds, off_t offset)
{
    if (fds < 0 || fds > MAX_FILDES || !fd[fds].used) {
        perror("ERROR: invalid fd");
        return -1;
    }

    int idx;
    for (idx = 0; idx < MAX_FILES; idx++) {
        if (DIR[idx].inode_num == fds) { // index of directory referred to by fds
            break;
        }
    }

    if (offset < 0 || offset > inode_bitmap[idx].size) { 
        perror("ERROR: invalid offset");
        return -1;
    }

    fd[fds].offset = offset;
    return 0;
}

/* causes the file referenced by fd to be truncated to length bytes in size */
int fs_truncate(int fds, off_t length)
{
    if (fds < 0 || fds > MAX_FILDES || !fd[fds].used) {
        perror("ERROR: invalid fd");
        return -1;
    }

    int idx;
    for (idx = 0; idx < MAX_FILES; idx++) {
        if (DIR[idx].inode_num == fds) { // index of directory referred to by fds
            break;
        }
    }

    if (length > inode_bitmap[idx].size) {
        perror("ERROR: invalid length");
        return -1;
    }

    /* free blocks */
    int nb = (uint16_t)(length + BLOCK_SIZE - 1) / BLOCK_SIZE;
    int b_idx = DIR[idx].inode_num;
    for (int i = 0; i < nb; i++) {
        b_idx = next_block(b_idx, fd[idx].inode_num);
    }
    while (idx > 0){
        char* buf = calloc(1, BLOCK_SIZE);
        if (b_idx < BLOCK_SIZE){
            block_read(sb.data_block_offset, buf);
            buf[b_idx] = '\0';
            block_write(sb.data_block_offset, buf);
        } 
        else {
            block_read(sb.data_block_offset + 1, buf);
            buf[b_idx - BLOCK_SIZE] = '\0';
            block_write(sb.data_block_offset + 1, buf);
        }
        b_idx = next_block(b_idx, fd[idx].inode_num);
    }

     /* modify file information */
    inode_bitmap[idx].size = (int)length;

    /* truncate fd offset */
    for(int i = 0; i < MAX_FILDES; i++) {
        if(fd[i].used == true && fd[i].inode_num == fd[idx].inode_num) {
            fd[i].offset = (int)length;
        }
    }

    return 0;
}
