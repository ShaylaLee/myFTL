/* 
 * Contributors: Youngjae Kim (youkim@cse.psu.edu)
 *               Aayush Gupta (axg354@cse.psu.edu)
 * 
 * In case if you have any doubts or questions, kindly write to: youkim@cse.psu.edu 
 *   
 * This source code provides page-level FTL scheme. 
 * 
 * Acknowledgement: We thank Jeong Uk Kang by sharing the initial version 
 * of sector-level FTL source code. 
 * 
 */

#include <stdlib.h>
#include <string.h>
#include "flash.h"
#include "ssd_interface.h"
extern int dftl_update_write;
_u32 nand_blk_num, min_fb_num;
_u8  pb_size;
struct nand_blk_info *nand_blk;

int MIN_ERASE;

/**************** NAND STAT **********************/
//用于统计 flash 各种操作的次数
void nand_stat(int option)
{ 
    switch(option){

		case DATA_PAGE_READ:
			stat_data_read_num++;
			flash_read_num++;
			break;

		case DATA_PAGE_WRITE:
			stat_data_write_num++;
			flash_write_num++;
			break;

		case TRANS_PAGE_READ:
			stat_trans_read_num++;
			flash_read_num++;
			break;
			
		case TRANS_PAGE_WRITE:
			stat_trans_write_num++;
			flash_write_num++;
			break;

		case DATA_BLOCK_ERASE:
			stat_data_erase_num++;
			flash_erase_num++;
			break;
			
		case TRANS_BLOCK_ERASE:
			stat_trans_erase_num++;
			flash_erase_num++;
			break;
			
		case GC_DATA_PAGE_READ:
			stat_gc_data_read_num++;
			flash_gc_read_num++;
			break;
    
		case GC_DATA_PAGE_WRITE:
			stat_gc_data_write_num++;
			flash_gc_write_num++;
			break;
		
		case GC_TRANS_PAGE_READ:
			stat_gc_trans_read_num++;
			flash_gc_read_num++;
			break;	

		case GC_TRANS_PAGE_WRITE:
			stat_gc_trans_write_num++;
			flash_gc_write_num++;
			break;
			
		case UPDATE_DATA_WRITE:
			stat_update_write_num++;
			update_write_num++;
			break;
	    
		case GC_TRANS_READ_FROM_MIGRATE_DATA:
			stat_gc_trans_read_num_migrate_data++;
			break;
			
		case GC_TRANS_WRITE_FROM_MIGRATE_DATA:
			stat_gc_trans_write_num_migrate_data++;
			break;
		
		case OOB_READ:
			stat_oob_read_num++;
			flash_oob_read_num++;
			break;

		case OOB_WRITE:
			stat_oob_write_num++;
			flash_oob_write_num++;
			break;
		
		default: 
			ASSERT(0);
			break;
    }
}

//重置 flash 操作的统计状态
void nand_stat_reset()
{
  stat_data_read_num = stat_data_write_num = 0;
  stat_trans_read_num = stat_trans_write_num = 0;
  stat_data_erase_num = stat_trans_erase_num = 0;
  stat_gc_data_read_num = stat_gc_data_write_num = 0;
  stat_gc_trans_read_num = stat_gc_trans_write_num = 0;
  stat_gc_trans_read_num_migrate_data = stat_gc_trans_write_num_migrate_data = 0;
  stat_oob_read_num = stat_oob_write_num = 0;
  stat_update_write_num = 0;
}

//打印各种 flash 操作的次数
void nand_stat_print(FILE *outFP)
{
  fprintf(outFP, "\n");
  fprintf(outFP, "FLASH STATISTICS\n");
  fprintf(outFP, "------------------------------------------------------------\n");
  fprintf(outFP, " Data page read except GC including update wirtes (#):%8u\n", stat_data_read_num);
  fprintf(outFP, " Data page write except GC (#):%8u\n", stat_data_write_num);
  fprintf(outFP, " Update data write (#):%8u\n", stat_update_write_num);
  fprintf(outFP, " Translation page read except GC (#):%8u\n", stat_trans_read_num);
  fprintf(outFP, " Translation page write except GC (#):%8u\n", stat_trans_write_num);
  fprintf(outFP, " Data block erase (#):%8u\n", stat_data_erase_num);
  fprintf(outFP, " Translation block erase (#):%8u\n", stat_trans_erase_num);
  fprintf(outFP, " GC data page read (#):%8u\n", stat_gc_data_read_num);
  fprintf(outFP, " GC data page write (#):%8u\n", stat_gc_data_write_num);
  fprintf(outFP, " GC translation page read (#):%8u\n", stat_gc_trans_read_num);
  fprintf(outFP, " GC translation page write (#):%8u\n", stat_gc_trans_write_num);
  fprintf(outFP, " GC translation page read from migrating data(#):%8u\n", stat_gc_trans_read_num_migrate_data);
  fprintf(outFP, " GC translation page write from migrating data (#):%8u\n", stat_gc_trans_write_num_migrate_data);
  
//  fprintf(outFP, " OOREAD  %8u   ", stat_oob_read_num);
//  fprintf(outFP, " OOWRITE %8u\n", stat_oob_write_num);
  
  fprintf(outFP, "------------------------------------------------------------\n");
}


/**************** NAND INIT **********************/
//初始化 flash 状态的函数
//blk_num 是 flash 块的数量（包括用户可见的容量和 over-provisioning 容量）
//min_free_blk_num 是触发垃圾回收操作的最小空闲块数量
int nand_init (_u32 blk_num, _u8 min_free_blk_num)
{
  _u32 blk_no;
  int i;

  nand_end();

  nand_blk = (struct nand_blk_info *)malloc(sizeof (struct nand_blk_info) * blk_num);

  if (nand_blk == NULL) 
  {
    return -1;
  }
  memset(nand_blk, 0xFF, sizeof (struct nand_blk_info) * blk_num);

  
  nand_blk_num = blk_num;

  pb_size = 1;
  min_fb_num = min_free_blk_num;
  for (blk_no = 0; blk_no < blk_num; blk_no++) {
    nand_blk[blk_no].state.free = 1;
    nand_blk[blk_no].state.ec = 0;
    nand_blk[blk_no].fpc = SECT_NUM_PER_BLK;
    nand_blk[blk_no].ipc = 0;
    nand_blk[blk_no].lwn = -1;


    for(i = 0; i<SECT_NUM_PER_BLK; i++){
      nand_blk[blk_no].sect[i].free = 1;
      nand_blk[blk_no].sect[i].valid = 0;
      nand_blk[blk_no].sect[i].lsn = -1;
    }

    for(i = 0; i < PAGE_NUM_PER_BLK; i++){
      nand_blk[blk_no].page_status[i] = -1; // 0: data, 1: map table
    }
  }
  free_blk_num = nand_blk_num;

  free_blk_idx =0;

  nand_stat_reset();
  
  return 0;
}

/**************** NAND END **********************/
void nand_end ()
{
  nand_blk_num = 0;
  if (nand_blk != NULL) {
    nand_blk = NULL;
  }
}

/**************** NAND OOB READ **********************/
//oob 操作的意义在于：物理页 oob 中存储的是该物理页对应的逻辑页号，用于垃圾回收过程中，找到对应的逻辑页号修改页映射表

int nand_oob_read(_u32 psn)
{
  blk_t pbn = BLK_F_SECT(psn);	// physical block number	
  _u16  pin = IND_F_SECT(psn);	// page index (within the block), here page index is the same as sector index
  _u16  i, valid_flag = 0;

  ASSERT(pbn < nand_blk_num);	// pbn shouldn't exceed max nand block number 

  for (i = 0; i < SECT_NUM_PER_PAGE; i++) {
    if(nand_blk[pbn].sect[pin + i].free == 0){

      if(nand_blk[pbn].sect[pin + i].valid == 1){
        valid_flag = 1;      //有效
        break;
      }
      else{
        valid_flag = -1;     //无效
        break;
      }
    }
    else{
      valid_flag = 0;        //空闲
      break;
    }
  }

  nand_stat(OOB_READ);
  
  return valid_flag;
}

void break_point()
{
  printf("break point\n");
}

/**************** NAND PAGE READ **********************/
//flash read 实质是：验证读的物理地址是否状态有效，并统计读次数
//isGC=1表示擦除映射块，isGC=5表示擦除数据块导致的映射页更新
//map_flag=1 表示是数据页，=2 表示是映射页
_u8 nand_page_read(_u32 psn, _u32 *lsns, _u8 isGC, int map_flag)
{ 
  blk_t pbn = BLK_F_SECT(psn);	// physical block number	
  _u16  pin = IND_F_SECT(psn);	// page index (within the block), here page index is the same as sector index
  _u16  i,j, valid_sect_num = 0;

  if(pbn >= nand_blk_num){
    printf("psn: %d, pbn: %d, nand_blk_num: %d\n", psn, pbn, nand_blk_num);
  }

  ASSERT(OFF_F_SECT(psn) == 0);
  if(nand_blk[pbn].state.free != 0) {
    for( i =0 ; i < nand_blk_num ; i++){
      for(j =0; j < SECT_NUM_PER_BLK;j++){
        if(nand_blk[i].sect[j].lsn == lsns[0]){
          printf("blk = %d",i);
          break;
        }
      }
    }
  }

  ASSERT(nand_blk[pbn].state.free == 0);	// block should be written with something

  if ( (isGC == 1) || (isGC == 5) ) {
    for (i = 0; i < SECT_NUM_PER_PAGE; i++) {

      if((nand_blk[pbn].sect[pin + i].free == 0) &&
         (nand_blk[pbn].sect[pin + i].valid == 1)) {
        lsns[valid_sect_num] = nand_blk[pbn].sect[pin + i].lsn;
        valid_sect_num++;
      }
    }

    if(valid_sect_num == 3){
      for(i = 0; i<SECT_NUM_PER_PAGE; i++){
        printf("pbn: %d, pin %d: %d, free: %d, valid: %d\n", 
            pbn, i, pin+i, nand_blk[pbn].sect[pin+i].free, nand_blk[pbn].sect[pin+i].valid);

      }
      exit(0);
    }

  } else if (isGC == 2) {
    for (i = 0; i < SECT_NUM_PER_PAGE; i++) {
      if (lsns[i] != -1) {
        if (nand_blk[pbn].sect[pin + i].free == 0 &&
            nand_blk[pbn].sect[pin + i].valid == 1) {
          ASSERT(nand_blk[pbn].sect[pin + i].lsn == lsns[i]);
          valid_sect_num++;
        } else {
          lsns[i] = -1;
        }
      }
    }
  } 

  else { // every sector should be "valid", "not free"   
    for (i = 0; i < SECT_NUM_PER_PAGE; i++) {
      if (lsns[i] != -1) {

        ASSERT(nand_blk[pbn].sect[pin + i].free == 0);
        ASSERT(nand_blk[pbn].sect[pin + i].valid == 1);
        ASSERT(nand_blk[pbn].sect[pin + i].lsn == lsns[i]);
        valid_sect_num++;
      }
      else{
        printf("lsns[%d]: %d shouldn't be -1\n", i, lsns[i]);
        exit(0);
      }
    }
  }
  
	if (isGC) //GC
	{      
		if (valid_sect_num > 0) {
			if( map_flag == 2 )  //trans page read
			{
				nand_stat(GC_TRANS_PAGE_READ);
				if( isGC == 5 )
					nand_stat(GC_TRANS_READ_FROM_MIGRATE_DATA);
			}
			else    //data page read
				nand_stat(GC_DATA_PAGE_READ);
		}
	} 
	else  //非GC
	{        
		if( map_flag == 2 )      //trans page read
			nand_stat(TRANS_PAGE_READ);
		else    //data page read
		{
			if(dftl_update_write == 1){
				nand_stat(UPDATE_DATA_WRITE);
				dftl_update_write = 0;
			}
			nand_stat(DATA_PAGE_READ);
		}
	}
  
  return valid_sect_num;
}

/**************** NAND PAGE WRITE **********************/
//flash write 实质是：修改页映射表、块的状态表，统计写操作次数

_u8 nand_page_write(_u32 psn, _u32 *lsns, _u8 isGC, int map_flag)
{
  blk_t pbn = BLK_F_SECT(psn);	// physical block number with psn
  _u16  pin = IND_F_SECT(psn);	// sector index, page index is the same as sector index 
  int i, valid_sect_num = 0;


  if(pbn >= nand_blk_num){
    printf("break !\n");
  }

  ASSERT(pbn < nand_blk_num);
  ASSERT(OFF_F_SECT(psn) == 0);

  if(map_flag == 2) {
        nand_blk[pbn].page_status[pin/SECT_NUM_PER_PAGE] = 1; // 1 for map table
  }
  else{
    nand_blk[pbn].page_status[pin/SECT_NUM_PER_PAGE] = 0; // 0 for data 
  }

  for (i = 0; i <SECT_NUM_PER_PAGE; i++) {

    if (lsns[i] != -1) {

      if(nand_blk[pbn].state.free == 1) {
        printf("blk num = %d",pbn);
      }

      ASSERT(nand_blk[pbn].sect[pin + i].free == 1);
      
      nand_blk[pbn].sect[pin + i].free = 0;			
      nand_blk[pbn].sect[pin + i].valid = 1;			
      nand_blk[pbn].sect[pin + i].lsn = lsns[i];	
      nand_blk[pbn].fpc--;  
      nand_blk[pbn].lwn = pin + i;	
      valid_sect_num++;
    }
    else{
      printf("lsns[%d] do not have any lsn\n", i);
    }
  }
  
  ASSERT(nand_blk[pbn].fpc >= 0);

	if (isGC)  //GC
	{
		if( map_flag == 2 )      //trans page write
		{
			nand_stat(GC_TRANS_PAGE_WRITE);
			if( isGC == 5 )
				nand_stat(GC_TRANS_WRITE_FROM_MIGRATE_DATA);
		}
		else   //data page write
			nand_stat(GC_DATA_PAGE_WRITE);
	} 
	else    //非GC
	{
		if( map_flag == 2 )      //trans page write
			nand_stat(TRANS_PAGE_WRITE);
		else
			nand_stat(DATA_PAGE_WRITE);
	}

  return valid_sect_num;
}


/**************** NAND BLOCK ERASE **********************/
//flash erase 实质是：修改块状态表，并统计擦除操作的次数

void nand_erase (_u32 blk_no, int map_flag)
{
  int i;

  ASSERT(blk_no < nand_blk_num);

  ASSERT(nand_blk[blk_no].fpc <= SECT_NUM_PER_BLK);

  if(nand_blk[blk_no].state.free != 0){ printf("debug\n"); }

  ASSERT(nand_blk[blk_no].state.free == 0);

  nand_blk[blk_no].state.free = 1;
  nand_blk[blk_no].state.ec++;
  nand_blk[blk_no].fpc = SECT_NUM_PER_BLK;
  nand_blk[blk_no].ipc = 0;
  nand_blk[blk_no].lwn = -1;


  for(i = 0; i<SECT_NUM_PER_BLK; i++){
    nand_blk[blk_no].sect[i].free = 1;
    nand_blk[blk_no].sect[i].valid = 0;
    nand_blk[blk_no].sect[i].lsn = -1;
  }

  //initialize/reset page status 
  for(i = 0; i < PAGE_NUM_PER_BLK; i++){
    nand_blk[blk_no].page_status[i] = -1;
  }

  free_blk_num++;

	if( map_flag == 2 )
		nand_stat(TRANS_BLOCK_ERASE);
	else
		nand_stat(DATA_BLOCK_ERASE);
}

/**************** NAND INVALIDATE **********************/
//当发生更新写时，将旧的物理页置为无效（修改块状态表）

void nand_invalidate (_u32 psn, _u32 lsn)
{
  _u32 pbn = BLK_F_SECT(psn);
  _u16 pin = IND_F_SECT(psn);
  if(pbn > nand_blk_num ) return;

  ASSERT(pbn < nand_blk_num);
  ASSERT(nand_blk[pbn].sect[pin].free == 0);
  if(nand_blk[pbn].sect[pin].valid != 1) { printf("debug"); }
  ASSERT(nand_blk[pbn].sect[pin].valid == 1);

  if(nand_blk[pbn].sect[pin].lsn != lsn){
    ASSERT(0);
  }

  ASSERT(nand_blk[pbn].sect[pin].lsn == lsn);
  
  nand_blk[pbn].sect[pin].valid = 0;
  nand_blk[pbn].ipc++;

  ASSERT(nand_blk[pbn].ipc <= SECT_NUM_PER_BLK);

}

//获取一个新的空闲块（找擦除次数最小的那个块）
_u32 nand_get_free_blk (int isGC) 
{
  _u32 blk_no = -1, i;
  int flag = 0,flag1=0;
  flag = 0;
  flag1 = 0;

  MIN_ERASE = 9999999;
  //in case that there is no avaible free block -> GC should be called !
  if ((isGC == 0) && (min_fb_num >= free_blk_num)) {
    //printf("min_fb_num: %d\n", min_fb_num);
    return -1;
  }

  for(i = 0; i < nand_blk_num; i++) 
  {
    if (nand_blk[i].state.free == 1) {
      flag1 = 1;

      if ( nand_blk[i].state.ec < MIN_ERASE ) {
            blk_no = i;
            MIN_ERASE = nand_blk[i].state.ec;
            flag = 1;
      }
    }
  }
  if(flag1 != 1){
    printf("no free block left=%d",free_blk_num);
    
  ASSERT(0);
  }
  if ( flag == 1) {
        flag = 0;
        ASSERT(nand_blk[blk_no].fpc == SECT_NUM_PER_BLK);
        ASSERT(nand_blk[blk_no].ipc == 0);
        ASSERT(nand_blk[blk_no].lwn == -1);
        nand_blk[blk_no].state.free = 0;

        free_blk_idx = blk_no;
        free_blk_num--;

        return blk_no;
  }
  else{
    printf("shouldn't reach...\n");
  }

  return -1;
}
