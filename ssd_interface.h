/* 
 * Contributors: Youngjae Kim (youkim@cse.psu.edu)
 *               Aayush Gupta (axg354@cse.psu.edu)
 *
 * In case if you have any doubts or questions, kindly write to: youkim@cse.psu.edu 
 *
 * Description: This is a header file for ssd_interface.c.
 *
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "fast.h"
#include "pagemap.h"
#include "flash.h"
#include "type.h"

//设置映射缓存的大小，使用存储的映射条目数表示大小
//512MB SSD 时，
#define MAP_ENTRIES 1024     //1-4x, 8KB
//#define MAP_ENTRIES 2048     //1-2x, 16KB
//#define MAP_ENTRIES 4096     //1x, 32KB
//#define MAP_ENTRIES 8192     //2x, 64KB
//#define MAP_ENTRIES 16384      //4x, 128KB
//#define MAP_ENTRIES 32768    //8x, 256KB
//#define MAP_ENTRIES 65536    //16x, 512KB
//#define MAP_ENTRIES 131072    //32x, 1MB
#define MAP_PAGE_NUM  128      // 512MB SSD 包含的映射页的数量

//16GB SSD 
//1x
//#define MAP_ENTRIES 131072 
//1-2x
//#define MAP_ENTRIES 65536 
//#define MAP_PAGE_NUM  4096   // total map page number for 16gb

//32GB SSD

//1-4x
//#define MAP_ENTRIES 65536 
//1-2x
//#define MAP_ENTRIES 131072 
//1x
//#define MAP_ENTRIES 262144
//2x
//#define MAP_ENTRIES 524288
//4x
//#define MAP_ENTRIES 1048576
//#define MAP_PAGE_NUM  8192   // total map page number for 32gb

//128GB SSD
//1-4x 
//#define MAP_ENTRIES 262144
//1x
//#define MAP_ENTRIES 1048576 
//4x
//#define MAP_ENTRIES 4194304
//#define MAP_PAGE_NUM  32768   // total map page number for 128gb

//设置 flash 操作的延时，单位是毫秒
//#define READ_DELAY        (0.1309/4)
#define READ_DELAY        0.025
//#define WRITE_DELAY       (0.4059/4)
#define WRITE_DELAY       0.2
#define ERASE_DELAY       1.5 
#define GC_READ_DELAY  READ_DELAY    // gc read_delay = read delay    
#define GC_WRITE_DELAY WRITE_DELAY  // gc write_delay = write delay 

#define OOB_READ_DELAY    0.0
#define OOB_WRITE_DELAY   0.0

struct ftl_operation * ftl_op;

//细化 flash 各种操作的统计

//trace I/O 导致的数据页读写
#define DATA_PAGE_READ     0   
#define DATA_PAGE_WRITE    1

//映射页的读写
#define TRANS_PAGE_READ 2
#define TRANS_PAGE_WRITE 3

//数据块和映射页的擦除
#define DATA_BLOCK_ERASE   4
#define TRANS_BLOCK_ERASE   5

//垃圾回收数据块，导致的数据页读写
#define GC_DATA_PAGE_READ  6
#define GC_DATA_PAGE_WRITE 7

//垃圾回收导致的映射页读写：包括 ①回收映射块导致的映射页迁移、②回收数据块 -> 有效数据页迁移 -> 映射条目更新 -> 映射更新
#define GC_TRANS_PAGE_READ 8
#define GC_TRANS_PAGE_WRITE 9

//trace I/O 导致的部分数据页写（部分页写会导致先读后写）
#define UPDATE_DATA_WRITE 10

//垃圾回收导致的映射页读写中的第二种
#define GC_TRANS_READ_FROM_MIGRATE_DATA 11
#define GC_TRANS_WRITE_FROM_MIGRATE_DATA 12

// OOB 读写
#define OOB_READ      13
#define OOB_WRITE     14

void reset_flash_stat();
double calculate_delay_flash();
void initFlash();
void endFlash();
void printWearout();
void send_flash_request(int start_blk_no, int block_cnt, int operation, int map_flag);
void find_cache_max();
void find_cache_min();
void synchronize_disk_flash();
double callFsim(unsigned int secno, int scount, int operation);
void cache_clean_update();

int write_count;
int read_count;

//计算延时
int flash_read_num;
int flash_write_num;
int flash_gc_read_num;
int flash_gc_write_num;
int flash_erase_num;
int flash_oob_read_num;
int flash_oob_write_num;
int update_write_num;

int ftl_type;

extern int total_util_sect_num; 
extern int total_extra_sect_num;

int global_total_blk_num;

int warm_done; 

int total_er_cnt;
int flag_er_cnt;
int block_er_flag[20000];
int block_dead_flag[20000];
int wear_level_flag[20000];
int unique_blk_num; 
int unique_log_blk_num;
int last_unique_log_blk;

int total_extr_blk_num;
int total_init_blk_num;

//记录每个映射页的更新次数
int trans_page_update[MAP_PAGE_NUM];
