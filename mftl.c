/* 
 * Contributors: Youngjae Kim (youkim@cse.psu.edu)
 *               Aayush Gupta (axg354@cse.psu.edu)
 * 
 * In case if you have any doubts or questions, kindly write to: youkim@cse.psu.edu 
 *
 * This source file implements the DFTL FTL scheme.
 * The detail algorithm for the DFTL can be obtainable from 
 * "DFTL: A Flash Translation Layer Employing Demand-based * Selective Caching of Page-level Address Mapping", ASPLOS, 2009".
 * 
 */

#include <stdlib.h>
#include <string.h>

#include "flash.h"
#include "dftl.h"
#include "ssd_interface.h"
#include "disksim_global.h"

//用于判断是否是部分更新写
//isHead 和 isTail 在 ssd_interface.c 中设置
//isHeadPartial 和 isTailPartial 在 disksim_iotrace.h 中设置
extern int isHeadPartial;
extern int isTailPartial;
extern int isHead;
extern int isTail;

_u32 opm_gc_cost_benefit();


blk_t extra_blk_num;
_u32 free_blk_no[2];
_u16 free_page_no[2];

extern int merge_switch_num;
extern int merge_partial_num;
extern int merge_full_num;
extern int page_num_for_2nd_map_table;
int stat_gc_called_num;
double total_gc_overhead_time;

//扫描所有的闪存块，找到无效页最多的块作为垃圾回收操作的 victim 块
_u32 mpm_gc_cost_benefit()
{
  int max_cb = 0;
  int blk_cb;

  _u32 max_blk = -1, i;

  for (i = 0; i < nand_blk_num; i++) {
    if(i == free_blk_no[0] || i == free_blk_no[1]){
      continue;
    }

    blk_cb = nand_blk[i].ipc;

    
    if (blk_cb > max_cb) {
      max_cb = blk_cb;
      max_blk = i;
    }
  }

  ASSERT(max_blk != -1);
  ASSERT(nand_blk[max_blk].ipc > 0);
  return max_blk;
}

// 读：从映射表 pagemap[] 中获取 PPN， 然后调用 nand_page_read() 进入 flash 层
/**
lsn	起始扇区号，以子页为单位对齐
size  扇区数，一般是8，一个子页大小

*/
size_t mpm_read(sect_t lsn, sect_t size, int map_flag)
{
  int i;
  int lpn = lsn/SECT_NUM_PER_PAGE; // logical page number
  int lspn = lsn/SECT_NUM_PER_SUBPAGE; //logicaL subpage number
 // int size_page = size/SECT_NUM_PER_PAGE; // size in page 
  int size_subpage = size/SECT_NUM_PER_SUBPAGE;
  int sect_num;

  sect_t s_lsn;	// starting logical sector number
  sect_t s_psn; // starting physical sector number 

  sect_t lsns[SECT_NUM_PER_PAGE];  //一个页中每个逻辑扇区号

  ASSERT(lpn < pagemap_num);
  ASSERT(lpn + size_page <= pagemap_num);

  memset (lsns, 0xFF, sizeof (lsns));

  sect_num = (size < SECT_NUM_PER_PAGE) ? size : SECT_NUM_PER_PAGE;

	struct lspn_node *node_p= LRU_list; 

	//遍历数据缓存列表，如果有lspn 在缓存列表中，则直接返回。
	while(node_p != NULL){	
	  	if(node_p->lspn == lspn)
	  	{
				read_cache_hit_num++;	
				return sect_num;
	  	}			
			node_p = node_p->next;
			
	}


	//如果要下发读整页
	/*
	int s_lspn = lpn * SUBPAGE_NUM_PER_PAGE;  //starting subpage logical number,子页所在页的子页开始。
  int lspns[SUBPAGE_NUM_PER_PAGE];  
  for (i = 0; i < SUBPAGE_NUM_PER_PAGE; i++) {
	  lspns[i] = s_lspn + i;
  }
  */

	//map_flag 自己可以判断的，从submap中，如果submap中没有保存该lpn的映射关系 说明没有日志信息。
	//map_flag 的值的传入的意义和确定的时间点。
  if(map_flag == 2){
  	//要读日志页
  	if(findppn_in_submap(lpn)!=-1){
 			s_psn = findppn_in_submap(lpn)* SECT_NUM_PER_PAGE;
  	}
  }else s_psn = pagemap[lpn].ppn * SECT_NUM_PER_PAGE;
  
  s_lsn = lpn * SECT_NUM_PER_PAGE;

  for (i = 0; i < SECT_NUM_PER_PAGE; i++) {
    lsns[i] = s_lsn + i;
  }

  size = nand_page_read(s_psn, lsns, 0, map_flag);
  ASSERT(size == SECT_NUM_PER_PAGE);

	//将其子页进行缓存，最好对整个页也进行缓存(可能下次访问内容为下个子页)
	struct lspn_node * anode = create_anode();
	anode->lspn = lspn;
	anode->dirty = 0;
	
	insert_lspn_node(anode);

  return sect_num;
}

//获取一个新的空闲块
// small 用来指示该块是用于存放数据页 （ small = 1, map_flag = 1 ），还是用来存放映射页（ small = 0, map_flag = 2 ）
// free_blk_no[0], free_page_no[0] 分别指示写映射页时：当前使用的空闲块、下一次写入的目标物理页（空闲状态）
// free_blk_no[1], free_page_no[1] 分别指示写数据页时：当前使用的空闲块、下一次写入的目标物理页（空闲状态）
int mpm_gc_get_free_blk(int small, int map_flag)
{
  if (free_page_no[small] >= SECT_NUM_PER_BLK) {

    free_blk_no[small] = nand_get_free_blk(1);

    free_page_no[small] = 0;

    return -1;
  }
  
  return 0;
}

int mpm_log_gc_run(int small, int page_flag)
{


}

//垃圾回收函数，在write 函数中触发（当空闲块少于一定数量时触发）
/**
做子页层面的回收迁移
*/
int mpm_gc_run(int small, int map_flag)
{
  blk_t victim_blk_no;
  int merge_count;
  int i,z, j,m,q, benefit = 0;
  int k,old_flag,temp_arr[PAGE_NUM_PER_BLK],temp_arr1[PAGE_NUM_PER_BLK],map_arr[PAGE_NUM_PER_BLK]; 
  int valid_flag,pos;

  _u32 copy_lsn[SECT_NUM_PER_PAGE], copy[SECT_NUM_PER_PAGE];
  _u16 valid_sect_num,  l, s;
    
  //选取进行回收的块
  victim_blk_no = opm_gc_cost_benefit();
  memset(copy_lsn, 0xFF, sizeof (copy_lsn));

  s = k = OFF_F_SECT(free_page_no[small]);

  if(!((s == 0) && (k == 0))){
    printf("s && k should be 0\n");
    exit(0);
  }
 
  small = -1;
	
  pos = 0;
  merge_count = 0;
	
  //遍历victim块内的每一个页，如果是数据页，判断有效否，对有效数据页进行迁移
  //如果是日志页，再对子页进行判断是否有效，对日志页中的有效子页进行合并（读取出），然后写回数据页，修改子页映射表，日志页计数器。
  for (i = 0; i < PAGE_NUM_PER_BLK; i++) 
  {
    valid_flag = nand_oob_read( SECTOR(victim_blk_no, i * SECT_NUM_PER_PAGE));

    if(valid_flag == 1)
    {
		if( small == 1 )   //data block
			valid_sect_num = nand_page_read( SECTOR(victim_blk_no, i * SECT_NUM_PER_PAGE), copy, 1, 1);
		else      //map block
			valid_sect_num = nand_page_read( SECTOR(victim_blk_no, i * SECT_NUM_PER_PAGE), copy, 1, 2);   //GC映射页操作，isGC=1表示擦除映射块，isGC=5表示擦除数据块导致的映射页更新

        merge_count++;

        ASSERT(valid_sect_num == SECT_NUM_PER_PAGE);
        k=0;
        for (j = 0; j < valid_sect_num; j++) {
          copy_lsn[k] = copy[j];
          k++;
        }

          benefit += opm_gc_get_free_blk(small, map_flag);

          if(nand_blk[victim_blk_no].page_status[i] == 1)    //map block
          {                       
            mapdir[(copy_lsn[s]/SECT_NUM_PER_PAGE)].ppn  = BLK_PAGE_NO_SECT(SECTOR(free_blk_no[small], free_page_no[small]));
            opagemap[copy_lsn[s]/SECT_NUM_PER_PAGE].ppn = BLK_PAGE_NO_SECT(SECTOR(free_blk_no[small], free_page_no[small]));

            nand_page_write(SECTOR(free_blk_no[small],free_page_no[small]) & (~OFF_MASK_SECT), copy_lsn, 1, 2);
            free_page_no[small] += SECT_NUM_PER_PAGE;
          }
          else{     //data block
		  
            opagemap[BLK_PAGE_NO_SECT(copy_lsn[s])].ppn = BLK_PAGE_NO_SECT(SECTOR(free_blk_no[small], free_page_no[small]));

            nand_page_write(SECTOR(free_blk_no[small],free_page_no[small]) & (~OFF_MASK_SECT), copy_lsn, 1, 1);
            free_page_no[small] += SECT_NUM_PER_PAGE;

            if(opagemap[BLK_PAGE_NO_SECT(copy_lsn[s])].cache_status == CACHE_VALID) {
              delay_flash_update++;
			  opagemap[BLK_PAGE_NO_SECT(copy_lsn[s])].update = 1;
            }
        
            else {
  
              map_arr[pos] = copy_lsn[s];
              pos++;
            } 
          }
    }
  }
  
  //如果回收的是回收数据块，会导致映射页发生更新：
  //pos记录的是需要直接更新回闪存的映射页的数量
  //temp_arr[] 记录更新的映射页的物理页号
  //temp_arr1[] 记录迁移的有效页的逻辑扇区号，(temp_arr1[i]/SECT_NUM_PER_PAGE)/MAP_ENTRIES_PER_PAGE 表示迁移的有效页对应的映射页的虚拟页号
  for(i=0;i < PAGE_NUM_PER_BLK;i++) {
      temp_arr[i]=-1;
  }
  k=0;
  for(i =0 ; i < pos; i++) {
      old_flag = 0;
      for( j = 0 ; j < k; j++) {     
           if(temp_arr[j] == mapdir[((map_arr[i]/SECT_NUM_PER_PAGE)/MAP_ENTRIES_PER_PAGE)].ppn) {  
                if(temp_arr[j] == -1){
                      printf("something wrong");
                      ASSERT(0);
                }
                old_flag = 1;
                break;
           }
      }
      if( old_flag == 0 ) {
           temp_arr[k] = mapdir[((map_arr[i]/SECT_NUM_PER_PAGE)/MAP_ENTRIES_PER_PAGE)].ppn;
           temp_arr1[k] = map_arr[i];        
           k++;
      }
      else
        save_count++;
  }
  
  for ( i=0; i < k; i++) {      //temp_arr 记录映射页的物理页号，该映射页是由于迁移有效页导致而需要更新
            if (free_page_no[0] >= SECT_NUM_PER_BLK) {
                if((free_blk_no[0] = nand_get_free_blk(1)) == -1){
                   printf("we are in big trouble shudnt happen");
                }

                free_page_no[0] = 0;
            }
     
			//读需要更新的映射页
			//GC映射页操作，isGC=1表示擦除映射块，isGC=5表示擦除数据块导致的映射页更新
			nand_page_read(temp_arr[i]*SECT_NUM_PER_PAGE,copy,5,2);

            for(m = 0; m<SECT_NUM_PER_PAGE; m++){
               nand_invalidate(mapdir[((temp_arr1[i]/SECT_NUM_PER_PAGE)/MAP_ENTRIES_PER_PAGE)].ppn*SECT_NUM_PER_PAGE+m, copy[m]);
              } 
            nand_stat(OOB_WRITE);


            mapdir[((temp_arr1[i]/SECT_NUM_PER_PAGE)/MAP_ENTRIES_PER_PAGE)].ppn  = BLK_PAGE_NO_SECT(SECTOR(free_blk_no[0], free_page_no[0]));
            opagemap[((temp_arr1[i]/SECT_NUM_PER_PAGE)/MAP_ENTRIES_PER_PAGE)].ppn = BLK_PAGE_NO_SECT(SECTOR(free_blk_no[0], free_page_no[0]));

            nand_page_write(SECTOR(free_blk_no[0],free_page_no[0]) & (~OFF_MASK_SECT), copy, 5, 2);
      
            free_page_no[0] += SECT_NUM_PER_PAGE;


  }
  if(merge_count == 0 ) 
    merge_switch_num++;
  else if(merge_count > 0 && merge_count < PAGE_NUM_PER_BLK)
    merge_partial_num++;
  else if(merge_count == PAGE_NUM_PER_BLK)
    merge_full_num++;
  else if(merge_count > PAGE_NUM_PER_BLK){
    printf("merge_count =%d PAGE_NUM_PER_BLK=%d",merge_count,PAGE_NUM_PER_BLK);
    ASSERT(0);
  }

	if( small == 1 )   //data block
		nand_erase(victim_blk_no, 1);
	else     //map block
		nand_erase(victim_blk_no, 2);

  return (benefit + 1);
}


// DFTL 写：修改映射表状态，触发垃圾回收
/**
一次函数调用，是对子页的一个写。
lsn = 子页起始扇区
size = 子页大小，8个扇区
*/
size_t mpm_write(sect_t lsn, sect_t size, int map_flag)  
{
  int i;
  static int t=0;
  static int j = 0;
  int lpn = lsn/SECT_NUM_PER_PAGE; // logical page number
  int size_page = size/SECT_NUM_PER_PAGE; // size in page 
  int lspn = lsn/SECT_NUM_PER_SUBPAGE; //logicaL subpage number
  int size_subpage = size/SECT_NUM_PER_SUBPAGE;
  int ppn;
  int small;

  sect_t lsns[SECT_NUM_PER_PAGE];
  int sect_num = SECT_NUM_PER_PAGE;

  sect_t s_lsn;	// starting logical sector number
  sect_t s_psn; // starting physical sector number 
  sect_t s_psn1;

	int lspns[SUBPAGE_NUM_PER_PAGE];
	for (i = 0; i < SUBPAGE_NUM_PER_PAGE; i++) 
	{
			lspns[i] = s_lspn + i;
	}

	ASSERT(lpn < pagemap_num);
	ASSERT(lpn + size_page <= pagemap_num);

	//写入缓存
	int f=0;
	srtuct lspn_node *p = LRU_list;
	while(p != NULL)
	{
		if(p->lpsn == lspn)
		{
			//change data
			p->dirty=1;
			f=1;
			break;
			write_cache_hit++;
		}
		p = p->next;
	}

	if(f==0) //未在缓存中
	{
		struct lspn_node * anode = create_anode();
		anode->lpsn = lspn;
		anode->dirty = 1;
		insert_lspn_node(anode);
		
	}
	
		//逻辑页号转换为逻辑扇区号
		s_lsn = lpn * SECT_NUM_PER_PAGE;
	
	
		if(map_flag == 2) //log  page
			small = 0;
		else if ( map_flag == 1) //data page
			small = 1;
		else{
			printf("something corrupted");
			exit(0);
		}
	
		if (free_page_no[small] >= SECT_NUM_PER_BLK) 
		{
	
			if ((free_blk_no[small] = nand_get_free_blk(0)) == -1) 
			{
				int j = 0;
	
				while (free_blk_num < 4 ){	//当空闲块少于 4 个，触发垃圾回收
					j += mpm_gc_run(small, map_flag);
				}
				mpm_gc_get_free_blk(small, map_flag);
			} 
			else {
				free_page_no[small] = 0;
			}
		}

	memset (lsns, 0xFF, sizeof (lsns));
  
  //获取即将使用的空闲物理页，s_psn 表示物理扇区号
  s_psn = SECTOR(free_blk_no[small], free_page_no[small]);

  if(s_psn % SECT_NUM_PER_PAGE != 0){
    printf("s_psn: %d\n", s_psn);
  }

  //即将写入的空闲物理页号
  ppn = s_psn / SECT_NUM_PER_PAGE;
  
  for (i = 0; i < SECT_NUM_PER_PAGE; i++) 
  {
    lsns[i] = s_lsn + i;
  }

	if (pagemap[lpn].ppn == 0xFF) { //新写
 
		opagemap[lpn].ppn = ppn;
		nand_page_write(s_psn, lsns, 0, map_flag);
		
    
  }else{ //更新
  //判断子页
  for(i = 0; i < SUBPAGE_NUM_PER_PAGE; i++)
  	{

		int m_lspn = lspns[i];
		 s_psn1 = findppn_in_submap(m_lspn) * SECT_NUM_PER_PAGE;
		 //是否是部分更新写，isHeadPartial 和 isTailPartial 在 disksim_iotrace.h 中设置
			 if( (map_flag != 2) && ( (isHead == 1 && isHeadPartial ==1) || (isTail == 1 && isTailPartial == 1) ) ){
				 mftl_update_write = 1;
				 nand_page_read(s_psn1, lsns, 0, 1);
				 //然后替换
			 }
		
		//将新的subpages写到数据缓存
		 if(LRU_cur_num < LRU_cache_num){
				 //写缓存
				 struct lspn_node *anode =	(struct lspn_node*) malloc(sizeof (struct lspn_node);
				 anode->lspn = m_lspn;
				 insert_lspn_node(anode);
				 LRU_cur_num++;
				 
			 }else {
				 //缓存中拼接
				 int j;
				 int back_lspns[SUBPAGE_NUM_PER_PAGE];
				 for(j=0; j< SUBPAGE_NUM_PER_PAGE; j++)
				 	{
				 		struct lspn_node *anode = get_LRU_node();
						back_lspns[j] = anode->lspn;
				 		
				 	}
				 
				 //获取一个日志页
				small = 1;
				 if (free_page_no[small] >= SECT_NUM_PER_BLK) 
					{
				 
						if ((free_blk_no[small] = nand_get_free_blk(0)) == -1) 
						{
							int j = 0;
				 
							while (free_blk_num < 4 ){	//当空闲块少于 4 个，触发垃圾回收
								j += mpm_log_gc_run(small, map_flag);
							}
							mpm_gc_get_free_blk(small, map_flag);
						} 
						else {
							free_page_no[small] = 0;
						}
					}

				s_psn = SECTOR(free_blk_no[small], free_page_no[small]);

  			if(s_psn % SECT_NUM_PER_PAGE != 0){
   				 printf("s_psn: %d\n", s_psn);
  				}

  			//即将写入的空闲物理页号
  			ppn = s_psn / SECT_NUM_PER_PAGE;

				memset (lsns, 0xFF, sizeof (lsns));
				
				for (i = 0; i < SUBPAGE_NUM_PER_PAGE; i++) 
					{
							int s = back_lspns[i];
						 for(j=0; j< SECT_NUM_PER_SUBPAGE; j++)
				 		{
				 			lsns[i*SUBPAGE_NUM_PER_PAGE + j] = s + j;			 		
				 		}
						
					}
				 
				 //修改子页映射表和页映射表
				 pagemap[lpn].ppn = ppn;
				 	for (i = 0; i < SUBPAGE_NUM_PER_PAGE; i++) 
					{
							subpagemap[cur]
						
					}
				 
				 nand_page_write(s_psn,lsns,_u8 isGC,int map_flag);
		
				 }

		}

	}
  

}

//整页写：修改映射表状态，触发垃圾回收
/**
lsn；起始扇区号
size: 一个页的大小
map_flag：1，data
**/
size_t page_write(sect_t lsn, sect_t size, int map_flag)  
{
  int i;
  static int t=0;
  static int j = 0;
  int lpn = lsn/SECT_NUM_PER_PAGE; // logical page number
  int size_page = size/SECT_NUM_PER_PAGE; // size in page 
  int ppn;
  int small;

  sect_t lsns[SECT_NUM_PER_PAGE];
  int sect_num = SECT_NUM_PER_PAGE;

  sect_t s_lsn;	// starting logical sector number
  sect_t s_psn; // starting physical sector number 
  sect_t s_psn1;


  ASSERT(lpn < opagemap_num);
  ASSERT(lpn + size_page <= opagemap_num);

  //逻辑页号转换为逻辑扇区号
  s_lsn = lpn * SECT_NUM_PER_PAGE;


  if(map_flag == 2) //log page
    small = 0;
		printf("page_write map_flag should be 1");
		exit(0);
  else if ( map_flag == 1) //data page
    small = 1;
  else{
    printf("something corrupted");
    exit(0);
  }

  if (free_page_no[small] >= SECT_NUM_PER_BLK) 
  {

    if ((free_blk_no[small] = nand_get_free_blk(0)) == -1) 
    {
      int j = 0;

      while (free_blk_num < 4 ){  //当空闲块少于 4 个，触发垃圾回收
        j += opm_gc_run(small, map_flag);
      }
      opm_gc_get_free_blk(small, map_flag);
    } 
    else {
      free_page_no[small] = 0;
    }
  }

  memset (lsns, 0xFF, sizeof (lsns));
  
  //获取即将使用的空闲物理页，s_psn 表示物理扇区号
  s_psn = SECTOR(free_blk_no[small], free_page_no[small]);

  if(s_psn % SECT_NUM_PER_PAGE != 0){
    printf("s_psn: %d\n", s_psn);
  }

  //即将写入的空闲物理页号
  ppn = s_psn / SECT_NUM_PER_PAGE;

	if(map_flag == 1) { 
		for (i = 0; i < SECT_NUM_PER_PAGE; i++) 
  {
    lsns[i] = s_lsn + i;
  }
  if (pagemap[lpn].free == 0) {
    s_psn1 = pagemap[lpn].ppn * SECT_NUM_PER_PAGE;
    //是否是部分更新写，isHeadPartial 和 isTailPartial 在 disksim_iotrace.h 中设置
	if( (map_flag != 2) && ( (isHead == 1 && isHeadPartial ==1) || (isTail == 1 && isTailPartial == 1) ) ){
	  dftl_update_write = 1;
	  nand_page_read(s_psn1, lsns, 0, 1);
	}
	
    for(i = 0; i<SECT_NUM_PER_PAGE; i++){
      nand_invalidate(s_psn1 + i, s_lsn + i);
    } 
    nand_stat(OOB_WRITE);
  }
  else {
    pagemap[lpn].free = 0;
  }
		
    pagemap[lpn].ppn = ppn;
  }
  
  

 
  free_page_no[small] += SECT_NUM_PER_PAGE;

  //向 flash 层下发写入操作
  nand_page_write(s_psn, lsns, 0, map_flag);

  return sect_num;
}


//整页写：修改映射表状态，触发垃圾回收
/**
针对日志页写
lpsns；子页数组
size: 数组大小
map_flag：2，log
闪存块不够，要触发gc
日志页不够（根据设置的日志页大小 判断） 要触发日志页的回收
**/
size_t logpage_write(sect_t *lspns, sect_t size, int map_flag)  
{
  int i;
  static int j = 0;
	int k;
  int ppn;
  int small;

  sect_t lsns[SECT_NUM_PER_PAGE];
  int sect_num = SECT_NUM_PER_PAGE;

  sect_t s_lsn;	// starting logical sector number
  sect_t s_psn; // starting physical sector number 


  if(map_flag == 2) //log page
    small = 0;	
  else if ( map_flag == 1) //data page
    small = 1;
		printf("LOGpage_write map_flag should be 2");
		exit(0);
  else{
    printf("something corrupted");
    exit(0);
  }

  if (free_page_no[small] >= SECT_NUM_PER_BLK) 
  {

    if ((free_blk_no[small] = nand_get_free_blk(0)) == -1) 
    {
      int j = 0;

      while (free_blk_num < 4 ){  //当空闲块少于 4 个，触发垃圾回收
        j += opm_gc_run(small, map_flag);
      }
      opm_gc_get_free_blk(small, map_flag);
    } 
    else {
      free_page_no[small] = 0;
    }
  }

  memset (lsns, 0xFF, sizeof (lsns));
  
  //获取即将使用的空闲物理页，s_psn 表示物理扇区号
  s_psn = SECTOR(free_blk_no[small], free_page_no[small]);

  if(s_psn % SECT_NUM_PER_PAGE != 0){
    printf("s_psn: %d\n", s_psn);
  }

  //即将写入的空闲物理页号
  ppn = s_psn / SECT_NUM_PER_PAGE;


//设置相应的lsns[]，注意不连续	
	for(k = 0; k < size; k++)
	{
		for (i = 0; i < SECT_NUM_PER_SUBPAGE; i++) 
 	 {
    	lsns[i] = lspns[k] * SECT_NUM_PER_SUBPAGE+ i;
  	}
	} 

//修改映射表
	for(k = 0; k < SUBPAGE_NUM_PER_PAGE; k++)
	{
		subpagemap.lspn[k] = lspns[k];
	} 
	subpagemap.ppn = ppn;
 
  free_page_no[small] += SECT_NUM_PER_PAGE;

  //向 flash 层下发写入操作
  nand_page_write(s_psn, lsns, 0, map_flag);

  return sect_num;
}


void opm_end()
{
  if (opagemap != NULL) {
    free(opagemap);
    free(mapdir);
  }
  
  opagemap_num = 0;
}

//重置 FTL 层的统计状态
void opagemap_reset()
{
  int i = 0;
  
  cache_hit = 0;
  rqst_page_cnt = 0;
  update_reqd = 0;
  read_cache_hit_num = 0;
  write_cache_hit_num = 0;
  flash_hit = 0;
  evict = 0;
  delay_flash_update = 0; 
  read_count =0;
  write_count=0;
  save_count = 0;
  
  for( i = 0; i < MAP_PAGE_NUM; i++ )
	trans_page_update[i] = 0;
}



void mpm_end()
{
  if (opagemap != NULL) {
    free(opagemap);
    free(mapdir);
  }
  
  opagemap_num = 0;
}

//重置 FTL 层的统计状态
void mpagemap_reset()
{
  int i = 0;
  
  cache_hit = 0;
  rqst_page_cnt = 0;
  update_reqd = 0;
  read_cache_hit_num = 0;
  write_cache_hit_num = 0;
  flash_hit = 0;
  evict = 0;
  delay_flash_update = 0; 
  read_count =0;
  write_count=0;
  save_count = 0;
  
  for( i = 0; i < MAP_PAGE_NUM; i++ )
	trans_page_update[i] = 0;
}

// FTL 的初始化函数
int mpm_init(blk_t blk_num, blk_t extra_num)
{
  int i;
  int logb_num;  //日志块数目

  //页映射表的表项数，包含数据页和日志页
  pagemap_num = blk_num * PAGE_NUM_PER_BLK;
 
  //创建页映射表
  printf("pagemap need space: %d MB.", sizeof (struct pagemap_entry) * pagemap_num / 1024 / 1024);
  pagemap= (struct pagemap_entry *) malloc(sizeof (struct pagemap_entry) * pagemap_num);
  
  if( pagemap == NULL )
  {
		printf("malloc pagemap error.\n");
		exit(0);
  }

  
  subpagemap_num = logb_num * PAGE_NUM_PER_BLK;
  printf("subpagemap need space: %d MB.", sizeof (struct subpagemap_entry) * subpagemap_num / 1024 / 1024);
  subpagemap= (struct subpagemap_entry *) malloc(sizeof (struct subpagemap_entry) * subpagemap_num); 

  if( subpagemap == NULL) {
    printf("malloc subpagemap error.\n");
		exit(0);
  }

  memset(pagemap, 0xFF, sizeof (struct pagemap_entry) * pagemap_num);
  memset(subpagemap,  0xFF, sizeof (struct subpagemap_entry) * subpagemap_num);


	//创建状态表
	valid_arr = (struct sub_state *)malloc(sizeof(struct sub_state) * blk_num * SUBPAGE_NUM_PER_BLK);
	if( valid_arr == NULL) {
    printf("malloc valid_arr error.\n");
		exit(0);
  }
	memset(valid_arr, 0xFF, sizeof (struct sub_state) * blk_num * SUBPAGE_NUM_PER_BLK);

  //创建数据缓存
  printf("LRU_list space: %d count.", LRU_cache_num);
	LRU_list = NULL;
	LRU_cur_num=0;

  extra_blk_num = extra_num;

  free_blk_no[0] = nand_get_free_blk(0);
  free_page_no[0] = 0;
  free_blk_no[1] = nand_get_free_blk(0);
  free_page_no[1] = 0;

//重置统计状态
  opagemap_reset();

  //update 2nd mapping table
  /*for(i = 0; i<mapdir_num; i++){
    ASSERT(MAP_ENTRIES_PER_PAGE == 1024);
    opm_write(i*SECT_NUM_PER_PAGE, SECT_NUM_PER_PAGE, 2);
  }
  */

  /*
  for(i = mapdir_num; i<(opagemap_num - mapdir_num - (extra_num * PAGE_NUM_PER_BLK)); i++){
    opm_write(i*SECT_NUM_PER_PAGE, SECT_NUM_PER_PAGE, 1);
  }
  */

  // update dm_table
  /*
  int j;
  for(i = mapdir_num; i<(opagemap_num - mapdir_num - (extra_num * PAGE_NUM_PER_BLK)); i++){
      for(j=0; j < SECT_NUM_PER_PAGE;j++)
        dm_table[ (i*SECT_NUM_PER_PAGE) + j] = DEV_FLASH;
  }
  */
  
  return 0;
}

//该函数未使用过
int mpm_invalid(int secno)
{
  int lpn = secno/SECT_NUM_PER_PAGE + page_num_for_2nd_map_table;	
  int s_lsn = lpn * SECT_NUM_PER_PAGE;
  int i, s_psn1;

  s_psn1 = opagemap[lpn].ppn * SECT_NUM_PER_PAGE;
  for(i = 0; i<SECT_NUM_PER_PAGE; i++){
      nand_invalidate(s_psn1 + i, s_lsn + i);
  }
  opagemap[lpn].ppn = -1;
  opagemap[lpn].cache_status = CACHE_INVALID;
  opagemap[lpn].cache_age = 0;
  opagemap[lpn].update = 0;

  return SECT_NUM_PER_PAGE;

}

//ftl 操作的函数指针
struct ftl_operation mpm_operation = {
  init:  mpm_init,
  read:  mpm_read,
  write: mpm_write,
  end:   mpm_end
};
  
struct ftl_operation * mpm_setup()
{
  return &mpm_operation;
}


//遍历subpage_map,找到lspn对应的ppn,以最后最新的为有效,没有找到返回-1
_u32 findppn_in_submap(_u32 m_lspn){
	int i,j;
	for(i=0; i<subpagemap_num; i++){
		for(j=0; j<SUBPAGE_NUM_PER_PAGE;j++){
			if(m_lspn == subpagemap[i].lspn[j]){
				return subpagemap[i].ppn;
			}
		}
	}
	return -1;
}

/**
日志页的回收
*/
logpage_gc()
{



}



/******************数据缓存链表操作函数**************/
struct lspn_node * create_anode(){
	struct lspn_node * anode  = (struct lspn_node*) malloc(sizeof (struct lspn_node);
	if( anode == NULL) {
    printf("malloc anode error.\n");
		return NULL;
  }
	memset(anode,	0xFF, sizeof (struct lspn_node));
	return anode;
	
}

//每次命中插到末尾
int insert_lspn_node(struct lspn_node* anode){
	if(LRU_cur_num >= LRU_cache_num)
	{
		merge_LRU();
	}

	//插到末尾
	struct lspn_node *p = LRU_list;
	while(p != NULL){
			p = p->next;
		}
	p = anode;
	anode->next = NULL;
	LRU_cur_num++;
	return LRU_cur_num;
}

//获得一个LRU链表中最少使用的节点，从链表中删除
struct lspn_node * get_LRU_node()
{	
//直接返回第一个结点
		if(LRU_cur_num == 0 || LRU_list == NULL)
		{
			return NULL;
		}
		struct lspn_node *p = LRU_list;
		LRU_list = LRU_list->next;
		LRU_cur_num--;
		return p;
}


int free_node(lspn_node * anode)
{

	free(anode);
	return 0;

}


/**
缓存满时，对缓存进行回收
*/
void merge_LRU()
{
//各种写回操作。 
	struct lspn_node * p = LRU_list;

//选定淘汰节点
	struct lspn_node *thenode = p;

	int thelspn = thenode->lspn;
	int thelpn = thelspn/SUBPAGE_NUM_PER_PAGE;
	int theoff = thelspn%SUBPAGE_NUM_PER_PAGE;
	int subnum = 0;
	int i;

	int lspns[SUBPAGE_NUM_PER_PAGE] = {-1};
	sect_t lspns1[SUBPAGE_NUM_PER_PAGE] = {-1};
	int lspntmp;
	p = p->next;
	while(p != NULL)
	{
		lspntmp = p->lspn;
		if(lspntmp/SUBPAGE_NUM_PER_PAGE == thelpn)//同一页
		{
			subnum++;
			lspns[lspntmp%SUBPAGE_NUM_PER_PAGE] = lspntmp;
		}	
		p = p->next;
	}

	switch(subnum)
	{
		case 0:
			//选择链表前4个节点 写回日志区
			p = LRU_list;
			for(i = 0; i< SUBPAGE_NUM_PER_PAGE; i++)
			{
				lspns1[i] = p.lspn;
				p = p->next;
			}

			logpage_write(lspns1, SUBPAGE_NUM_PER_PAGE , 2);

		case 1:
			p = LRU_list;
			for(i = 0; i< SUBPAGE_NUM_PER_PAGE; i++)
			{
				lspns1[i] = p.lspn;
				p = p->next;
			}

			logpage_write(lspns1, SUBPAGE_NUM_PER_PAGE , 2);

		case 2:
			//先对另外个子页进行一次读
			int s_lspn = thelpn * SUBPAGE_NUM_PER_PAGE;
			int a;
			for(i=0; i<SUBPAGE_NUM_PER_PAGE;i++)
			{
				if(lspns[i] == -1)
				{
					a = s_lspn + i;
				}
			}
			int lsn = a * SECT_NUM_PER_SUBPAGE;

			mpm_read(lsn, SECT_NUM_PER_SUBPAGE, 1);  //日志页和数据页的处理。

			//再一起写回
			lsn = s_lspn * SECT_NUM_PER_SUBPAGE;
			page_write(lsn, SECT_NUM_PER_PAGE, 1);


		case 3:
			//凑成一页直接写回
			int lsn = thelpn * SECT_NUM_PER_PAGE;
			//lsn = lspns[0] * SECT_NUM_PER_SUBPAGE;
			page_write(lsn, SECT_NUM_PER_PAGE, 1);			
			
	}

}



	
 

	

