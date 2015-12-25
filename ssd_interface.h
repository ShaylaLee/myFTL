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

//����ӳ�仺��Ĵ�С��ʹ�ô洢��ӳ����Ŀ����ʾ��С
//512MB SSD ʱ��
#define MAP_ENTRIES 1024     //1-4x, 8KB
//#define MAP_ENTRIES 2048     //1-2x, 16KB
//#define MAP_ENTRIES 4096     //1x, 32KB
//#define MAP_ENTRIES 8192     //2x, 64KB
//#define MAP_ENTRIES 16384      //4x, 128KB
//#define MAP_ENTRIES 32768    //8x, 256KB
//#define MAP_ENTRIES 65536    //16x, 512KB
//#define MAP_ENTRIES 131072    //32x, 1MB
#define MAP_PAGE_NUM  128      // 512MB SSD ������ӳ��ҳ������

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

//���� flash ��������ʱ����λ�Ǻ���
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

//ϸ�� flash ���ֲ�����ͳ��

//trace I/O ���µ�����ҳ��д
#define DATA_PAGE_READ     0   
#define DATA_PAGE_WRITE    1

//ӳ��ҳ�Ķ�д
#define TRANS_PAGE_READ 2
#define TRANS_PAGE_WRITE 3

//���ݿ��ӳ��ҳ�Ĳ���
#define DATA_BLOCK_ERASE   4
#define TRANS_BLOCK_ERASE   5

//�����������ݿ飬���µ�����ҳ��д
#define GC_DATA_PAGE_READ  6
#define GC_DATA_PAGE_WRITE 7

//�������յ��µ�ӳ��ҳ��д������ �ٻ���ӳ��鵼�µ�ӳ��ҳǨ�ơ��ڻ������ݿ� -> ��Ч����ҳǨ�� -> ӳ����Ŀ���� -> ӳ�����
#define GC_TRANS_PAGE_READ 8
#define GC_TRANS_PAGE_WRITE 9

//trace I/O ���µĲ�������ҳд������ҳд�ᵼ���ȶ���д��
#define UPDATE_DATA_WRITE 10

//�������յ��µ�ӳ��ҳ��д�еĵڶ���
#define GC_TRANS_READ_FROM_MIGRATE_DATA 11
#define GC_TRANS_WRITE_FROM_MIGRATE_DATA 12

// OOB ��д
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

//������ʱ
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

//��¼ÿ��ӳ��ҳ�ĸ��´���
int trans_page_update[MAP_PAGE_NUM];
