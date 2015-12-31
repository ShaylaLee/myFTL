/* 
 * Contributors: Youngjae Kim (youkim@cse.psu.edu)
 *               Aayush Gupta (axg354@cse.psu.edu)
 *
 * In case if you have any doubts or questions, kindly write to: youkim@cse.psu.edu 
 *
 * Description: This is a header file for flash.c.
 *
 * Acknowledgement: We thank Jeong Uk Kang by sharing the initial version 
 * of sector-level FTL source code. 
 *
 */

#ifndef SSD_LAYOUT 
#define SSD_LAYOUT 

#include  "type.h"

#define MINIPAGE_NUM_PER_PAGE 4
#define SECT_NUM_PER_MINIPAGE 8


#define LB_SIZE_512  1
#define LB_SIZE_1024 2
#define LB_SIZE_2048 4

//定义 flash 的结构，每个页包含的扇区数、每个块包含的页数
#define SECT_NUM_PER_PAGE 32        //4->8->32
#define PAGE_NUM_PER_BLK  128       //64->128     

#define MINIPAGE_NUM_PER_BLK (PAGE_NUM_PER_BLK * MINIPAGE_NUM_PER_PAGE)

#define SECT_NUM_PER_BLK  (SECT_NUM_PER_PAGE * PAGE_NUM_PER_BLK)
#define SECT_SIZE_B 512

#define SECT_BITS       5         //2->3->5
#define PAGE_BITS       7         //6->7
#define PAGE_SECT_BITS  12         //8->9->10->12
#define BLK_BITS        20        //24->23->22->20

#define NAND_STATE_FREE    -1
#define NAND_STATE_INVALID -2

//以下是一些进行地址转换的宏定义，根据以上 flash 结构进行定义
#define SECT_MASK_IN_SECT 0x001F        //0x0003 -> 0x0007 -> 0x001F    低5位表示页内扇区偏移
#define PAGE_MASK_IN_SECT 0x0FE0        //0x00FC -> 0x01F8->0x03F8->0x0FE0    中间7位表示块内页偏移
#define PAGE_SECT_MASK_IN_SECT 0x0FFF   //0x00FF -> 0x01FF->0x03FF->0x0FFF    低12位表示块内扇区偏移
#define BLK_MASK_IN_SECT  0xFFFFF000    //0XFFFFFF00 -> 0xFFFFFE00-> 0xFFFFFC00->0xFFFFF000   高20位表示闪存内块偏移
#define PAGE_BITS_IN_PAGE 0x007F        //0x003F->0x007F 低7位表示块内page偏移    
#define BLK_MASK_IN_PAGE  0x3FFFFF80    //0x3FFFFFC0->0x3FFFFF80 高23位表示闪存内块偏移

#define PAGE_SIZE_B (SECT_SIZE_B * SECT_NUM_PER_PAGE)
#define PAGE_SIZE_KB (PAGE_SIZE_B / 1024)
#define BLK_SIZE_B  (PAGE_SIZE_B * PAGE_NUM_PER_BLK)
#define BLK_SIZE_KB (BLK_SIZE_B / 1024)

#define BLK_NO_SECT(sect)  (((sect) & BLK_MASK_IN_SECT) >> (PAGE_BITS + SECT_BITS))
#define PAGE_NO_SECT(sect) (((sect) & PAGE_MASK_IN_SECT) >> SECT_BITS)
#define SECT_NO_SECT(sect) ((sect) & SECT_MASK_IN_SECT)
#define BLK_PAGE_NO_SECT(sect) ((sect) >> SECT_BITS)
#define PAGE_SECT_NO_SECT(sect) ((sect) & PAGE_SECT_MASK_IN_SECT)
#define BLK_NO_PAGE(page)  (((page) & BLK_MASK_IN_PAGE) >> PAGE_BITS)
#define PAGE_NO_PAGE(page) ((page) & PAGE_MASK_IN_PAGE)
#define SECTOR(blk, page) (((blk) << PAGE_SECT_BITS) | (page))

//0x3FFFFF00 -> 0x3FFFFE00->0x3FFFFC00->0x3FFFF000
#define BLK_MASK_SECT 0x3FFFF000     //高 20 位 （其中后18位有效） 为块地址，见sect_state结构
#define PGE_MASK_SECT 0x00000FE0     //0x000000FC -> 0x000001F8 -> 0x000003F8->0x00000FE0 中间7位是块内页偏移
#define OFF_MASK_SECT 0x0000001F     //0x00000003 -> 0x00000007->0x0000001F  低5位是页内扇区便宜
#define IND_MASK_SECT (PGE_MASK_SECT | OFF_MASK_SECT)
#define BLK_BITS_SECT 18     //22->21->20->18 块编号最多20位，因为扇区地址为30位，块内扇区偏移需要5+7共12位
#define PGE_BITS_SECT  7
#define OFF_BITS_SECT  5
#define IND_BITS_SECT (PGE_BITS_SECT + OFF_BITS_SECT)
#define BLK_F_SECT(sect) (((sect) & BLK_MASK_SECT) >> IND_BITS_SECT)
#define PGE_F_SECT(sect) (((sect) & PGE_MASK_SECT) >> OFF_BITS_SECT)
#define OFF_F_SECT(sect) (((sect) & OFF_MASK_SECT))
#define PNI_F_SECT(sect) (((sect) & (~OFF_MASK_SECT)) >> OFF_BITS_SECT)
#define IND_F_SECT(sect) (((sect) & IND_MASK_SECT))
#define IS_SAME_BLK(s1, s2) (((s1) & BLK_MASK_SECT) == ((s2) & BLK_MASK_SECT))
#define IS_SAME_PAGE(s1, s2) (((s1) & (~OFF_MASK_SECT)) == ((s2) & (~OFF_MASK_SECT)))

//闪存块的状态
struct blk_state {
   int free;       //是否是空闲块
   int ec;         //已擦除的次数
   //int update_ec;  //程序中未使用到
};

//物理扇区的状态
struct sect_state {
  _u32 free  :  1;     //是否空闲
  _u32 valid :  1;     //是否有效
  _u32 lsn   : 30;     //存放该物理扇区对应的逻辑扇区号，sector 的最大编址，若 sector 是512B，30位寻址最多支持512GB的盘
};

struct nand_blk_info {
  struct blk_state state;                    // Erase Conunter
  struct sect_state sect[SECT_NUM_PER_BLK];  // Logical Sector Number

//为了修改页大小和块大小，扩大这些字段的值表示范围
  _s16 fpc; // free sector counter 块内包含空闲扇区的数量
  _s16 ipc; // invalid sector counter 块内包含无效扇区的数量
  _s16 lwn; // last written sector number 已使用的扇区数量，下一个未使用的扇区即是将要写入的位置

   int minipage_status[MINIPAGE_NUM_PER_BLK];
};

extern _u32 nand_blk_num;
extern _u8  pb_size;
extern struct nand_blk_info *nand_blk;
extern FILE *fp_erase;
int nand_init (_u32 blk_num, _u8 min_free_blk_num);
void nand_end ();
_u8 nand_page_read(_u32 psn, _u32 *lsns, _u8 isGC, int map_flag);
_u8 nand_page_write (_u32 psn, _u32 *lsns, _u8 isGC, int map_flag);
void nand_erase (_u32 blk_no, int map_flag);
void nand_invalidate (_u32 psn, _u32 lsn);
_u32 nand_get_free_blk(int);
void nand_stat(int);
void nand_stat_reset();
void nand_stat_print(FILE *outFP);
int nand_oob_read(_u32 psn);

_u32 free_blk_num;
_u32 free_blk_idx;

_u32 stat_data_read_num, stat_data_write_num;
_u32 stat_trans_read_num, stat_trans_write_num;
_u32 stat_data_erase_num, stat_trans_erase_num;
_u32 stat_gc_data_read_num, stat_gc_data_write_num;
_u32 stat_gc_trans_read_num, stat_gc_trans_write_num;
_u32 stat_gc_trans_read_num_migrate_data, stat_gc_trans_write_num_migrate_data;
_u32 stat_update_write_num;
_u32 stat_oob_read_num, stat_oob_write_num;

//_u32  cache_read_hit_num, cache_write_hit_num;
//_u32 cache_evict_num, cache_evict_writeback_num;


#endif 

#define WEAR_LEVEL_THRESHOLD   35 

void flush(int); 


int pre_psn;