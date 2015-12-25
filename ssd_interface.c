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

//�� disksim_iotrace.c �и�ֵ��Ŀ������ͳ��trace�������ɵ�ҳ����������ÿ����10000�Σ���ssd_interface.c�н���һ��״̬�����tp_stat_print������
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

//һ�� trace I/O �������ɶ��ҳ���ʣ�ֻ���׸��������һ��ҳ���ʿ����ǲ���ҳ����
//���ֲ���ҳ���ʻ�������ҳ���ʵ�ԭ�����ڲ���д��Ҫ�ȶ���д
int isHead;  //�ж�һ��ҳ�����Ƿ��ǵ�ǰ����ĵ�һ��ҳ����
int isTail;  //�ж�һ��ҳ�����Ƿ��ǵ�ǰ��������һ��ҳ����
// Interface between disksim & fsim 

//lpn ��ָ�߼�ҳ�ţ� vpn ��ָ���߼�ҳ��Ӧ��ӳ����Ŀ���ڵ�ӳ��ҳ��������
int lpn_to_vpn(int lpn)
{
    return (lpn - page_num_for_2nd_map_table) / MAP_ENTRIES_PER_PAGE;
}

//ͳ��ÿ����������У����� flash �����Ĵ��������ڼ�����ʱ
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

//����һ�������з��� flash �����Ĵ�����������������Ӧʱ��
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
  total_util_sect_num  = flash_numblocks;   //flash �û��ɼ�����������flash_numblocks �ɶ�ȡ flashsim �Ĳ����ļ��õ�
  total_extra_sect_num = flash_extrblocks;  //flash ������Ӧ�ռ���������� flash_extrblocks �ɶ�ȡ flashsim �Ĳ����ļ��õ�
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

  //���ö�Ӧ�� FTL �㷨�ĳ�ʼ������
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

//�ú���û���õ���Ŀǰ�� DFTL û���� wear-leveling
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

//��ӡӳ�仺����ص�ͳ�Ʋ���
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
//�Լ���ӵ�����ͳ�ơ���ӡһЩ�����ݵĺ���
//������������Ҫ�ǣ���ǰӳ�仺���У������ӳ��ҳ��������ƽ��ÿ������ҳ��������Ŀ��

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

 //���� FTL �Ķ�д��������ת�����ҳ�����·��� FTL ��
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

//�����л�����Ŀ��Ϊ clean
void cache_clean_update()
{
	int i;
	for( i = 0; i < MAP_ENTRIES; i++)
			if( map_cache_arr[i] != -1)
				opagemap[map_cache_arr[i]].update = 0;
	return;
}

//Ѱ�һ��������ȵ���Ŀ��Most recently used��
//map_cache_arr[] ��¼�����ѻ����ӳ����Ŀ���߼�ҳ��
void find_cache_max()
{
  int i; 

  for(i=0;i < MAP_ENTRIES; i++) 
      if(opagemap[map_cache_arr[i]].cache_age > opagemap[cache_max].cache_age) 
          cache_max = map_cache_arr[i];

}

//Ѱ�һ������������Ŀ��Least recently used���������滻
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

//��ʼ��ӳ�仺���״̬
//CACHE_NUM_ENTRIES ��¼�����ѻ������Ŀ������
void init_arr()
{
  int i;
  for( i = 0; i < MAP_ENTRIES; i++) {
      map_cache_arr[i] = -1;
  }
  CACHE_NUM_ENTRIES = 0;
}

// size ��ָӳ�仺��Ĵ�С�����Դ�ŵ�ӳ����Ŀ��������
// arr ָ�� map_cache_arr[] ����
//�ú������� map_cache_arr[] �����в����߼�ҳ��Ϊ val ����Ŀ������λ�ã��ڸ������е�������
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

//����ӳ�仺���еĿ���λ�ã����� -1 ��ʾ���У�����������ֵ��ʾ�߼�ҳ�ţ�
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

// ssd interface ������庯�������� trace �·��� I/O ����ת��Ϊ flash page ����ʵ��ӳ�仺��Ĺ�������·��� FTL ��
double callFsim(unsigned int secno, int scount, int operation)
{
	double delay; 
	int bcount;
	unsigned int blkno,start1; // pageno for page based FTL
  int cnt,z,i;

  int pos=-1;

  if(ftl_type == 1){ }

  if(ftl_type == 3) {   // DFTL
      //page_num_for_2nd_map_table ��¼����ӳ��ҳ������
      page_num_for_2nd_map_table = (opagemap_num / MAP_ENTRIES_PER_PAGE);  
      if(youkim_flag1 == 0 ) {
        youkim_flag1 = 1;
        init_arr();   //��ʼ��ӳ�仺��
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
  // o-pagemap scheme �� DFTL
  else if(ftl_type == 3 ) { 
    blkno = secno / SECT_NUM_PER_PAGE;  //blkno Ϊ���� I/O ���ʵ���ʼ�߼�ҳ��
    //ҳӳ��� opagemap[] ǰ page_num_for_2nd_map_table ���ŵ���ӳ��ҳ������λ�ã�����ӳ��ҳ��--����ҳ�ţ�
    //������Ҫ���� page_num_for_2nd_map_table ƫ�ƣ���ʾ����ҳ��ӳ�������
    blkno += page_num_for_2nd_map_table;  
    // bcount Ϊ���� I/O ���ʵĳ��ȣ��������Ķ��ٸ�ҳ
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
    
    //һ��ѭ������һ�� trace I/O ���󣬿��ܰ������ flash page ����
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
		  
            rqst_page_cnt++;  //ͳ�� trace I/O �·��� flash page �����û�������ҳ���ʣ�������
		
            //for(i = 1; i < opagemap_num; i++ )
            //	printf("%d opagemap[%d].ppn\n", i, opagemap[i].ppn);
		
            if( opagemap[blkno].cache_status == CACHE_VALID ) 
            {
                cache_hit++;
                if(operation){
                read_cache_hit_num++;            //����1��д��0
                }else{
                write_cache_hit_num++;
                }

                opagemap[blkno].cache_age++;      //������ age ++ ��age ��¼�����ȶȣ�age ��С�� LRU ��
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
            //�����ӳ����Ŀû�����л���
            else
            {
                //if map table in SRAM is full
                // MAP_ENTRIES ��¼����Ĵ�С��������ӳ����Ŀ��������
                // CACHE_NUM_ENTRIES ��¼�ѻ������Ŀ��
                if((MAP_ENTRIES - CACHE_NUM_ENTRIES) == 0)
                {
                    find_cache_min();  //�ҵ� LRU ����Ŀ�� cache_min ��¼���߼�ҳ��
                    evict++;           //ͳ�Ʒ����滻�Ĵ���
                    
                    //�����ѡ�е���Ŀ�ѱ����£���д�أ� 1 ��ʾ dirty �� 0 ��ʾ clean ��
                    if(opagemap[cache_min].update == 1)
                    {
                        update_reqd++;   //ͳ���滻������Ŀ����ӳ��ҳд�صĴ���
                        opagemap[cache_min].update = 0;
                        
                        //���滻������Ŀд�����棨�����ǲ��ָ���д��������Ҫ�ȶ���д��
                        send_flash_request(((cache_min-page_num_for_2nd_map_table)/MAP_ENTRIES_PER_PAGE)*SECT_NUM_PER_PAGE, SECT_NUM_PER_PAGE, 1, 2);   // read from 2nd mapping table then update it
                        send_flash_request(((cache_min-page_num_for_2nd_map_table)/MAP_ENTRIES_PER_PAGE)*SECT_NUM_PER_PAGE, SECT_NUM_PER_PAGE, 0, 2);   // write into 2nd mapping table 
                    
                        trans_page_update[(cache_min - page_num_for_2nd_map_table)/MAP_ENTRIES_PER_PAGE] ++;  //��¼ÿ��ӳ��ҳ�ĸ��´���
                    }
                    
                    //���� cache_min ��Ŀ���滻���޸�����״̬
                    opagemap[cache_min].cache_status = CACHE_INVALID;  //δ����
                    CACHE_NUM_ENTRIES--;   //�ѻ������Ŀ����һ
                    //���������� map_cache_arr[] �� cache_min ��Ӧ��λ����Ϊ��
                    pos = search_table(map_cache_arr,MAP_ENTRIES,cache_min);
                    map_cache_arr[pos]=-1;
                }

                flash_hit++;   //�ñ���δʹ��
                
                //���ڱ���ҳ������ӳ�仺���� miss��������Ҫ�������϶�ȡ��Ӧ��ӳ��ҳ��ȡ�������Ŀ
                send_flash_request(((blkno-page_num_for_2nd_map_table)/MAP_ENTRIES_PER_PAGE)*SECT_NUM_PER_PAGE, SECT_NUM_PER_PAGE, 1, 2);   // read from 2nd mapping table
                
                //����Ŀ���뻺�棬�޸�����״̬
                opagemap[blkno].cache_status = CACHE_VALID;
                opagemap[blkno].cache_age = opagemap[cache_max].cache_age + 1;
                cache_max = blkno;
                CACHE_NUM_ENTRIES++;
                pos = find_free_pos(map_cache_arr,MAP_ENTRIES);
                map_cache_arr[pos] = blkno;
            }

                //��������Ŀ�� dirty ��־
                if(operation==0){
                    write_count++;
                    opagemap[blkno].update = 1;
                }
                else
                    read_count++;
                
                //���ˣ�ӳ�仺��Ĺ������
                //�·� flash page ���� FTL �㣬��ȡ����д���û�����
                send_flash_request(blkno*SECT_NUM_PER_PAGE, SECT_NUM_PER_PAGE, operation, 1); 
                
                blkno++;
            }   //ÿһ��ѭ������һ�� flash page ��������ѭ��ִ����֮�����һ�� trace I/O ����

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
      
    //����ô� trace I/O ����Ӧ��ʱ
    delay = calculate_delay_flash();
      
    //for test ÿ 10000 �� trace I/O ��ӡһ���¼ӵ�ͳ������
    // ch_sum_page_num �� disksim_istrace.c ������
    if(ch_sum_page_num >= 10000){
      ch_sum_page_num = 0;
      tp_stat_print(outputfile);
    } 

    return delay;
}

//����ҳӳ���ȫ�����棬ȥ��ӳ�仺��Ĺ���
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

