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

//�����ж��Ƿ��ǲ��ָ���д
//isHead �� isTail �� ssd_interface.c ������
//isHeadPartial �� isTailPartial �� disksim_iotrace.h ������
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

//ɨ�����е�����飬�ҵ���Чҳ���Ŀ���Ϊ�������ղ����� victim ��
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

// ������ӳ��� opagemap[] �л�ȡ PPN�� Ȼ����� nand_page_read() ���� flash ��
/**
lsn	��ʼ�����ţ�����ҳΪ��λ����
size  ��������һ����8

*/
size_t mpm_read(sect_t lsn, sect_t size, int map_flag)
{
  int i;
  int lpn = lsn/SECT_NUM_PER_PAGE; // logical page number
  int size_page = size/SECT_NUM_PER_PAGE; // size in page 
  int sect_num;

  sect_t s_lsn;	// starting logical sector number
  sect_t s_psn; // starting physical sector number 

  sect_t lsns[SECT_NUM_PER_PAGE];  //һ��ҳ��ÿ���߼�������

  ASSERT(lpn < pagemap_num);
  ASSERT(lpn + size_page <= pagemap_num);

  memset (lsns, 0xFF, sizeof (lsns));

  sect_num = (size < SECT_NUM_PER_PAGE) ? size : SECT_NUM_PER_PAGE;

	int s_lspn = lpn * SUBPAGE_NUM_PER_PAGE;  //starting subpage logical number
  int lspns[SUBPAGE_NUM_PER_PAGE];  
  for (i = 0; i < SUBPAGE_NUM_PER_PAGE; i++) {
	  lspns[i] = s_lspn + i;
  }

	int onepage_ready[SUBPAGE_NUM_PER_PAGE] ={0};

	struct lspn_node *node_p= LRU_list; 

	//�������ݻ����б������lspn �ڻ����б��У���ֱ�ӷ��ء�
	while(node_p->next != NULL){
		for (i = 0; i < SUBPAGE_NUM_PER_PAGE; i++) {
	  	node_p->lspn == lspns[i];
			onepage_ready[i] = 1;
			read_cache_hit_num++;
  	}				
	}
  
  if(map_flag == 2){
  	//Ҫ����־ҳ
  	if(findppn_in_submap(lpn)!=-1){
 			s_psn = findppn_in_submap(lpn)* SECT_NUM_PER_PAGE;
  	}
  }
  else s_psn = pagemap[lpn].ppn * SECT_NUM_PER_PAGE;
  
  s_lsn = lpn * SECT_NUM_PER_PAGE;

  for (i = 0; i < SECT_NUM_PER_PAGE; i++) {
    lsns[i] = s_lsn + i;
  }

  size = nand_page_read(s_psn, lsns, 0, map_flag);

  ASSERT(size == SECT_NUM_PER_PAGE);

  return sect_num;
}

//��ȡһ���µĿ��п�
// small ����ָʾ�ÿ������ڴ������ҳ �� small = 1, map_flag = 1 ���������������ӳ��ҳ�� small = 0, map_flag = 2 ��
// free_blk_no[0], free_page_no[0] �ֱ�ָʾдӳ��ҳʱ����ǰʹ�õĿ��п顢��һ��д���Ŀ������ҳ������״̬��
// free_blk_no[1], free_page_no[1] �ֱ�ָʾд����ҳʱ����ǰʹ�õĿ��п顢��һ��д���Ŀ������ҳ������״̬��
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

//�������պ������� opm_write �����д����������п�����һ������ʱ������
int mpm_gc_run(int small, int map_flag)
{
  blk_t victim_blk_no;
  int merge_count;
  int i,z, j,m,q, benefit = 0;
  int k,old_flag,temp_arr[PAGE_NUM_PER_BLK],temp_arr1[PAGE_NUM_PER_BLK],map_arr[PAGE_NUM_PER_BLK]; 
  int valid_flag,pos;

  _u32 copy_lsn[SECT_NUM_PER_PAGE], copy[SECT_NUM_PER_PAGE];
  _u16 valid_sect_num,  l, s;
    
  //ѡȡ���л��յĿ�
  victim_blk_no = opm_gc_cost_benefit();
  memset(copy_lsn, 0xFF, sizeof (copy_lsn));

  s = k = OFF_F_SECT(free_page_no[small]);

  if(!((s == 0) && (k == 0))){
    printf("s && k should be 0\n");
    exit(0);
  }
 
  small = -1;

  for( q = 0; q < PAGE_NUM_PER_BLK; q++){
    if(nand_blk[victim_blk_no].page_status[q] == 1){ //map block
      for( q = 0; q  < PAGE_NUM_PER_BLK; q++) {
        if(nand_blk[victim_blk_no].page_status[q] == 0 ){
          printf("something corrupted1=%d",victim_blk_no);
        }
      }
      small = 0;      //map block
      break;
    } 
    else if(nand_blk[victim_blk_no].page_status[q] == 0){ //data block
      for( q = 0; q  < PAGE_NUM_PER_BLK; q++) {
        if(nand_blk[victim_blk_no].page_status[q] == 1 ){
          printf("something corrupted2",victim_blk_no);
        }
      }
      small = 1;    //data block
      break;
    }
  }

  ASSERT ( small == 0 || small == 1);
  pos = 0;
  merge_count = 0;

	//���ж�����־�黹�����ݿ飬�ٽ��зֱ���
	if(small == 0){

	}
	else if(small == 1){


	}else{

			printf("small error!");
	}
	
  //����victim���ڵ�ÿһ��ҳ������Ч�������Ǩ��
  for (i = 0; i < PAGE_NUM_PER_BLK; i++) 
  {
    valid_flag = nand_oob_read( SECTOR(victim_blk_no, i * SECT_NUM_PER_PAGE));

    if(valid_flag == 1)
    {
		if( small == 1 )   //data block
			valid_sect_num = nand_page_read( SECTOR(victim_blk_no, i * SECT_NUM_PER_PAGE), copy, 1, 1);
		else      //map block
			valid_sect_num = nand_page_read( SECTOR(victim_blk_no, i * SECT_NUM_PER_PAGE), copy, 1, 2);   //GCӳ��ҳ������isGC=1��ʾ����ӳ��飬isGC=5��ʾ�������ݿ鵼�µ�ӳ��ҳ����

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
  
  //������յ��ǻ������ݿ飬�ᵼ��ӳ��ҳ�������£�
  //pos��¼������Ҫֱ�Ӹ��»������ӳ��ҳ������
  //temp_arr[] ��¼���µ�ӳ��ҳ������ҳ��
  //temp_arr1[] ��¼Ǩ�Ƶ���Чҳ���߼������ţ�(temp_arr1[i]/SECT_NUM_PER_PAGE)/MAP_ENTRIES_PER_PAGE ��ʾǨ�Ƶ���Чҳ��Ӧ��ӳ��ҳ������ҳ��
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
  
  for ( i=0; i < k; i++) {      //temp_arr ��¼ӳ��ҳ������ҳ�ţ���ӳ��ҳ������Ǩ����Чҳ���¶���Ҫ����
            if (free_page_no[0] >= SECT_NUM_PER_BLK) {
                if((free_blk_no[0] = nand_get_free_blk(1)) == -1){
                   printf("we are in big trouble shudnt happen");
                }

                free_page_no[0] = 0;
            }
     
			//����Ҫ���µ�ӳ��ҳ
			//GCӳ��ҳ������isGC=1��ʾ����ӳ��飬isGC=5��ʾ�������ݿ鵼�µ�ӳ��ҳ����
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


// DFTL д���޸�ӳ���״̬��������������
/**
һ�κ������ã��Ƕ���ҳ��һ��д��
*/
size_t mpm_write(sect_t lsn, sect_t size, int map_flag)  
{
  int i;
  static int t=0;
  static int j = 0;
  int lpn = lsn/SECT_NUM_PER_PAGE; // logical page number
  int s_lspn = lsn/SECT_NUM_PER_SUBPAGE;  //logical subpage number
  int size_page = size/SECT_NUM_PER_PAGE; // size in page 
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
	
		//�߼�ҳ��ת��Ϊ�߼�������
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
	
				while (free_blk_num < 4 ){	//�����п����� 4 ����������������
					j += mpm_gc_run(small, map_flag);
				}
				mpm_gc_get_free_blk(small, map_flag);
			} 
			else {
				free_page_no[small] = 0;
			}
		}

	memset (lsns, 0xFF, sizeof (lsns));
  
  //��ȡ����ʹ�õĿ�������ҳ��s_psn ��ʾ����������
  s_psn = SECTOR(free_blk_no[small], free_page_no[small]);

  if(s_psn % SECT_NUM_PER_PAGE != 0){
    printf("s_psn: %d\n", s_psn);
  }

  //����д��Ŀ�������ҳ��
  ppn = s_psn / SECT_NUM_PER_PAGE;
  
  for (i = 0; i < SECT_NUM_PER_PAGE; i++) 
  {
    lsns[i] = s_lsn + i;
  }

	if (pagemap[lpn].ppn == 0xFF) { //��д
 
		opagemap[lpn].ppn = ppn;
		nand_page_write(s_psn, lsns, 0, map_flag);
		
    
  }else{ //����
  //�ж���ҳ
  for(i = 0; i < SUBPAGE_NUM_PER_PAGE; i++)
  	{

		int m_lspn = lspns[i];
		 s_psn1 = findppn_in_submap(m_lspn) * SECT_NUM_PER_PAGE;
		 //�Ƿ��ǲ��ָ���д��isHeadPartial �� isTailPartial �� disksim_iotrace.h ������
			 if( (map_flag != 2) && ( (isHead == 1 && isHeadPartial ==1) || (isTail == 1 && isTailPartial == 1) ) ){
				 mftl_update_write = 1;
				 nand_page_read(s_psn1, lsns, 0, 1);
				 //Ȼ���滻
			 }
		
		//���µ�subpagesд�����ݻ���
		 if(LRU_cur_num < LRU_cache_num){
				 //д����
				 struct lspn_node *anode =	(struct lspn_node*) malloc(sizeof (struct lspn_node);
				 anode->lspn = m_lspn;
				 insert_lspn_node(anode);
				 LRU_cur_num++;
				 
			 }else {
				 //������ƴ��
				 int j;
				 int back_lspns[SUBPAGE_NUM_PER_PAGE];
				 for(j=0; j< SUBPAGE_NUM_PER_PAGE; j++)
				 	{
				 		struct lspn_node *anode = get_LRU_node();
						back_lspns[j] = anode->lspn;
				 		
				 	}
				 
				 //��ȡһ����־ҳ
				small = 1;
				 if (free_page_no[small] >= SECT_NUM_PER_BLK) 
					{
				 
						if ((free_blk_no[small] = nand_get_free_blk(0)) == -1) 
						{
							int j = 0;
				 
							while (free_blk_num < 4 ){	//�����п����� 4 ����������������
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

  			//����д��Ŀ�������ҳ��
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
				 
				 //�޸���ҳӳ����ҳӳ���
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


void mpm_end()
{
  if (opagemap != NULL) {
    free(opagemap);
    free(mapdir);
  }
  
  opagemap_num = 0;
}

//���� FTL ���ͳ��״̬
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

// FTL �ĳ�ʼ������
int mpm_init(blk_t blk_num, blk_t extra_num)
{
  int i;
  int logb_num;  //��־����Ŀ

  //ҳӳ���ı���������������ҳ����־ҳ
  pagemap_num = blk_num * PAGE_NUM_PER_BLK;
 
  //����ҳӳ���
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

  //�������ݻ���
  printf("LRU_list space: %d count.", LRU_cache_num);
	LRU_cur_num=0;

  extra_blk_num = extra_num;

  free_blk_no[0] = nand_get_free_blk(0);
  free_page_no[0] = 0;
  free_blk_no[1] = nand_get_free_blk(0);
  free_page_no[1] = 0;

//����ͳ��״̬
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

//�ú���δʹ�ù�
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

//ftl �����ĺ���ָ��
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


//����subpage_map,�ҵ�lspn��Ӧ��ppn,��������µ�Ϊ��Ч,û���ҵ�����-1
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



/******************���ݻ��������������**************/
struct lspn_node * create_anode(){
	struct lspn_node * anode  = (struct lspn_node*) malloc(sizeof (struct lspn_node);
	if( anode == NULL) {
    printf("malloc anode error.\n");
		return NULL;
  }
	memset(anode,	0xFF, sizeof (struct lspn_node));
	return anode;
	
}

insert_lspn_node(struct lspn_node* anode){
	//�嵽ĩβ
	struct lspn_node *p = LRU_list;
	while(p->next != NULL){
			p = p->next;
		}
	p->next = anode;
	anode->next = NULL;
}

//���һ��LRU����������ʹ�õĽڵ㣬��ɾ����Ҫfree��
struct lspn_node * get_LRU_node()
{	
//ֱ�ӷ��ص�һ�����
		struct lspn_node *p = LRU_list;
		LRU_list = LRU_list->next;
		return p;
}




	
 

	

