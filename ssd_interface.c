/* 
 * Contributors: Youngjae Kim (youkim@cse.psu.edu)
 *               Aayush Gupta (axg354@cse.psu.edu_
 *   
 * In case if you have any doubts or questions, kindly write to: youkim@cse.psu.edu 
 *
 * This source plays a role as bridiging disksim and flash simulator. 
 * 
 * Request processing flow: 
 *
 *  1. Request is sent to the simple flash device module. 
 *  2. This interface determines FTL type. Then, it sends the request
 *     to the lower layer according to the FTL type. 
 *  3. It returns total time taken for processing the request in the flash. 
 *
 */

#include "ssd_interface.h"
#include "disksim_global.h"
#include "dftl.h"

extern int merge_switch_num;
extern int merge_partial_num;
extern int merge_full_num;
int old_merge_switch_num = 0;
int old_merge_partial_num = 0;
int old_merge_full_num= 0;
int old_flash_gc_read_num = 0;
int old_flash_erase_num = 0;
int req_count_num = 1;
int cache_hit, rqst_page_cnt;
int cache_evict_write_back;
int flag1 = 1;
int count = 0;

//在 disksim_iotrace.c 中赋值，目的在于统计trace请求生成的页访问数量，每访问10000次，在ssd_interface.c中进行一次状态输出（tp_stat_print函数）
double ch_sim_time;
long ch_sum_page_num;

int page_num_for_2nd_map_table;
int map_cache_arr[MAP_ENTRIES];


/***********************************************************************
  Variables for statistics    
 ***********************************************************************/
unsigned int cnt_read = 0;
unsigned int cnt_write = 0;
unsigned int cnt_delete = 0;
unsigned int cnt_evict_from_flash = 0;
unsigned int cnt_evict_into_disk = 0;
unsigned int cnt_fetch_miss_from_disk = 0;
unsigned int cnt_fetch_miss_into_flash = 0;

double sum_of_queue_time = 0.0;
double sum_of_service_time = 0.0;
double sum_of_response_time = 0.0;
unsigned int total_num_of_req = 0;


/***********************************************************************
  Cache
 ***********************************************************************/
int cache_min = -1;
int cache_max = 0;

//一个 trace I/O 可能生成多个页访问，只有首个或者最后一个页访问可能是部分页访问
//区分部分页访问还是完整页访问的原因，在于部分写需要先读后写
int isHead;  //判断一个页访问是否是当前请求的第一个页访问
int isTail;  //判断一个页访问是否是当前请求的最后一个页访问
// Interface between disksim & fsim 

//lpn 是指逻辑页号， vpn 是指该逻辑页对应的映射条目所在的映射页的虚拟编号
int lpn_to_vpn(int lpn)
{
    return (lpn - page_num_for_2nd_map_table) / MAP_ENTRIES_PER_PAGE;
}

//统计每次请求过程中，发生 flash 操作的次数，用于计算延时
void reset_flash_stat()
{
  flash_read_num = 0;
  flash_write_num = 0;
  flash_gc_read_num = 0;
  flash_gc_write_num = 0; 
  flash_erase_num = 0;
  flash_oob_read_num = 0;
  flash_oob_write_num = 0; 
  update_write_num = 0;
}

FILE *fp_flash_stat;
FILE *fp_gc;
FILE *fp_gc_timeseries;
double gc_di =0 ,gc_ti=0;

//根据一次请求中发生 flash 操作的次数，计算该请求的响应时间
double calculate_delay_flash()
{
  double delay;
  double read_delay, write_delay;
  double erase_delay;
  double gc_read_delay, gc_write_delay;
  double oob_write_delay, oob_read_delay;

  oob_read_delay  = (double)OOB_READ_DELAY  * flash_oob_read_num;
  oob_write_delay = (double)OOB_WRITE_DELAY * flash_oob_write_num;

  read_delay     = (double)READ_DELAY  * flash_read_num; 
  write_delay    = (double)WRITE_DELAY * flash_write_num; 
  erase_delay    = (double)ERASE_DELAY * flash_erase_num; 

  gc_read_delay  = (double)GC_READ_DELAY  * flash_gc_read_num; 
  gc_write_delay = (double)GC_WRITE_DELAY * flash_gc_write_num; 


  delay = read_delay + write_delay + erase_delay + gc_read_delay + gc_write_delay + 
    oob_read_delay + oob_write_delay;

  if( flash_gc_read_num > 0 || flash_gc_write_num > 0 || flash_erase_num > 0 ) {
    gc_ti += delay;
  }
  else {
    gc_di += delay;
  }

  if(warm_done == 1){
    fprintf(fp_gc_timeseries, "%d\t%d\t%d\t%d\t%d\t%d\n", 
      req_count_num, merge_switch_num - old_merge_switch_num, 
      merge_partial_num - old_merge_partial_num, 
      merge_full_num - old_merge_full_num, 
      flash_gc_read_num,
      flash_erase_num);

    old_merge_switch_num = merge_switch_num;
    old_merge_partial_num = merge_partial_num;
    old_merge_full_num = merge_full_num;
    req_count_num++;
  }

  reset_flash_stat();

  return delay;
}


/***********************************************************************
  Initialize Flash Drive 
  ***********************************************************************/

void initFlash()
{
  blk_t total_blk_num;
  blk_t total_util_blk_num;
  blk_t total_extr_blk_num;

  // total number of sectors    
  total_util_sect_num  = flash_numblocks;   //flash 用户可见的扇区数，flash_numblocks 由读取 flashsim 的参数文件得到
  total_extra_sect_num = flash_extrblocks;  //flash 过量供应空间的扇区数， flash_extrblocks 由读取 flashsim 的参数文件得到
  total_sect_num = total_util_sect_num + total_extra_sect_num; 

  // total number of flash blocks 
  total_blk_num      = total_sect_num / SECT_NUM_PER_BLK;     // total block number
  total_util_blk_num = total_util_sect_num / SECT_NUM_PER_BLK;    // total unique block number

  global_total_blk_num = total_util_blk_num;

  total_extr_blk_num = total_blk_num - total_util_blk_num;        // total extra block number

  ASSERT(total_extr_blk_num != 0);

  if (nand_init(total_blk_num, 4) < 0) {   //free_blk_num
    EXIT(-4); 
  }

  //调用对应的 FTL 算法的初始化函数
  switch(ftl_type){

    // pagemap
    case 1: ftl_op = pm_setup(); break;
    // blockmap
    //case 2: ftl_op = bm_setup(); break;
    // o-pagemap 
    case 3: ftl_op = opm_setup(); break;
    // fast
    case 4: ftl_op = lm_setup(); break;

    default: break;
  }

  ftl_op->init(total_util_blk_num, total_extr_blk_num);

  nand_stat_reset();
}

//该函数没有用到，目前的 DFTL 没有做 wear-leveling
void printWearout()
{
  int i;
  FILE *fp = fopen("wearout", "w");
  
  for(i = 0; i<nand_blk_num; i++)
  {
    fprintf(fp, "%d %d\n", i, nand_blk[i].state.ec); 
  }

  fclose(fp);
}

//打印映射缓存相关的统计参数
void cache_stat_print(FILE *outFP){ 
	int i;

  fprintf(outFP, "\n\n");
  fprintf(outFP, "CACHE  STATISTICS\n");
  fprintf(outFP, "------------------------------------------------------------\n");
  fprintf(outFP, " Page-level data request for flash in the workload (#):%8u\n", rqst_page_cnt);
  fprintf(outFP, " Cache hit num (#):%8u\n", cache_hit);
  fprintf(outFP, " Cache hit rate (#):%.6f\n", (double)cache_hit/rqst_page_cnt);
  fprintf(outFP, " Read cache hit (#):%8u\n", read_cache_hit_num);
  fprintf(outFP, " Write cache hit (#):%8u\n\n", write_cache_hit_num);
  fprintf(outFP, " Cache evict (#):%8u\n", evict);
  fprintf(outFP, " Cache evict write back (#):%8u\n", update_reqd);
  fprintf(outFP, " Cache evict write back rate (#):%.6f\n\n",(double)update_reqd/evict);
  
  //fprintf(outFP, " translation page updates (vpn--#):\n");
  //for( i = 0; i < MAP_PAGE_NUM; i ++ )
	//fprintf(outFP, "%d--%d\t", i, trans_page_update[i]);
  fprintf(outFP, "------------------------------------------------------------\n");

}

// add dftl cache statistics
//--------------------------------------------------------------------------------
//自己添加的用于统计、打印一些新数据的函数
//这里新数据主要是：当前映射缓存中，缓存的映射页的数量，平均每个缓存页包含的条目数

int tp_index = 0;
void tp_stat_print(FILE *outFP){
    int i;
    int map_page_num = 0,entry_num = 0,update_entry_num = 0, vpn;
    int vpn_in_cache[MAP_PAGE_NUM] = {0};  // vpn in cache
    int updat_entry_num[MAP_PAGE_NUM] = {0};
    
    if(tp_index == 0){
        tp_index = 1;
        fprintf(outFP, "\n\n");
        fprintf(outFP, "TP NODE STATISTICS\n");
        fprintf(outFP, "------------------------------------------------------------\n");
        //fprintf(outFP, "sim_time  map_page_num  entry_num  vpn  Distribution \n");
    }
    
    for(i = 0;i<MAP_ENTRIES;i++){
        if(map_cache_arr[i] != -1){
            entry_num++;
            vpn = lpn_to_vpn(map_cache_arr[i]);
            vpn_in_cache[vpn]++;
            if(opagemap[map_cache_arr[i]].update == 1){
                update_entry_num++;
                updat_entry_num[vpn]++;
            }
        }
    }

    for(i = 0;i<MAP_PAGE_NUM;i++){
        if(vpn_in_cache[i] != 0)
            map_page_num++;
    }
    
    fprintf(outFP,"sim_time:%lf\t  total_tp_page:%d\t total_entry_num:%d\t total_update_entry_num:%d\n",ch_sim_time,map_page_num,entry_num,update_entry_num);
    fprintf(outFP,"vpn   \t entry_num \t update_entry_num\n");
    for(i = 0;i<MAP_PAGE_NUM;i++){
        if(vpn_in_cache[i] != 0)
        fprintf(outFP,"%-8d %-8d %-8d\n",i,vpn_in_cache[i],updat_entry_num[i]);
    }
}


void endFlash()
{
  nand_stat_print(outputfile);
  cache_stat_print(outputfile);
  ftl_op->end;
  nand_end();
}  

/***********************************************************************
  Send request (lsn, sector_cnt, operation flag)
  ***********************************************************************/

 //调用 FTL 的读写函数，将转换后的页请求下发到 FTL 层
void send_flash_request(int start_blk_no, int block_cnt, int operation, int map_flag)
{
	int size;
	//size_t (*op_func)(sect_t lsn, size_t size);
	size_t (*op_func)(sect_t lsn, size_t size, int map_flag);

        if((start_blk_no + block_cnt) >= total_util_sect_num){
          printf("start_blk_no: %d, block_cnt: %d, total_util_sect_num: %d\n", 
              start_blk_no, block_cnt, total_util_sect_num);
          exit(0);
        }

	switch(operation){
     
	//write
	case 0:
		op_func = ftl_op->write;
		while (block_cnt> 0) {
			size = op_func(start_blk_no, block_cnt, map_flag);
			start_blk_no += size;
			block_cnt-=size;
		}
		break;
	//read
	case 1:
		op_func = ftl_op->read;
		while (block_cnt> 0) {
			size = op_func(start_blk_no, block_cnt, map_flag);
			start_blk_no += size;
			block_cnt-=size;
		}
		break;

	default: 
		break;
	}
}

//将所有缓存条目置为 clean
void cache_clean_update()
{
	int i;
	for( i = 0; i < MAP_ENTRIES; i++)
			if( map_cache_arr[i] != -1)
				opagemap[map_cache_arr[i]].update = 0;
	return;
}

//寻找缓存中最热的条目（Most recently used）
//map_cache_arr[] 记录所有已缓存的映射条目的逻辑页号
void find_cache_max()
{
  int i; 

  for(i=0;i < MAP_ENTRIES; i++) 
      if(opagemap[map_cache_arr[i]].cache_age > opagemap[cache_max].cache_age) 
          cache_max = map_cache_arr[i];

}

//寻找缓存中最冷的条目（Least recently used），用于替换
void find_cache_min()
{
  int i; 
  int temp = 99999999;

  for(i=0; i < MAP_ENTRIES; i++) {
        if(opagemap[map_cache_arr[i]].cache_age <= temp) {
            cache_min = map_cache_arr[i];
            temp = opagemap[map_cache_arr[i]].cache_age;
        }
  }  
}

//初始化映射缓存的状态
//CACHE_NUM_ENTRIES 记录的是已缓存的条目的数量
void init_arr()
{
  int i;
  for( i = 0; i < MAP_ENTRIES; i++) {
      map_cache_arr[i] = -1;
  }
  CACHE_NUM_ENTRIES = 0;
}

// size 是指映射缓存的大小（可以存放的映射条目的数量）
// arr 指向 map_cache_arr[] 数组
//该函数是在 map_cache_arr[] 数组中查找逻辑页号为 val 的条目所处的位置（在该数组中的索引）
int search_table(int *arr, int size, int val) 
{
    int i;
    for(i =0 ; i < size; i++) {
        if(arr[i] == val) {
            return i;
        }
    }

    printf("shouldnt come here for search_table()=%d,%d",val,size);
    for( i = 0; i < size; i++) {
      if(arr[i] != -1) {
        printf("arr[%d]=%d ",i,arr[i]);
      }
    }
    exit(1);
    return -1;
}

//查找映射缓存中的空闲位置（等于 -1 表示空闲，其它正整数值表示逻辑页号）
int find_free_pos( int *arr, int size)
{
    int i;
    for(i = 0 ; i < size; i++) {
        if(arr[i] == -1) {
            return i;
        }
    } 
    printf("shouldnt come here for find_free_pos()");
    exit(1);
    return -1;
}

int youkim_flag1=0;

// ssd interface 层的主体函数，接受 trace 下发的 I/O 请求，转换为 flash page 请求，实行映射缓存的管理，最后下发给 FTL 层
double callFsim(unsigned int secno, int scount, int operation)
{
	double delay; 
	int bcount;
	unsigned int blkno,start1; // pageno for page based FTL
  int cnt,z,i;

  int pos=-1;

  if(ftl_type == 1){ }

  if(ftl_type == 3) {   // DFTL
      //page_num_for_2nd_map_table 记录的是映射页的数量
      page_num_for_2nd_map_table = (opagemap_num / MAP_ENTRIES_PER_PAGE);  
      if(youkim_flag1 == 0 ) {
        youkim_flag1 = 1;
        init_arr();   //初始化映射缓存
      }

      if((opagemap_num % MAP_ENTRIES_PER_PAGE) != 0){
        page_num_for_2nd_map_table++;
      }
  }
  
  // page based FTL 
  if(ftl_type == 1 ) { 
    blkno = secno / SECT_NUM_PER_PAGE;
    bcount = (secno + scount -1)/SECT_NUM_PER_PAGE - (secno)/SECT_NUM_PER_PAGE + 1;
  }  
  // block based FTL 
  else if(ftl_type == 2){
    blkno = secno/SECT_NUM_PER_PAGE;
    bcount = (secno + scount -1)/SECT_NUM_PER_PAGE - (secno)/SECT_NUM_PER_PAGE + 1;
  }
  // o-pagemap scheme 即 DFTL
  else if(ftl_type == 3 ) { 
    blkno = secno / SECT_NUM_PER_PAGE;  //blkno 为本次 I/O 访问的起始逻辑页号
    //页映射表 opagemap[] 前 page_num_for_2nd_map_table 项存放的是映射页的物理位置（虚拟映射页号--物理页号）
    //所以需要加上 page_num_for_2nd_map_table 偏移，表示数据页的映射表区域
    blkno += page_num_for_2nd_map_table;  
    // bcount 为本次 I/O 访问的长度，即连续的多少个页
    bcount = (secno + scount -1)/SECT_NUM_PER_PAGE - (secno)/SECT_NUM_PER_PAGE + 1;
  }  
  // FAST scheme
  else if(ftl_type == 4){
    blkno = secno/SECT_NUM_PER_PAGE;
    bcount = (secno + scount -1)/SECT_NUM_PER_PAGE - (secno)/SECT_NUM_PER_PAGE + 1;
  }

  cnt = bcount;
  
  switch(operation)
  {
    //write/read
    case 0:
    case 1:
    
    //一次循环处理一个 trace I/O 请求，可能包括多个 flash page 请求
    while(cnt > 0)
    {   
	    isHead = 0;
        isTail = 0;
        if( cnt == bcount )
            isHead = 1;
        if( cnt == 1)
            isTail = 1;
          cnt--;

        // page based FTL
        if(ftl_type == 1){
          send_flash_request(blkno*SECT_NUM_PER_PAGE, SECT_NUM_PER_PAGE, operation, 1); 
          blkno++;
        }

        // blck based FTL
        else if(ftl_type == 2){
          send_flash_request(blkno*SECT_NUM_PER_PAGE, SECT_NUM_PER_PAGE, operation, 1); 
          blkno++;
        }

        // opagemap ftl scheme
        else if(ftl_type == 3)
        {   
            /************************************************
                primary map table 
            *************************************************/
            
            //1. pagemap in SRAM 
		  
            rqst_page_cnt++;  //统计 trace I/O 下发的 flash page 请求（用户的数据页访问）的数量
		
            //for(i = 1; i < opagemap_num; i++ )
            //	printf("%d opagemap[%d].ppn\n", i, opagemap[i].ppn);
		
            if( opagemap[blkno].cache_status == CACHE_VALID ) 
            {
                cache_hit++;
                if(operation){
                read_cache_hit_num++;            //读是1，写是0
                }else{
                write_cache_hit_num++;
                }

                opagemap[blkno].cache_age++;      //命中则 age ++ （age 记录的是热度，age 最小即 LRU ）
				//opagemap[blkno].cache_age = opagemap[cache_max].cache_age + 1;
				// cache_max = blkno;
               
                if ( cache_max == -1 ) 
                {
                    cache_max = 0;
                    find_cache_max();
                    printf("Never happend.\n");
                }  
                
                if(opagemap[cache_max].cache_age <= opagemap[blkno].cache_age)
                    cache_max = blkno;
            }

            //2. opagemap not in SRAM 
            //请求的映射条目没有命中缓存
            else
            {
                //if map table in SRAM is full
                // MAP_ENTRIES 记录缓存的大小（可容纳映射条目的数量）
                // CACHE_NUM_ENTRIES 记录已缓存的条目数
                if((MAP_ENTRIES - CACHE_NUM_ENTRIES) == 0)
                {
                    find_cache_min();  //找到 LRU 的条目， cache_min 记录其逻辑页号
                    evict++;           //统计发生替换的次数
                    
                    //如果被选中的条目已被更新，则写回（ 1 表示 dirty ， 0 表示 clean ）
                    if(opagemap[cache_min].update == 1)
                    {
                        update_reqd++;   //统计替换了脏条目导致映射页写回的次数
                        opagemap[cache_min].update = 0;
                        
                        //将替换的脏条目写回闪存（由于是部分更新写，所以需要先读后写）
                        send_flash_request(((cache_min-page_num_for_2nd_map_table)/MAP_ENTRIES_PER_PAGE)*SECT_NUM_PER_PAGE, SECT_NUM_PER_PAGE, 1, 2);   // read from 2nd mapping table then update it
                        send_flash_request(((cache_min-page_num_for_2nd_map_table)/MAP_ENTRIES_PER_PAGE)*SECT_NUM_PER_PAGE, SECT_NUM_PER_PAGE, 0, 2);   // write into 2nd mapping table 
                    
                        trans_page_update[(cache_min - page_num_for_2nd_map_table)/MAP_ENTRIES_PER_PAGE] ++;  //记录每个映射页的更新次数
                    }
                    
                    //由于 cache_min 条目被替换，修改以下状态
                    opagemap[cache_min].cache_status = CACHE_INVALID;  //未缓存
                    CACHE_NUM_ENTRIES--;   //已缓存的条目数减一
                    //将缓存数组 map_cache_arr[] 中 cache_min 对应的位置置为空
                    pos = search_table(map_cache_arr,MAP_ENTRIES,cache_min);
                    map_cache_arr[pos]=-1;
                }

                flash_hit++;   //该变量未使用
                
                //由于本次页请求在映射缓存中 miss，所以需要到闪存上读取相应的映射页，取请求的条目
                send_flash_request(((blkno-page_num_for_2nd_map_table)/MAP_ENTRIES_PER_PAGE)*SECT_NUM_PER_PAGE, SECT_NUM_PER_PAGE, 1, 2);   // read from 2nd mapping table
                
                //将条目载入缓存，修改如下状态
                opagemap[blkno].cache_status = CACHE_VALID;
                opagemap[blkno].cache_age = opagemap[cache_max].cache_age + 1;
                cache_max = blkno;
                CACHE_NUM_ENTRIES++;
                pos = find_free_pos(map_cache_arr,MAP_ENTRIES);
                map_cache_arr[pos] = blkno;
            }

                //置载入条目的 dirty 标志
                if(operation==0){
                    write_count++;
                    opagemap[blkno].update = 1;
                }
                else
                    read_count++;
                
                //至此，映射缓存的管理完成
                //下发 flash page 请求到 FTL 层，读取或者写入用户数据
                send_flash_request(blkno*SECT_NUM_PER_PAGE, SECT_NUM_PER_PAGE, operation, 1); 
                
                blkno++;
            }   //每一次循环处理一个 flash page 请求，整个循环执行完之后，完成一次 trace I/O 请求

            // FAST scheme  
            else if(ftl_type == 4){ 

                if(operation == 0){
                    write_count++;
                }
                else read_count++;

                send_flash_request(blkno*SECT_NUM_PER_PAGE, SECT_NUM_PER_PAGE, operation, 1); //cache_min is a page for page baseed FTL
                blkno++;
            }
        }
        break;
    }
      
    //计算该次 trace I/O 的响应延时
    delay = calculate_delay_flash();
      
    //for test 每 10000 次 trace I/O 打印一次新加的统计数据
    // ch_sum_page_num 在 disksim_istrace.c 中设置
    if(ch_sum_page_num >= 10000){
      ch_sum_page_num = 0;
      tp_stat_print(outputfile);
    } 

    return delay;
}

//假设页映射表被全部缓存，去掉映射缓存的管理
double callFsim2(unsigned int secno, int scount, int operation)
{
  double delay; 
  int bcount;
  unsigned int blkno; // pageno for page based FTL
  int cnt,z,i;

  int pos=-1;
  
//printf("zy:1. callFsim2 begin.\n");

	if(ftl_type == 3) 
	{
		page_num_for_2nd_map_table = (opagemap_num / MAP_ENTRIES_PER_PAGE);
		if(youkim_flag1 == 0 ) {
			youkim_flag1 = 1;
			init_arr();
		}

		if((opagemap_num % MAP_ENTRIES_PER_PAGE) != 0){
			page_num_for_2nd_map_table++;
		}
  
		blkno = secno / SECT_NUM_PER_PAGE;
		blkno += page_num_for_2nd_map_table;
		bcount = (secno + scount -1)/SECT_NUM_PER_PAGE - (secno)/SECT_NUM_PER_PAGE + 1; 
	}

  cnt = bcount;
  
  switch(operation)
  {
    //write/read
    case 0:
    case 1:

    while(cnt > 0)
    {   
        cnt--;

		if(ftl_type == 3)
        {	
          send_flash_request(blkno*SECT_NUM_PER_PAGE, SECT_NUM_PER_PAGE, operation, 1); 
          blkno++;
        }
    }
    break;
  }
  //printf("zy:2. callFsim2 over.\n");
  return 0;
}

