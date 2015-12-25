/* 
 * Contributors: Youngjae Kim (youkim@cse.psu.edu)
 *               Aayush Gupta (axg354@cse.psu.edu)
 * 
 * In case if you have any doubts or questions, kindly write to: youkim@cse.psu.edu 
 *   
 * Description: This source code provides page-level FTL scheme. 
 * 
 * Acknowledgement: We thank Jeong Uk Kang by sharing the initial version 
 * of sector-level FTL source code. 
 * 
 */

#include <stdlib.h>
#include <string.h>

#include "flash.h"
#include "pagemap.h"
#include "type.h"

_u32 pm_gc_cost_benefit();

struct map_dir *mapdir;

blk_t extra_blk_num;
_u32 free_blk_no[2];
_u16 free_page_no[2];

int stat_gc_called_num;
double total_gc_overhead_time;

_u32 pm_gc_cost_benefit()
{
  int max_cb = 0;
  int blk_cb;

  _u32 max_blk = -1, i;

  for (i = 0; i < nand_blk_num; i++) {
    if(i == free_blk_no[1]){ continue; }

    blk_cb = nand_blk[i].ipc;

    if (blk_cb > max_cb) {
      max_cb = blk_cb; max_blk = i;
    }
  }

  ASSERT(max_blk != -1);
  ASSERT(nand_blk[max_blk].ipc > 0);
  return max_blk;
}

size_t pm_read(sect_t lsn, sect_t size, int map_flag)
{
  int i;
  int lpn = lsn/SECT_NUM_PER_PAGE;					
  int size_page = size/SECT_NUM_PER_PAGE;  
  sect_t lsns[SECT_NUM_PER_PAGE];

  sect_t s_lsn;
  sect_t s_psn; 

  int sect_num;

  ASSERT(lpn < pagemap_num);
  ASSERT(lpn + size_page <= pagemap_num);


  memset (lsns, 0xFF, sizeof (lsns));

  sect_num = (size < SECT_NUM_PER_PAGE) ? size : SECT_NUM_PER_PAGE;

  if(map_flag == 2){
    s_psn = mapdir[lpn].ppn * SECT_NUM_PER_PAGE;
  }
  else s_psn = pagemap[lpn].ppn * SECT_NUM_PER_PAGE;

  s_lsn = lpn * SECT_NUM_PER_PAGE;

  for (i = 0; i < SECT_NUM_PER_PAGE; i++) {
    lsns[i] = s_lsn + i;
  }

  size = nand_page_read(s_psn, lsns, 0, 1);

  ASSERT(size == SECT_NUM_PER_PAGE);

  return sect_num;
}

int pm_gc_get_free_blk(int small, int map_flag)
{
  if (free_page_no[small] >= SECT_NUM_PER_BLK) {
    free_blk_no[small] = nand_get_free_blk(1);
    free_page_no[small] = 0;

    return -1;
  }
  
  return 0;
}

int pm_gc_run(int small, int map_flag)
{
  blk_t victim_blk_no = -1;
  int i, j,m, benefit = 0;
  
  int valid_flag;


  _u32 copy_lsn[SECT_NUM_PER_PAGE], copy[SECT_NUM_PER_PAGE];
  _u16 valid_sect_num, k, l, s;

  victim_blk_no = pm_gc_cost_benefit();

  memset(copy_lsn, 0xFF, sizeof (copy_lsn));

  s = k = OFF_F_SECT(free_page_no[small]);

  for (i = 0; i < PAGE_NUM_PER_BLK; i++) 
  {
    valid_flag = nand_oob_read( SECTOR(victim_blk_no, i * SECT_NUM_PER_PAGE));

    if(valid_flag == 1)
    {
        valid_sect_num = nand_page_read( SECTOR(victim_blk_no, i * SECT_NUM_PER_PAGE), copy, 1, 1);

        ASSERT(valid_sect_num == SECT_NUM_PER_PAGE);

        k=0;
        for (j = 0; j < valid_sect_num; j++) { copy_lsn[k] = copy[j]; k++; }

        benefit += pm_gc_get_free_blk(small, map_flag);
        pagemap[BLK_PAGE_NO_SECT(copy_lsn[s])].ppn = BLK_PAGE_NO_SECT(SECTOR(free_blk_no[small], free_page_no[small]));
        nand_page_write(SECTOR(free_blk_no[small],free_page_no[small]) & (~OFF_MASK_SECT), copy_lsn, 1, 1);
        free_page_no[small] += SECT_NUM_PER_PAGE;
    }
  }
      
  nand_erase(victim_blk_no, 1);

  return (benefit + 1);
}

size_t pm_write(sect_t lsn, sect_t size, int map_flag)  
{
  int i;
  int lpn = lsn/SECT_NUM_PER_PAGE;					
  int size_page = size/SECT_NUM_PER_PAGE;   	
  int ppn;
  int small;
  int sect_num;

  sect_t s_lsn;	
  sect_t s_psn; 
  sect_t s_psn1;
  sect_t lsns[SECT_NUM_PER_PAGE];;

  ASSERT(lpn < pagemap_num);
  ASSERT(lpn + size_page <= pagemap_num);

  s_lsn = lpn * SECT_NUM_PER_PAGE;

  small = 1;

  if (free_page_no[small] >= SECT_NUM_PER_BLK) 
  {
    if ((free_blk_no[small] = nand_get_free_blk(0)) == -1) 
    {
      int j = 0;

      while (free_blk_num < 4){
        j += pm_gc_run(small, map_flag);
      }
      pm_gc_get_free_blk(small, map_flag);
    } 
    else {
      free_page_no[small] = 0;
    }
  }

  memset (lsns, 0xFF, sizeof (lsns));
  sect_num = SECT_NUM_PER_PAGE;
  
  s_psn = SECTOR(free_blk_no[small], free_page_no[small]);

  if(s_psn % SECT_NUM_PER_PAGE != 0){
    printf("s_psn: %d\n", s_psn);
  }

  ppn = s_psn / SECT_NUM_PER_PAGE;

  if (pagemap[lpn].free == 0) {
    s_psn1 = pagemap[lpn].ppn * SECT_NUM_PER_PAGE;
    for(i = 0; i<SECT_NUM_PER_PAGE; i++){
      nand_invalidate(s_psn1 + i, s_lsn + i);
    } 
    nand_stat(3);
  }
  else {
    pagemap[lpn].free = 0;
  }

  for (i = 0; i < SECT_NUM_PER_PAGE; i++) 
  {
    lsns[i] = s_lsn + i;
  }

  if(map_flag == 2) {
    mapdir[lpn].ppn = ppn;
    pagemap[lpn].ppn = ppn;
  }
  else {
    pagemap[lpn].ppn = ppn;
  }

  free_page_no[small] += SECT_NUM_PER_PAGE;
  nand_page_write(s_psn, lsns, 0, map_flag);

  return sect_num;
}

void pm_end()
{
  if (pagemap != NULL) {
    free(pagemap);
    free(mapdir);
  }
  pagemap_num = 0;
}

void pagemap_reset()
{
  cache_hit = 0;
  flash_hit = 0;
  evict = 0;
  delay_flash_update = 0; 
}

int pm_init(blk_t blk_num, blk_t extra_num)
{
  int i;
  int mapdir_num;

  pagemap_num = blk_num * PAGE_NUM_PER_BLK;

  pagemap = (struct pm_entry *) malloc(sizeof (struct pm_entry) * pagemap_num);
  mapdir = (struct map_dir *)malloc(sizeof(struct map_dir) * pagemap_num / MAP_ENTRIES_PER_PAGE); 

  if ((pagemap == NULL) || (mapdir == NULL)) {
    return -1;
  }

  mapdir_num = (pagemap_num / MAP_ENTRIES_PER_PAGE);

  if((pagemap_num % MAP_ENTRIES_PER_PAGE) != 0){
    printf("pagemap_num %% MAP_ENTRIES_PER_PAGE is not zero\n"); 
    mapdir_num++;
  }

  memset(pagemap, 0xFF, sizeof (struct pm_entry) * pagemap_num);
  memset(mapdir,  0xFF, sizeof (struct map_dir) * mapdir_num);

  TOTAL_MAP_ENTRIES = pagemap_num;

  for(i = 0; i<TOTAL_MAP_ENTRIES; i++){
    pagemap[i].cache_status = 0;
    pagemap[i].cache_age = 0;
    pagemap[i].map_status = 0;
    pagemap[i].map_age = 0;
  }

  extra_blk_num = extra_num;

  free_blk_no[1] = nand_get_free_blk(0);
  free_page_no[1] = 0;

  MAP_REAL_NUM_ENTRIES = 0;
  MAP_GHOST_NUM_ENTRIES = 0;
  CACHE_NUM_ENTRIES = 0;
  SYNC_NUM = 0;

  cache_hit = 0;
  flash_hit = 0;
  evict = 0;
  read_cache_hit = 0;
  write_cache_hit = 0;

  /*
  for(i = 0; i<(pagemap_num); i++){
    pm_write(i*SECT_NUM_PER_PAGE, SECT_NUM_PER_PAGE, 1);
  }
  */

  return 0;
}

struct ftl_operation pm_operation = {
  init:  pm_init,
  read:  pm_read,
  write: pm_write,
  end:   pm_end
};
  
struct ftl_operation * pm_setup()
{
  return &pm_operation;
}
