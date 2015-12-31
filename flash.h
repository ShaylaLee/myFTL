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

#define LB_SIZE_512  1
#define LB_SIZE_1024 2
#define LB_SIZE_2048 4

//���� flash �Ľṹ��ÿ��ҳ��������������ÿ���������ҳ��
#define SUBPAGE_NUM_PER_PAGE 4     //ÿ��ҳ��4����ҳ  
#define SECT_NUM_PER_SUBPAGE 8      //ÿ����ҳ8������
#define SECT_NUM_PER_PAGE (SUBPAGE_NUM_PER_PAGE * SECT_NUM_PER_SUBPAGE)
#define PAGE_NUM_PER_BLK  256       //ÿ����256��ҳ
#define SUBPAGE_NUM_PER_BLK  ��PAGE_NUM_PER_BLK * SUBPAGE_NUM_PER_PAGE��      //ÿ����256*4����ҳ       
#define SECT_NUM_PER_BLK  (SECT_NUM_PER_PAGE * PAGE_NUM_PER_BLK)
#define SECT_SIZE_B 512


#define SECT_BITS       3         //����λ��
#define SUBP_BITS       2         //��ҳλ��
#define PAGE_BITS       8         //ҳ��λ��
#define PAGE_SECT_BITS  13         //3+2+8
#define BLK_BITS        19        //19+13 = 32

#define NAND_STATE_FREE    -1
#define NAND_STATE_INVALID -2

//������һЩ���е�ַת���ĺ궨�壬�������� flash �ṹ���ж���
//��30λΪ��ַ��

#define SECT_MASK_IN_SECT 0x0007        //   ����λ��ʾ��ҳ������ƫ��
#define SUBP_MASK_IN_SECT 0x0018        //   �м���λ��ʾҳ����ҳƫ��
#define PAGE_MASK_IN_SECT 0x1FE0        //   8λ��ʾ����ҳƫ��
#define PAGE_SECT_MASK_IN_SECT 0x1FFF   //    ��13λ��ʾ��������ƫ��
#define BLK_MASK_IN_SECT  0xFFFFE000    //0XFFFFFF00 -> 0xFFFFFE00    ��19λ��ʾ�����ڿ�ƫ�ƣ����
#define PAGE_BITS_IN_PAGE 0x00FF        //����ҳƫ��  ��8λ 
#define BLK_MASK_IN_PAGE  0x3FFFE000    //�����ڿ�ƫ�� (ȥ����ǰ��2λ)

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
#define SECTOR(blk, page) (((blk) << PAGE_SECT_BITS) | (page))  //���ݿ�źͿ��������� �� ������

#define BLK_MASK_SECT 0x3FFFE000     // 0x3FFFFE00 -> 0x3FFFE000��30λΪ��ַ��������Ϊ��λ������sect_state�ṹ
#define PGE_MASK_SECT 0x00001FE0     // ҳ������
#define SUBP_MASK_SECT 0x00001FF8    //��ҳ����
#define OFF_MASK_SECT 0x00000007     //��ҳ����������
#define OFF_MASK_SECT_IN_PAGE 0x0000001F     //ҳ����������

#define IND_MASK_SECT (PGE_MASK_SECT | OFF_MASK_SECT)
#define BLK_BITS_SECT 17     //�������17λ����Ϊ������ַΪ30λ����������ƫ����Ҫ3+2+8��13λ
#define PGE_BITS_SECT  13    //һ��ҳ�ڵ�������
#define SUBP_BIT_PAGE  2     //һ��ҳ�ڵ���ҳ��
#define OFF_BITS_SECT  3
#define IND_BITS_SECT (PGE_BITS_SECT + OFF_BITS_SECT)
#define BLK_F_SECT(sect) (((sect) & BLK_MASK_SECT) >> IND_BITS_SECT)
#define PGE_F_SECT(sect) (((sect) & PGE_MASK_SECT) >> OFF_BITS_SECT)
#define OFF_F_SECT(sect) (((sect) & OFF_MASK_SECT))
#define PNI_F_SECT(sect) (((sect) & (~OFF_MASK_SECT)) >> OFF_BITS_SECT)
#define IND_F_SECT(sect) (((sect) & IND_MASK_SECT))
#define IS_SAME_BLK(s1, s2) (((s1) & BLK_MASK_SECT) == ((s2) & BLK_MASK_SECT))
#define IS_SAME_PAGE(s1, s2) (((s1) & (~OFF_MASK_SECT)) == ((s2) & (~OFF_MASK_SECT)))

//������״̬
struct blk_state {
   int free;       //�Ƿ��ǿ��п�
   int ec;         //�Ѳ����Ĵ���
   int isdata_blk;   //0��ʾ��־�飬1��ʾ���ݿ� 
};

//����������״̬
struct sect_state {
  _u32 free  :  1;     //�Ƿ����
  _u32 valid :  1;     //�Ƿ���Ч
  _u32 lsn   : 30;     //��Ÿ�����������Ӧ���߼������ţ�sector ������ַ���� sector ��512B��30λѰַ���֧��512GB����
};

struct nand_blk_info {
  struct blk_state state;                    // Erase Conunter
  struct sect_state sect[SECT_NUM_PER_BLK];  // Logical Sector Number

//Ϊ���޸�ҳ��С�Ϳ��С��������Щ�ֶε�ֵ��ʾ��Χ
  _s16 fpc; // free sector counter ���ڰ�����������������
  _s16 ipc; // invalid sector counter ���ڰ�����Ч����������
  _s16 lwn; // last written sector number ��ʹ�õ�������������һ��δʹ�õ��������ǽ�Ҫд���λ��
/* 
  _s32 fpc : 10; 
  _s32 ipc : 10; 
  _s32 lwn : 12; 
*/ 
   int page_status[PAGE_NUM_PER_BLK];
};

extern _u32 nand_blk_num;
extern _u8  pb_size;
extern struct nand_blk_info *nand_blk;
extern FILE *fp_erase;
int nand_init (_u32 blk_num, _u8 min_free_blk_num);
void nand_end ();
_u8 nand_page_read (_u32 psn, _u32 *lsns, _u8 isGC, int map_flag);
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
