/*
 *  Copyright (C) 2023 CS416 Rutgers CS
 *	Tiny File System
 *	File:	rufs.c
 *
 */

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/time.h>
#include <libgen.h>
#include <limits.h>

#include "block.h"
#include "rufs.h"

char diskfile_path[PATH_MAX];

// Declare your in-memory data structures here

/* 
 * Get available inode number from bitmap
 */
int get_avail_ino() {

	// Step 1: Read inode bitmap from disk
	bitmap_t inode_bitmap = (unsigned char*) malloc(BLOCK_SIZE);
	bio_read(superblock->i_bitmap_blk, inode_bitmap);
	
	// Step 2: Traverse inode bitmap to find an available slot
	int ino = 0;
	for(; ino < superblock->max_inum && get_bitmap(inode_bitmap, ino); ino++) {}
	
	// Step 3: Update inode bitmap and write to disk
	if (ino < superblock->max_inum) {
		set_bitmap(inode_bitmap, ino);
		bio_write(superblock->i_bitmap_blk, inode_bitmap);
		return ino;
	}
	printf("oh no\n");
	return -1;
}

/* 
 * Get available data block number from bitmap
 */
int get_avail_blkno() {

	// Step 1: Read data block bitmap from disk
	
	
	// Step 2: Traverse data block bitmap to find an available slot

	// Step 3: Update data block bitmap and write to disk 

	return 0;
}

/* 
 * inode operations
 */
int readi(uint16_t ino, struct inode *inode) {

  // Step 1: Get the inode's on-disk block number
  	int block_num = superblock->i_start_blk + (ino)/(BLOCK_SIZE/sizeof(inode));

  // Step 2: Get offset of the inode in the inode on-disk block
	int offset = ino % (BLOCK_SIZE/sizeof(inode));
  // Step 3: Read the block from disk and then copy into inode structure
  	unsigned char* desired_block = (unsigned char*)malloc(BLOCK_SIZE);
	bio_read(block_num, desired_block);

	unsigned char * ptr = desired_block + sizeof(inode)*offset;
	memcpy(inode, ptr, sizeof(index_node));

	return 0;
}

int writei(uint16_t ino, struct inode *inode) {

	// Step 1: Get the block number where this inode resides on disk
	
	// Step 2: Get the offset in the block where this inode resides on disk

	// Step 3: Write inode to disk 

	return 0;
}


/* 
 * directory operations
 */
int dir_find(uint16_t ino, const char *fname, size_t name_len, struct dirent *dirent) {

  // Step 1: Call readi() to get the inode using ino (inode number of current directory)

  // Step 2: Get data block of current directory from inode

  // Step 3: Read directory's data block and check each directory entry.
  //If the name matches, then copy directory entry to dirent structure

	return 0;
}

int dir_add(struct inode dir_inode, uint16_t f_ino, const char *fname, size_t name_len) {

	// Step 1: Read dir_inode's data block and check each directory entry of dir_inode
	
	// Step 2: Check if fname (directory name) is already used in other entries

	// Step 3: Add directory entry in dir_inode's data block and write to disk

	// Allocate a new data block for this directory if it does not exist

	// Update directory inode

	// Write directory entry

	return 0;
}

int dir_remove(struct inode dir_inode, const char *fname, size_t name_len) {

	// Step 1: Read dir_inode's data block and checks each directory entry of dir_inode
	
	// Step 2: Check if fname exist

	// Step 3: If exist, then remove it from dir_inode's data block and write to disk

	return 0;
}

/* 
 * namei operation
 */
int get_node_by_path(const char *path, uint16_t ino, struct inode *inode) {
	
	// Step 1: Resolve the path name, walk through path, and finally, find its inode.
	// Note: You could either implement it in a iterative way or recursive way

	return 0;
}

/* 
 * Make file system
 */
int rufs_mkfs() {

	// Call dev_init() to initialize (Create) Diskfile
	printf("prickface\n");
	dev_init(diskfile_path);
	
	// write superblock information
	sb* supahblock = (sb*)malloc(BLOCK_SIZE); // starts at block 0

	supahblock->magic_num = MAGIC_NUM;
	supahblock->max_inum = MAX_INUM;	
	supahblock->max_dnum = MAX_DNUM;
	supahblock->i_bitmap_blk = 1; // starts at block 1
	supahblock->d_bitmap_blk = 2; // starts at block 2
	supahblock->i_start_blk = 3; // starts at block 3
	/*
		total_inode_blocks = (total_inode_bytes/BLOCK_SIZE) + 1
		total_inode_bytes = sizeof(index_node)*supahblock->max_inum
	*/
	int total_bytes = (sizeof(index_node)*(supahblock->max_inum));
	int total_inode_blocks = (total_bytes % BLOCK_SIZE == 0) ? (total_bytes/BLOCK_SIZE) : (total_bytes/BLOCK_SIZE) + 1;
	supahblock->d_start_blk = supahblock->i_start_blk + total_inode_blocks;

	// initialize inode bitmap
	bitmap_t inode_bitmap = malloc(supahblock->max_inum/8); // "For completition purposes"

	// initialize data block bitmap
	bitmap_t dblock_bitmap = malloc(supahblock->max_dnum); // "For completition purposes"

	// update bitmap information for root directory
	set_bitmap(inode_bitmap, 0);

	// update inode for root directory
	index_node* root_dir = (index_node*)malloc(sizeof(index_node));
	root_dir->ino = 0;
	direntry* root_dirents = malloc(BLOCK_SIZE);
	root_dir->direct_ptr[0] = supahblock->d_start_blk;
	set_bitmap(dblock_bitmap, 0);

	bio_write(0, supahblock);
	bio_write(supahblock->i_bitmap_blk, inode_bitmap);
	bio_write(supahblock->d_bitmap_blk, dblock_bitmap);
	bio_write(supahblock->i_start_blk, root_dir);
	bio_write(supahblock->d_start_blk, root_dirents);

	return 0;
}


/* 
 * FUSE file operations
 */
static void *rufs_init(struct fuse_conn_info *conn) {

	char* path = "./DISKFILE.txt";
	strncpy(diskfile_path, path, PATH_MAX-1);
	diskfile_path[strlen(path)] = '\0';	
	
	// Step 1a: If disk file is not found, call mkfs
	if (dev_open(diskfile_path) == -1) {
		int a = rufs_mkfs();
	}

  // Step 1b: If disk file is found, just initialize in-memory data structures
  // and read superblock from disk
	superblock = (sb*)malloc(BLOCK_SIZE);
	bio_read(0, superblock);

	bitmap_t inode_bitmap = (unsigned char*)malloc(BLOCK_SIZE);
	bio_read(superblock->i_bitmap_blk, inode_bitmap);

	bitmap_t data_bitmap = (unsigned char*)malloc(BLOCK_SIZE);
	bio_read(superblock->d_bitmap_blk, data_bitmap);

	printf("inode bitmap block number: %d\n", superblock->i_bitmap_blk);
	printf("data bitmap block number: %d\n", superblock->d_bitmap_blk);
	printf("starting block of inode table: %d\n", superblock->i_start_blk);
	printf("starting block of data blocks: %d\n", superblock->d_start_blk);
	printf("size of inode table: %d\n", (superblock->d_start_blk - superblock->i_start_blk));
	
 	printf("Result of get_avail_ino (AKA the inode # that was SET): %d\n", get_avail_ino());

	return NULL;
}

static void rufs_destroy(void *userdata) {

	// Step 1: De-allocate in-memory data structures
	free(superblock);
	printf("frick you bro\n");

	// Step 2: Close diskfile
	dev_close();

}

static int rufs_getattr(const char *path, struct stat *stbuf) {

	// Step 1: call get_node_by_path() to get inode from path

	// Step 2: fill attribute of file into stbuf from inode

		stbuf->st_mode   = S_IFDIR | 0755;
		stbuf->st_nlink  = 2;
		time(&stbuf->st_mtime);

	return 0;
}

static int rufs_opendir(const char *path, struct fuse_file_info *fi) {

	// Step 1: Call get_node_by_path() to get inode from path

	// Step 2: If not find, return -1

    return 0;
}

static int rufs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {

	// Step 1: Call get_node_by_path() to get inode from path

	// Step 2: Read directory entries from its data blocks, and copy them to filler

	return 0;
}


static int rufs_mkdir(const char *path, mode_t mode) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name

	// Step 2: Call get_node_by_path() to get inode of parent directory

	// Step 3: Call get_avail_ino() to get an available inode number

	// Step 4: Call dir_add() to add directory entry of target directory to parent directory

	// Step 5: Update inode for target directory

	// Step 6: Call writei() to write inode to disk
	

	return 0;
}

static int rufs_rmdir(const char *path) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name

	// Step 2: Call get_node_by_path() to get inode of target directory

	// Step 3: Clear data block bitmap of target directory

	// Step 4: Clear inode bitmap and its data block

	// Step 5: Call get_node_by_path() to get inode of parent directory

	// Step 6: Call dir_remove() to remove directory entry of target directory in its parent directory

	return 0;
}

static int rufs_releasedir(const char *path, struct fuse_file_info *fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int rufs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
	printf("shush\n");
	// Step 1: Use dirname() and basename() to separate parent directory path and target file name

	// Step 2: Call get_node_by_path() to get inode of parent directory

	// Step 3: Call get_avail_ino() to get an available inode number

	// Step 4: Call dir_add() to add directory entry of target file to parent directory

	// Step 5: Update inode for target file

	// Step 6: Call writei() to write inode to disk

	return 0;
}

static int rufs_open(const char *path, struct fuse_file_info *fi) {

	// Step 1: Call get_node_by_path() to get inode from path

	// Step 2: If not find, return -1

	return 0;
}

static int rufs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {

	// Step 1: You could call get_node_by_path() to get inode from path

	// Step 2: Based on size and offset, read its data blocks from disk

	// Step 3: copy the correct amount of data from offset to buffer

	// Note: this function should return the amount of bytes you copied to buffer
	return 0;
}

static int rufs_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
	// Step 1: You could call get_node_by_path() to get inode from path

	// Step 2: Based on size and offset, read its data blocks from disk

	// Step 3: Write the correct amount of data from offset to disk

	// Step 4: Update the inode info and write it to disk

	// Note: this function should return the amount of bytes you write to disk
	return size;
}

static int rufs_unlink(const char *path) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target file name

	// Step 2: Call get_node_by_path() to get inode of target file

	// Step 3: Clear data block bitmap of target file

	// Step 4: Clear inode bitmap and its data block

	// Step 5: Call get_node_by_path() to get inode of parent directory

	// Step 6: Call dir_remove() to remove directory entry of target file in its parent directory

	return 0;
}

static int rufs_truncate(const char *path, off_t size) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int rufs_release(const char *path, struct fuse_file_info *fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
	return 0;
}

static int rufs_flush(const char * path, struct fuse_file_info * fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int rufs_utimens(const char *path, const struct timespec tv[2]) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}


static struct fuse_operations rufs_ope = {
	.init		= rufs_init,
	.destroy	= rufs_destroy,

	.getattr	= rufs_getattr,
	.readdir	= rufs_readdir,
	.opendir	= rufs_opendir,
	.releasedir	= rufs_releasedir,
	.mkdir		= rufs_mkdir,
	.rmdir		= rufs_rmdir,

	.create		= rufs_create,
	.open		= rufs_open,
	.read 		= rufs_read,
	.write		= rufs_write,
	.unlink		= rufs_unlink,

	.truncate   = rufs_truncate,
	.flush      = rufs_flush,
	.utimens    = rufs_utimens,
	.release	= rufs_release
};


int main(int argc, char *argv[]) {
	int fuse_stat;

	getcwd(diskfile_path, PATH_MAX);
	strcat(diskfile_path, "/DISKFILE");

	fuse_stat = fuse_main(argc, argv, &rufs_ope, NULL);

	return fuse_stat;
}

