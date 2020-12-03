#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "disk.h"
#include "fs.h"

#define FAT_EOC 0xFFFF
#define BLOCK_SIZE 4096


struct superblock_t{
    uint8_t signature[8]; //could use char here 8 * 8 = 64 same as uint64
    uint16_t block_num;
    uint16_t rootDir_t_block_index;  // Root directory block index
    uint16_t data_block_index; //Index of Data Blocks
    uint16_t data_block_num;
    uint8_t fat_block_num;
    uint8_t padding[BLOCK_SIZE - 17]; //4096 - (8 + 2 X 4 + 1 )
};

struct rootDir_t{
    uint8_t filename[FS_FILENAME_LEN]; //16 * 8 = 128 16byts
    uint32_t file_size; //size of File 8 byts
    uint16_t file_index; //index of the first data block 2 byts
    uint8_t padding[10];
};


struct file_descriptor_t{ //File descriptor class
    int offset; //where to start/stop in FAT
    struct rootDir_t *root_directory; //every file has its own directory struct
};
typedef struct file_descriptor_t file_descriptor_t;

/*
 * globals vars
 * */
char * sig = "ECS150FS";
struct superblock_t *superblock = NULL;  //later easier for check if the sys is mount or not
struct rootDir_t *current_root_dir;
uint16_t *FATt;
file_descriptor_t file_descriptor_table[FS_OPEN_MAX_COUNT]; //file descriptor table array storing all files
uint8_t num_file_open = 0;


/*
 * returns the free fat's index(offset)
 * -1 if no free offset
 */
int find_free_fat(int initial_offset)
{
    int offset = -1;

    //check if opened previously
    for(int i = initial_offset; i < superblock->data_block_num; i++){
        if(FATt[i] == 0)
        {
            offset = i;
            break;
        }
    }

    return offset; //-1 if no available fat space
}

/*
 * find the next free block
 * return block_index otherwise return -1 if no free block
 */
int find_free_block()
{
    for(int i = 0; i < superblock->data_block_num; i++)
    {
        if(FATt[i] == 0)
        {
            return i;
        }
    }
    return -1;
}

/*
 * count free block num
 */
int count_free_block()
{
    int fat_free = 0;
    for(int i = 0; i < superblock->data_block_num; i++)
    {
        if(FATt[i] == 0)
        {
            fat_free++;
        }
    }
    return fat_free;
}
/*
 * find a directory in current_root_dir
 * return the directory index, -1 if not found
 */
int find_dir_file(const char *filename)
{
    int index = -1;
    for(int i = 0; i < FS_FILE_MAX_COUNT; i++) {
        if(strcmp((char*)current_root_dir[i].filename, filename) == 0)
        {
            index = i;
            break;
        }
    }
    return index;
}

/*
 * find the free directory in current_root_directory
 * initialize it
 * returns -1 if no free directory space or file directory already exists
 */
int find_free_directory(const char *filename)
{
    int index = -1;
    int exist_index;
    int fat_index;
    //check if file exist
    exist_index = find_dir_file(filename);
    if(exist_index != -1) //exist before, return -1
    {
        return index;
    }

    index = find_dir_file(""); //find first empty directory place
    if (index == -1)
    {
        return index;
    }
    
    fat_index = find_free_fat(0);
    if(fat_index == FAT_EOC)
    {
        return -1;
    }
    strcpy((char *)current_root_dir[index].filename, filename); //specific
    current_root_dir[index].file_size = 0;
    current_root_dir[index].file_index = fat_index;
    FATt[fat_index] = FAT_EOC;
    
    return index; //return the index
}
/*
 * count free directory block num
 */
int count_free_dir(){
    int free_dir = 0;
    for(int i = 0; i < FS_FILE_MAX_COUNT; i++){
        if(current_root_dir[i].filename[0] == '\0'){
            free_dir++;
        }
    }
    return free_dir;
}
/*
 * Check if the file is already open in File Descriptor
 * returns the index if found, -1 otherwise
 */
int check_open(const char *filename){
    int index = -1;
    for (int i = 0; i < FS_OPEN_MAX_COUNT; i++){
        //find the File name in open file file_descriptor_table in Directory class
        if(strcmp(filename, (char *)file_descriptor_table[i].root_directory->filename) == 0){
            return i;
        }
    }
    return index;
}

int fat_delete(const int index){
    uint16_t current = current_root_dir[index].file_index;
    uint16_t temp; //temp of index
    while(current != FAT_EOC){
        temp = FATt[current];//FAT file_descriptor_table
        FATt[current] = 0;
        current = temp;
    }
    FATt[temp] = 0; //last index set to be empty
    return 0;
}



int fs_mount(const char *diskname)
{
    if (block_disk_open(diskname) == -1){
        return -1;
    }
    //init super
    superblock = malloc(sizeof(struct superblock_t));
    if (superblock == NULL){
        return -1;
    }
    //read super
    if (block_read(0, superblock) == -1){ //block[0] is superblock_t
        return -1;
    }
    if (memcmp(sig, superblock->signature, strlen(sig)) != 0){
        free(superblock);
        return -1;
    }
    if (superblock->block_num != block_disk_count()){
        free(superblock);
        return -1;
    }

    //root directory
    current_root_dir = (struct rootDir_t*)malloc(sizeof(struct rootDir_t) * FS_FILE_MAX_COUNT);//128 root directory maximum
    if(block_read(superblock->rootDir_t_block_index, current_root_dir) == -1){ //block[directory starting index] is rootDir_ts
        return -1;
    }

    //read FAT
    FATt = malloc(sizeof(uint16_t)* superblock->fat_block_num * BLOCK_SIZE );
    for (int i = 0; i < superblock->fat_block_num; ++i){
        if (block_read(i + 1, FATt + (i * BLOCK_SIZE)) == -1){ //block[1 to fat number] is FAT
            return -1;
        }
    }

    //set file descriptor
    for(int i = 0; i < FS_OPEN_MAX_COUNT; ++i){
        file_descriptor_table[i].root_directory = NULL;
        file_descriptor_table[i].offset = -1;
    }
    num_file_open = 0;

    return 0;
}

int fs_umount(void)
{
    if(num_file_open > 0){
        return -1;
    }

    //write all metadata out to disk
    //superblock
    if(block_write(0, superblock) == -1)
    {
        return -1;
    }

    //Root Dir
    if( block_write(superblock->rootDir_t_block_index, current_root_dir) == -1){
        return -1;
    }

    //FAT
    for (int i = 0; i < superblock->fat_block_num; ++i){
        block_write(i + 1, i * BLOCK_SIZE + FATt);
    }

    //delete
    free(superblock);
    free(FATt);
    free(current_root_dir);

    //close
    if (block_disk_close() < 0){
        return -1;
    }
    return 0;
}
//format from prof
/*
FS Info:
total_blk_count=4100
fat_blk_count=2
rdir_blk=3
data_blk=4
data_blk_count=4096
fat_free_ratio=4095/4096
rdir_free_ratio=128/128
*/
int fs_info(void)
{
    if (block_disk_count() == -1)
        return -1;
    int total_blk = 2 + superblock->fat_block_num + superblock->data_block_num; // 1 is supper 1 is root dir

    printf("FS Info:\n");
    printf("total_blk_count=%d\n",total_blk);
    printf("fat_blk_count=%d\n",superblock->fat_block_num);
    printf("rdir_blk=%d\n", superblock->rootDir_t_block_index);
    printf("data_blk=%d\n", superblock->data_block_index);
    printf("data_blk_count=%d\n", superblock->data_block_num);
    //get fat free block
    int fat_free = 0;
    fat_free = count_free_block();

    //get root dir free block
    int rdir_free = 0;
    rdir_free = count_free_dir();

    printf("fat_free_ratio=%d/%d\n",fat_free, superblock->data_block_num);
    printf("rdir_free_ratio=%d/%d\n",rdir_free, FS_FILE_MAX_COUNT);

    return 0;
}


int fs_create(const char *filename)
{
    /*THREE CHECKS*/
    //check too long
    if (strlen(filename) > FS_FILENAME_LEN || filename[strlen(filename) - 1] != '\0'){
        return -1;
    }
    //check exist
    for (int i = 0; i < FS_OPEN_MAX_COUNT; i++){
        if (strcmp((char *)current_root_dir[i].filename, filename) == 0){ //cast filename to char
            return -1;
        }
    }
    //check if root max (more than 128 file)
    int indx_count;
    for (indx_count = 0; indx_count < FS_FILE_MAX_COUNT; ++indx_count ){
        if (current_root_dir[indx_count].filename[0] == '\0'){
            break;
        }
    }
    if (indx_count >= 127){ //which means we went through 128 times
        return -1;
    }

    //check if file already exit
    //find first open entry
    if(find_free_directory(filename) == -1)
    {
        return -1;
    }

    return 0;
}


int fs_delete(const char *filename)
{
    /*THREE CHECKS*/
    //check filename invalid
    if (filename == NULL || strlen(filename) > FS_FILENAME_LEN){
        return -1;
    }
    //check exist
    int Index_del = -1;
    if(find_dir_file(filename) == -1)
    {
        return -1;
    }
    //check if currently open
    if(check_open(filename) == -1){
        return -1;
    }
    /*finish checking*/

    //delect
    strcpy((char *)current_root_dir[Index_del].filename, "");


    // free FAT
    fat_delete(Index_del);

    // Clear the size
    current_root_dir[Index_del].file_size = 0;
    current_root_dir[Index_del].file_index = FAT_EOC;

    return 0;
}


int fs_ls(void)
{
    if (block_disk_count() == -1){
        return -1;
    }
    printf("FS Ls:\n");
    for (int i = 0; i < FS_FILE_MAX_COUNT; ++i){
        if (strcmp((char*)current_root_dir[i].filename, "") == 0){
            printf("file: %s", current_root_dir[i].filename);
            printf(" , size: %d",current_root_dir[i].file_size);
            printf(" , data_blk: %d\n", current_root_dir[i].file_index);
        }
    }

    return 0;

}


int fs_open(const char *filename)
{
    int free_index;
    int index;
    int found_dir_index;

    //check if filename is invalid
    if (strlen(filename) > FS_FILENAME_LEN || filename == NULL){
        return -1;
    }

    //check if the open file already full
    if (num_file_open >= FS_OPEN_MAX_COUNT){
        return -1;
    }

    found_dir_index = find_dir_file(filename); //index of the found directory
    //check if file exist on file directory
    if(found_dir_index == -1)
    {
        return -1;
    }

    free_index = check_open(""); //free_index = -1 if no free space found
    index = check_open(filename);

    if(index != -1) //opened previously
    {
        file_descriptor_table[free_index].root_directory = file_descriptor_table[index].root_directory;
        file_descriptor_table[free_index].offset = 0; //start from 0
    }
    else
    {
        file_descriptor_table[free_index].root_directory = &current_root_dir[found_dir_index];
        file_descriptor_table[free_index].offset = current_root_dir[found_dir_index].file_index;
    }
    num_file_open++;
    return free_index;

}

int Check_Valid(const int fd){
    if (fd < 0 || fd > FS_OPEN_MAX_COUNT){
        return -1;
    }
    /*
    since I set this offset at -1 at beginning, and 0 when it open
    */
    if (strcmp((char*)file_descriptor_table[fd].root_directory->filename,"") == 0){ //since we set this offset at -1 at beginning
        return -1;
    }
    return 1;
}

int fs_close(int fd)
{
    //Check if fd <0, if fd > FS_OPEN_MAX_COUNT, if my offset already -1
    if(Check_Valid(fd) == -1){
        return -1;
    }

    file_descriptor_table[fd].root_directory = NULL;
    file_descriptor_table[fd].offset = -1;
    num_file_open--;

    return 0;
}


int fs_stat(int fd)
{
    if(Check_Valid(fd) == -1){
        return -1;
    }

    //return file size
    return file_descriptor_table[fd].root_directory->file_size;

}


int fs_lseek(int fd, size_t offset)
{
    if (Check_Valid(fd) == -1){
        return -1;
    }

    if (offset >= file_descriptor_table[fd].root_directory->file_size){
        return -1;
    }

    //set open file's offset 想要看第几页
    file_descriptor_table[fd].offset = offset;
    return 0;

}

int fs_write(int fd, void *buf, size_t count)
{
    char *buffer;
    size_t buffer_num, byte_remained, byte_offset;
    int buffer_offset;
    uint16_t block_current;

    struct file_descriptor_t* write_descriptor;
    if (Check_Valid(fd) == -1){
        return -1;
    }

    write_descriptor = &file_descriptor_table[fd];
    if(write_descriptor->root_directory == NULL){
        return -1; //no file opened at fd
    }

    buffer = (char*) buf;
    buffer_num = count;

    //reading process
    if(write_descriptor->root_directory->file_index == FAT_EOC && count > 0){
        int buffer_block = find_free_block();
        if(buffer_block == -1){
            return 0; //no blocks left
        }
        write_descriptor->root_directory->file_index = buffer_block;
        FATt[buffer_block] = FAT_EOC;
    }
    buffer_offset = 0;

    //writing process
    while(buffer_num > 0){
        int block_index = (write_descriptor->offset)/BLOCK_SIZE; //in bytes
        block_current = write_descriptor->root_directory->file_index; //first file block in FAT for current file

        for(int i = 0; i < block_current; i++){
            if(FATt[block_current] == FAT_EOC){
                int block_next = find_free_block();
                if(block_next == -1){
                    return 0; //no blocks left
                }
                FATt[block_current] = block_next;
                FATt[block_next] = FAT_EOC;
            }
        }
        block_current
    }
}

int fs_read(int fd, void *buf, size_t count)
{

}
