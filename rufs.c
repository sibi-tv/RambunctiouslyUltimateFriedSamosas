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

	return 0;
}

/* 
 * Get available data block number from bitmap
 */
int get_avail_blkno() {
	// Step 1: Read data block bitmap from disk
	bitmap_t data_bitmap = (unsigned char*) malloc(BLOCK_SIZE);
	bio_read(superblock->d_bitmap_blk, data_bitmap);

	// Step 2: Traverse data block bitmap to find an available slot
	for(int i = 0; i < superblock->max_dnum; i++){
		if(!get_bitmap(data_bitmap, i)){

			// Step 3: Update data block bitmap and write to disk 
			set_bitmap(data_bitmap, i);
			bio_write(superblock->d_bitmap_blk, data_bitmap);
			return i;
		}
	}

	return 0;
}

/* 
 * inode operations
 */
int readi(uint16_t ino, struct inode *inode) { // assumes that ino is checked beforehand and that this method always runs successfully
  // Step 1: Get the inode's on-disk block number
  	int block_num = superblock->i_start_blk + (ino)/(BLOCK_SIZE/sizeof(inode));

  // Step 2: Get offset of the inode in the inode on-disk block
	int offset = ino % (BLOCK_SIZE/sizeof(inode));
  // Step 3: Read the block from disk and then copy into inode structure
  	unsigned char* desired_block = (unsigned char*)malloc(BLOCK_SIZE);
	bio_read(block_num, desired_block);

	index_node * ptr = ((index_node*)desired_block) + offset;
	memcpy(inode, ptr, sizeof(index_node));

	return 1;
}

int writei(uint16_t ino, struct inode *inode) {
	// Step 1: Get the block number where this inode resides on disk
	int block_num = superblock->i_start_blk + (ino)/(BLOCK_SIZE/sizeof(inode));
	
	// Step 2: Get the offset in the block where this inode resides on disk
	int offset = ino % (BLOCK_SIZE/sizeof(inode));

	// Step 3: Write inode to disk 
	unsigned char* desired_block = (unsigned char*)malloc(BLOCK_SIZE);
	bio_read(block_num, desired_block);

	index_node * ptr = ((index_node*)desired_block) + offset;
	memcpy(ptr, inode, sizeof(index_node));

	bio_write(block_num, desired_block);

	return 0;
}


/* 
 * directory operations
 */
int dir_find(uint16_t ino, const char *fname, size_t name_len, struct dirent *dirent) {
	// Step 1: Call readi() to get the inode using ino (inode number of current directory)
	index_node* curr_dir = (index_node*)malloc(sizeof(index_node));
	readi(ino, curr_dir);

	// Step 2: Get data block of current directory from inode
	for (int i = 0; i < 16; i++) {
		/** 
		 * blocks before were all full of dirents that DID NOT contain fname,
		 * and this block (and therefore subsequent blocks) haven't even been allocated. 
		 * So the fname is not in this directory.
		*/ 
		if (curr_dir->direct_ptr[i] == -1) { return 0; }
		
		int data_block_num = curr_dir->direct_ptr[i];
		unsigned char* data_block = (unsigned char *) malloc(BLOCK_SIZE);
		bio_read(data_block_num, data_block);

		// Step 3: Read directory's data block and check each directory entry.
		//If the name matches, then copy directory entry to dirent structure
		direntry* ptr = (direntry*) data_block;
		for (; ptr != NULL && ptr->valid != INVALID; ptr = ptr + 1) {
			if (strcmp(fname, ptr->name) == 0) { 
				memcpy(dirent, ptr, sizeof(direntry));
				
				free(data_block);
				free(curr_dir);

				return 1; // success			
			}
		}

		if (ptr != NULL && ptr->valid == INVALID) { 
			return 0; // failure
		}
	}

	return 0; // user allocated 256 dirents in this inode and none of them equal fname :D
}

int dir_add(struct inode* dir_inode, uint16_t f_ino, const char *fname, size_t name_len) { // assumes caller method knows if f_ino is for file or directory
	int i = 0;
	for (; i < 16 && dir_inode->direct_ptr[i] != -1; i++) {
		// Step 1: Read dir_inode's data block and check each directory entry of dir_inode
		int data_block_num = dir_inode->direct_ptr[i];
		unsigned char* data_block = (unsigned char *) malloc(BLOCK_SIZE);
		bio_read(data_block_num, data_block);
		
		// Step 2: Check if fname (directory name) is already used in other entries
		direntry* ptr = (direntry*) data_block;
		int count = 0;
		for (int i = 0; i < MAX_DIRENTS && ptr->valid != INVALID; i++) {
			if (strcmp(fname, ptr->name) == 0) {
				free(data_block);
				return 0;
			}
			count++;
			ptr = ptr + 1;
		}

		// Step 3: Add directory entry in dir_inode's data block and write to disk
		if (ptr != NULL) {
			ptr->ino = f_ino;
			ptr->valid = VALID;
			strncpy(ptr->name, fname, name_len);
			ptr->name[name_len] = '\0';
			ptr->len = name_len;
			// Write directory entry
			bio_write(data_block_num, data_block);

			return 1;
		}
	}

	// Step 3: Add directory entry in dir_inode's data block and write to disk
	if (i < 16) { // only runs if bro is assigning a 17th or higher file in this directory
		// Allocate a new data block for this directory if it does not exist
		int new_block_num = get_avail_blkno();
		dir_inode->direct_ptr[i] = new_block_num;

		direntry* new_block = (direntry*)malloc(BLOCK_SIZE);

		new_block->ino = f_ino;
		new_block->valid = VALID;
		strncpy(new_block->name, fname, name_len);
		new_block->name[name_len] = '\0';
		new_block->len = name_len;

		// Update directory inode
		dir_inode->direct_ptr[i] = new_block_num;
		dir_inode->size += 1;

		// Write directory entry
		bio_write(new_block_num, new_block);
	}

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

	// this code below finds the dirent of the current directory and checks if it exists. If it doesn't, return 0.
	int index = 0; // number of characters/bytes in dir/file name == index of closest '/'
	const char* ptr; 
	if (path[0] != '/') { // the passed path DOES NOT include root directory
		ptr = path; 
	} else { // the passed path DOES include root directory
		ptr = path + 1;
	}

	while ((ptr + index)[0] != '\0' && ptr[index] != '/') { // if ptr + index == NULL then you're at the terminal point
		index++;
	}

	if (index == 0) { // path is just root
		readi(ino, inode);
		return 0; // success
	}

	char* dir_entry_name = (char*) malloc(index+1);
	memcpy(dir_entry_name, ptr, index);
	dir_entry_name[index] = '\0';

	direntry* dir_entry = (direntry*) malloc(sizeof(direntry));
	if (dir_find(ino, dir_entry_name, 0, dir_entry) == 0) { // dirent was not found
		free(dir_entry_name);
		free(dir_entry);
		return -1; // failure
	} else {
		int node_ino = dir_entry->ino;
		if ((ptr + index)[0] == '\0') { // Reached terminal point			
			readi(node_ino, inode);
			free(dir_entry);
			free(dir_entry_name);

			return 0; // success
		} else {
			return get_node_by_path(ptr+index, node_ino, inode);
		}
	}
}

/* 
 * Make file system
 */
int rufs_mkfs() {

	// Call dev_init() to initialize (Create) Diskfile
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
	root_dir->valid = VALID;
	root_dir->size = 1; // because it will have 1 block at the start
	for (int i = 0; i < 16; i++) { 
		root_dir->direct_ptr[i] = -1; 
	} // setting all direct pointers to invalid

	direntry* root_dirents = (direntry*) malloc(BLOCK_SIZE);
	root_dirents->ino = 0; 

	root_dir->direct_ptr[0] = supahblock->d_start_blk;

	set_bitmap(dblock_bitmap, 0);
	set_bitmap(dblock_bitmap, supahblock->i_bitmap_blk);
	set_bitmap(dblock_bitmap, supahblock->d_bitmap_blk);
	for (int i = supahblock->i_start_blk; i < supahblock->d_start_blk; i++) { set_bitmap(dblock_bitmap, i); }
	set_bitmap(dblock_bitmap, supahblock->d_start_blk); // setting 67th block
	
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

	// initializing diskfile_path
	char* path = "./DISKFILE.txt";
	strncpy(diskfile_path, path, PATH_MAX-1);
	diskfile_path[strlen(path)] = '\0';	
	
	// Step 1a: If disk file is not found, call mkfs
	if (dev_open(diskfile_path) == -1) {
		rufs_mkfs();
	}

  // Step 1b: If disk file is found, just initialize in-memory data structures
  // and read superblock from disk
	superblock = (sb*)malloc(BLOCK_SIZE);
	bio_read(0, superblock);

	bitmap_t inode_bitmap = (unsigned char*)malloc(BLOCK_SIZE);
	bio_read(superblock->i_bitmap_blk, inode_bitmap);

	bitmap_t data_bitmap = (unsigned char*)malloc(BLOCK_SIZE);
	bio_read(superblock->d_bitmap_blk, data_bitmap);
	
	return NULL;
}

static void rufs_destroy(void *userdata) {

	// Step 1: De-allocate in-memory data structures
	free(superblock);

	// Step 2: Close diskfile
	dev_close();

}

static int rufs_getattr(const char *path, struct stat *stbuf) { // Sibi // initializes an inode's vstat

	// Step 1: call get_node_by_path() to get inode from path
	index_node * inode = (index_node*)malloc(sizeof(index_node));

	int bruh = get_node_by_path(path, 0, inode);
	// direntry* dd = (direntry*)malloc(BLOCK_SIZE);
	// bio_read(inode->direct_ptr[0], dd);

	if (bruh == -1) {
		free(inode);
		return -ENOENT;
	}

	// Step 2: fill attribute of file into stbuf from inode
	if (strcmp(path, "/") == 0) {
		stbuf->st_mode   = __S_IFDIR | 0755;
		stbuf->st_nlink  = 2;
		inode->link = 1;
	} else {
		stbuf->st_mode = __S_IFREG | 0644;
		stbuf->st_nlink  = 1;
		index_node * directory_node = (index_node*)malloc(sizeof(index_node));
		get_node_by_path(dirname((char*) path), 0, directory_node);
		directory_node->link += 1;
		directory_node->vstat.st_nlink += 1;
		time(&(directory_node->vstat.st_mtime));
		time(&(directory_node->vstat.st_atime));
	}
	
	stbuf->st_gid = getgid();
	stbuf->st_uid = getuid();
	stbuf->st_ino = inode->ino;
	stbuf->st_size = (inode->size)*BLOCK_SIZE;
	time(&stbuf->st_mtime);
	time(&stbuf->st_atime);
	
	free(inode);
	return 0;
}

static int rufs_opendir(const char *path, struct fuse_file_info *fi) { // Rahul

	// Step 1: Call get_node_by_path() to get inode from path
	index_node * in = (index_node*)malloc(sizeof(index_node));

	// Step 2: If not find, return -1
    return get_node_by_path(path, 0, in);
}

static int rufs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) { // Rahul

	// Step 1: Call get_node_by_path() to get inode from path
	index_node * in = (index_node*)malloc(sizeof(index_node));
    get_node_by_path(path, 0, in);

	printf(".........Gaffot\n");

	// Step 2: Read directory entries from its data blocks, and copy them to filler
	for(int i = 0; i < in->size; i++){

		printf(".........Gaffot\n");

		direntry * copythebastard = malloc(BLOCK_SIZE);
		bio_read(in->direct_ptr[i], copythebastard);
		
		printf(".........Moron\n");

		for(int j = 0; j < MAX_DIRENTS && copythebastard->valid != INVALID; j++){
			index_node * bruh = malloc(sizeof(index_node));
			readi(copythebastard->ino, bruh);
			filler(buffer, copythebastard->name, &(bruh->vstat), offset);
			printf("LS YEAAAA BABYYYY\n");
			copythebastard += 1;
		}
		// memcpy(buffer + (i * BLOCK_SIZE), copythebastard, BLOCK_SIZE);
	}

	

	return 0;
}


static int rufs_mkdir(const char *path, mode_t mode) { // Sibi
	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name
	char* parent_directory = dirname((char*)path);
	char* base = basename((char*)path);

	// Step 2: Call get_node_by_path() to get inode of parent directory
	index_node * dir_inode = (index_node*) malloc(sizeof(index_node));
	get_node_by_path(parent_directory, 0, dir_inode);

	// Step 3: Call get_avail_ino() to get an available inode number
	int ino = get_avail_ino();

	// Step 4: Call dir_add() to add directory entry of target directory to parent directory
	if (dir_add(dir_inode, ino, base, strlen(base)) == 0) {
		return -1;
	}

	// Step 5: Update inode for target directory
	index_node* target_node = (index_node*)malloc(sizeof(index_node));
	target_node->direct_ptr[0] = get_avail_blkno();
	for (int i = 1; i < 16; i++) { target_node->direct_ptr[i] = -1; }
	target_node->ino = ino;
	target_node->size = 1;
	target_node->valid = VALID;
	direntry* root_dirents = (direntry*) malloc(BLOCK_SIZE);
	for (int i = 0; i < MAX_DIRENTS; i++) { root_dirents->valid = INVALID; }
	bio_write(target_node->direct_ptr[0], root_dirents);

	// Step 6: Call writei() to write inode to disk
	writei(ino, target_node);

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

static int rufs_create(const char *path, mode_t mode, struct fuse_file_info *fi) { // Sibi // needs to call getattr?
	// Step 1: Use dirname() and basename() to separate parent directory path and target file name
	char* p1 = (char*) malloc(strlen(path));
	char* p2 = (char*) malloc(strlen(path));
	memcpy(p1, path, strlen(path)+1);
	p1[strlen(path)] = '\0';
	memcpy(p2, path, strlen(path)+1);
	p2[strlen(path)] = '\0';
	char* parent_directory = dirname(p1);
	char* base = basename(p2);

	// Step 2: Call get_node_by_path() to get inode of parent directory
	index_node * dir_inode = (index_node*) malloc(sizeof(index_node));
	int a = get_node_by_path(parent_directory, 0, dir_inode);
	if (a == -1) {
		free(dir_inode);
		return -ENOENT;
	}

	

	// Step 3: Call get_avail_ino() to get an available inode number
	int ino = get_avail_ino();

	// Step 4: Call dir_add() to add directory entry of target file to parent directory
	
	int b = dir_add(dir_inode, ino, base, strlen(base));
	if (b == 0) {
		free(dir_inode);
		return -EIO;
	}

	// Step 5: Update inode for target file
	index_node* target_node = (index_node*)malloc(sizeof(index_node));
	target_node->direct_ptr[0] = get_avail_blkno();
	for (int i = 1; i < 16; i++) { target_node->direct_ptr[i] = -1; }
	target_node->ino = ino;
	target_node->size = 1;
	target_node->link = 1;
	target_node->valid = VALID;
	target_node->vstat.st_gid = getgid();
	target_node->vstat.st_uid = getuid();
	target_node->vstat.st_size = BLOCK_SIZE*target_node->size;
	target_node->vstat.st_mode = mode;
	target_node->vstat.st_nlink = 1;
	time(&(target_node->vstat.st_atime));
    time(&(target_node->vstat.st_mtime));
	
	// Step 6: Call writei() to write inode to disk
	writei(ino, target_node);

	return 0;
}

static int rufs_open(const char *path, struct fuse_file_info *fi) { // Sibi

	// Step 1: Call get_node_by_path() to get inode from path
	index_node * in = (index_node*)malloc(sizeof(index_node));

	// Step 2: If not find, return -1
    return get_node_by_path(path, 0, in);
}

static int rufs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) { // Rahul

	// Step 1: You could call get_node_by_path() to get inode from path
	index_node * gaf = malloc(sizeof(index_node));
	get_node_by_path(path, 0, gaf);

	// Step 2: Based on size and offset, read its data blocks from disk

	int index = offset / BLOCK_SIZE;
	unsigned char * buff = malloc(BLOCK_SIZE);
	bio_read(gaf->direct_ptr[index], buff);

	int s = size;

	memcpy(buffer, buff + (offset % BLOCK_SIZE), BLOCK_SIZE - (offset % BLOCK_SIZE));

	index++;
	while(size > 0){
		if(size > BLOCK_SIZE){
			
		}
	}

	memcpy(buffer, )

	// Step 3: copy the correct amount of data from offset to buffer

	// Note: this function should return the amount of bytes you copied to buffer
	return 0;
}

static int rufs_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) { // Rahul
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

