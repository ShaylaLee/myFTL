/* 
 * Contributors: Youngjae Kim (youkim@cse.psu.edu)
 *               Aayush Gupta (axg354@cse.psu.edu)
 * 
 * In case if you have any doubts or questions, kindly write to: youkim@cse.psu.edu 
 *
 * This source file implements the FAST FTL scheme by * Dr. Sang-Won Lee. 
 * The detail algorithm for the FAST FTL can be obtainable from  
 * "A Log Buffer-based Flash Translation Layer Using Fully-Associative Sector Translation, 
 * ACM Transactions on Embedded Computing Systems (TECS), 2007".  
 * We try to implement of FAST ftl as exactly as possible.  
 * 
 */

#include <stdlib.h>
#include <string.h>
#include "flash.h"
#include "ssd_interface.h"

int merge_switch_num = 0;
int merge_partial_num = 0;
int merge_full_num = 0;

/* Log blocks are composed of ONE sequential log block 
  and random log blocks for the rest */
struct LogMap{
    int fpc; // free page count within a block 
    int pbn; // physical blk no of the log block 
    int lpn[PAGE_NUM_PER_BLK];
    int lpn_status[PAGE_NUM_PER_BLK]; // -1: invalid, 0: free, 1: valid
};

// This is only for ONE sequential log block 
struct seq_log_blk{
    struct LogMap logblk;
    int data_blk;           // sequential log block owner
};

struct LogMap *PMT;     // page mapping table for log blocks
int *BMT;               // block mapping table for data blocks

struct seq_log_blk global_SW_blk;       // pbn which is being used as SW_blk

int total_log_blk_num;
int total_blk_num;
int global_currRWblk = 1;
int global_firstRWblk = 0;
int free_SW_blk_num;
int free_RW_blk_num;

/********************* READ **********************************/ 
size_t lm_read(sect_t lsn, sect_t size, int map_flag)
{
      int i, k, m, h;
      int read_flag;
      int lpn = lsn/SECT_NUM_PER_PAGE;	                
      int lbn = lsn/SECT_NUM_PER_BLK;                             
      int ppn;
      int pbn;
      int size_page = size/SECT_NUM_PER_PAGE;   		
      int offset = lpn%PAGE_NUM_PER_BLK;
      int valid_flag;
      int sect_num;

      sect_t s_lsn;	
      sect_t s_psn; 

      sect_t copy[SECT_NUM_PER_PAGE];
      memset (copy, 0xFF, sizeof (copy));

      if(BMT[lbn] == -1){
        ASSERT(0);
      }

      sect_num = 4;

      s_psn = ((BMT[lbn] * PAGE_NUM_PER_BLK + offset) * SECT_NUM_PER_PAGE);
      s_lsn = lpn * SECT_NUM_PER_PAGE;

      for (h = 0; h < SECT_NUM_PER_PAGE; h++) {
          copy[h] = s_lsn + h;
      }

      valid_flag = nand_oob_read(s_psn);

      if(valid_flag == 1){        
              size = nand_page_read(s_psn, copy, 0, 1);
      }
      else if(valid_flag == -1){    

        read_flag = 0;

        for( k = 0; (k < total_log_blk_num) && (read_flag != 1); k++){
          for( m = 0; m < PAGE_NUM_PER_BLK; m++){
              if((PMT[k].lpn[m] == lpn) && (PMT[k].lpn_status[m] == 1)) {
                  s_psn = ((PMT[k].pbn * PAGE_NUM_PER_BLK + m) * SECT_NUM_PER_PAGE);
                  s_lsn = lpn * SECT_NUM_PER_PAGE;

                  for (i = 0; i < SECT_NUM_PER_PAGE; i++) {
                      copy[i] = s_lsn + i;
                  }

                  size = nand_page_read(s_psn, copy, 0, 1);

                  read_flag = 1;
                  break;
                }
            }
        }
      }
      else{ 
        stat_data_read_num++;
        flash_read_num++;

        return 4;

      }

      ASSERT(size == SECT_NUM_PER_PAGE);

      return sect_num;
}

/********************* WRITE **********************************/ 
struct seq_log_blk get_new_SW_blk()
{
    struct seq_log_blk new_SW_blk;
    int i;

    new_SW_blk.logblk.pbn = nand_get_free_blk(1);
    new_SW_blk.logblk.fpc = PAGE_NUM_PER_BLK; // chk if correct
    new_SW_blk.data_blk   = -1; 
    for( i = 0; i < PAGE_NUM_PER_BLK; i++) {
        new_SW_blk.logblk.lpn[i] = -1;        // -1: no data written
        new_SW_blk.logblk.lpn_status[i] = 0;  // 0: free
    }
    return new_SW_blk;
}

struct LogMap get_SW_blk_from_PMT()
{
      if(global_SW_blk.logblk.pbn == -1) {
          global_SW_blk = get_new_SW_blk();
          PMT[0] = global_SW_blk.logblk;
      }
      return PMT[0];   //first entry in PMT always tells about SW_Blk
}

/*********************************************
switch, partial, full merge operations
*******************************************/

// for sequential
void merge_switch(int log_pbn, int data_pbn)
{
    //1. search & update pointers
    //2. update BMT
    int i;
    for(i = 0; i< total_blk_num; i++){
      if( BMT[i] == data_pbn ){
        BMT[i] = log_pbn;
        break;
      }
    }

    ASSERT(i != total_blk_num);

    //3. erase (data_pbn)
    nand_erase(data_pbn, 1);
}

void merge_partial(int log_pbn, int data_pbn, int fpc, int req_lsn)
{
    //1. copy valid pages from data_pbn to log_pbn  
    int i,j,k,h,m;
    int lpn;
    int s_psn, s_lsn;
    int sect_index = 0;
    int valid_sect_num;
    int start = PAGE_NUM_PER_BLK - fpc;

    int invalid_flag,valid_flag;

    int copy[SECT_NUM_PER_PAGE];
    memset(copy, 0xFF, sizeof copy);

    for(j=0; j < total_blk_num; j++) {
        if(BMT[j] == data_pbn) {
          break;
        }
    }

    ASSERT(j != total_blk_num);

    lpn = j*PAGE_NUM_PER_BLK;  

    for (i = start; i < PAGE_NUM_PER_BLK; i++) 
    {
        s_lsn = (lpn+i) * SECT_NUM_PER_PAGE;  
        for (m = 0; m < SECT_NUM_PER_PAGE; m++) {
            copy[m] = s_lsn + m;
        }

          valid_flag = nand_oob_read( SECTOR(data_pbn, i * SECT_NUM_PER_PAGE));
          if(valid_flag == 1)
          {

            if(s_lsn != req_lsn){

            valid_sect_num = nand_page_read( SECTOR(data_pbn, i * SECT_NUM_PER_PAGE), copy, 1, 1);
            nand_page_write(SECTOR(log_pbn, i*SECT_NUM_PER_PAGE), copy, 1, 1);
            }
            else{
              nand_page_write(SECTOR(log_pbn, i*SECT_NUM_PER_PAGE), copy, 0, 1);
            }

          }
          else if( valid_flag == -1) 
          {
              invalid_flag = 0;
              for( j = 0; j < total_log_blk_num && invalid_flag != 1; j++) {  
                for( k = 0; k < PAGE_NUM_PER_BLK;k++)  {
                    if(PMT[j].lpn[k] == (lpn+i)) {
                      //invalidate in log block

                      PMT[j].lpn_status[k] = -1;    // -1: invalid
                      
                      s_psn = ((PMT[j].pbn * PAGE_NUM_PER_BLK + k) * SECT_NUM_PER_PAGE);
                      s_lsn = (lpn+i) * SECT_NUM_PER_PAGE;
                      
                      // copy the page in log block into new data block  
                      valid_sect_num = nand_page_read(s_psn, copy, 1, 1);

                      if(s_lsn != req_lsn){
                        nand_page_write(SECTOR(log_pbn, i*SECT_NUM_PER_PAGE), copy, 1, 1);
                      }
                      else{
                        nand_page_write(SECTOR(log_pbn, i*SECT_NUM_PER_PAGE), copy, 0, 1);
                      }

                      // invalidate the page in log block
                      for(h = 0; h<SECT_NUM_PER_PAGE; h++){
                        nand_invalidate(s_psn + h, s_lsn + h);
                      }
                      nand_stat(OOB_WRITE);

                      invalid_flag = 1;
                      break;
                    }
                }
              }
          }
          else 
          {
              if(s_lsn == req_lsn){
                nand_page_write(SECTOR(log_pbn, i*SECT_NUM_PER_PAGE), copy, 0, 1);
              }
          }
    }

    //2. update BMT
    for(i = 0; i<total_blk_num; i++){
      if( BMT[i] == data_pbn ){
        BMT[i] = log_pbn;
        break;
      }
    }

    ASSERT(i != total_blk_num);
    //3. erase (data_pbn)
    nand_erase(data_pbn, 1);
}

void merge_full_SW(int req_lsn)
{
    int i,h;
    int s_lsn, s_psn, s_psn1, lpn, valid_flag = 0;
    int new_pbn,pbn,lbn = -1;
    sect_t lsns[SECT_NUM_PER_PAGE];
    
    merge_full_num++;
    
    PMT[0] = global_SW_blk.logblk;
    pbn = global_SW_blk.data_blk;

    new_pbn = nand_get_free_blk(1);

    for(i = 0; i<total_blk_num; i++){
      if( BMT[i] ==  pbn){
        lbn = i;
        break;
      }
    }

    BMT[lbn] = new_pbn;
    ASSERT( lbn != -1);
              
    for( i =0 ; i < PAGE_NUM_PER_BLK; i++) {
      
      if(PMT[0].lpn_status[i] == -1){   // -1: invalid, 0: free, 1: valid
          ASSERT(0);
          continue;
      }
      else if(PMT[0].lpn_status[i] == 1) { 
        lpn = (lbn * PAGE_NUM_PER_BLK) + i;
        s_lsn = lpn * SECT_NUM_PER_PAGE;
        s_psn = SECTOR(new_pbn,i* SECT_NUM_PER_PAGE);    
        memset (lsns, 0xFF, sizeof (lsns));

        for (h = 0; h < SECT_NUM_PER_PAGE; h++) {
           lsns[h] = s_lsn + h;
        }
              
        if(req_lsn == s_lsn) {
            nand_page_write(s_psn, lsns, 0, 1);
        }
        else{
            s_psn1 = (global_SW_blk.logblk.pbn * PAGE_NUM_PER_BLK + i) * SECT_NUM_PER_PAGE; 
            nand_page_read(s_psn1, lsns, 1, 1); // read from log block
            nand_page_write(s_psn, lsns, 1, 1);
        }
                  
      }
      else {
        lpn = (lbn * PAGE_NUM_PER_BLK) + i;
        s_lsn = lpn * SECT_NUM_PER_PAGE;
        s_psn = SECTOR(new_pbn,i* SECT_NUM_PER_PAGE);    
        s_psn1 = (pbn * PAGE_NUM_PER_BLK + i) * SECT_NUM_PER_PAGE; 
        for (h = 0; h < SECT_NUM_PER_PAGE; h++) {
           lsns[h] = s_lsn + h;
        }
        valid_flag = nand_oob_read(s_psn1);
        if( valid_flag == 1){ // read from data block
          nand_page_read(s_psn1,lsns,1, 1);
          nand_page_write(s_psn,lsns,1,1);
        }
      }
    }

    nand_erase(pbn, 1);
    nand_erase(global_SW_blk.logblk.pbn, 1);
}

void merge_full(int pmt_index)  
{
    int i,j,k,m,h;
    int size;
    int old_pbn;

    int lbn,lpn,new_pbn,pbn,offset, invalid_flag;
    int s_lsn, s_psn;
    sect_t lsns[SECT_NUM_PER_PAGE];


    if(PMT[pmt_index].fpc != 0 && pmt_index == 0) {
        printf("something sucks");
        ASSERT(0);
    }


    // Check with all page in a log block 
    for(i = 0; i<PAGE_NUM_PER_BLK; i++)
    {
      if(PMT[pmt_index].lpn_status[i] != 1){   // -1: invalid, 0: free, 1: valid
        continue;
      }
      else{
          offset  = PMT[pmt_index].lpn[i] % PAGE_NUM_PER_BLK;
          lbn     = PMT[pmt_index].lpn[i] / PAGE_NUM_PER_BLK;
          old_pbn = BMT[lbn];

          if(old_pbn == global_SW_blk.data_blk) {
                  merge_partial(global_SW_blk.logblk.pbn, global_SW_blk.data_blk, global_SW_blk.logblk.fpc,-1);
                  merge_partial_num++;
              
                  global_SW_blk.logblk.pbn = nand_get_free_blk(1);
                  global_SW_blk.logblk.fpc = PAGE_NUM_PER_BLK; 
                  global_SW_blk.data_blk   = -1; 
                  for( h = 0; h < PAGE_NUM_PER_BLK; h++) {
                    global_SW_blk.logblk.lpn[h] = -1;        // -1: no data written
                    global_SW_blk.logblk.lpn_status[h] = 0;  // 0: free
                  }
                  PMT[0] = global_SW_blk.logblk;         // insert new SW_blk info into PMT
                  continue;

          }
          new_pbn = nand_get_free_blk(1);
          BMT[lbn] = new_pbn;
          merge_full_num++;

          for(j =0 ; j < PAGE_NUM_PER_BLK ; j++) {
  
                lpn = (lbn * PAGE_NUM_PER_BLK) + j;
                /* for nand_oob_read */

                s_psn = SECTOR(old_pbn, j*SECT_NUM_PER_PAGE);   // chk if correct
                s_lsn = lpn * SECT_NUM_PER_PAGE;
                memset (lsns, 0xFF, sizeof (lsns));
  
                for (h = 0; h < SECT_NUM_PER_PAGE; h++) {
                  lsns[h] = s_lsn + h;
                }

                size = nand_oob_read(s_psn);
  
                if(size == 1) // valid -> invalidate page in the data block 
                {
                    // invalidate page in data block
                    s_psn = SECTOR(old_pbn, j*SECT_NUM_PER_PAGE);   // chk if correct
                    s_lsn = lpn * SECT_NUM_PER_PAGE;


                    // read from data block - youkim
                    memset (lsns, 0xFF, sizeof (lsns));
                    for (h = 0; h< SECT_NUM_PER_PAGE; h++) 
                    {
                       lsns[h] = s_lsn + h;
                    }

                    nand_page_read(s_psn, lsns, 1, 1);

                    // invalidate page in data block
                    for(h = 0; h<SECT_NUM_PER_PAGE; h++){
                        nand_invalidate(s_psn + h, s_lsn + h);
                    } 
                    nand_stat(OOB_WRITE);

                    // write into new pbn 
                    s_psn = SECTOR(new_pbn,j* SECT_NUM_PER_PAGE);    
                    s_lsn = lpn * SECT_NUM_PER_PAGE;
          
                    memset (lsns, 0xFF, sizeof (lsns));
    
                    for (h = 0; h< SECT_NUM_PER_PAGE; h++) 
                    {
                      lsns[h] = s_lsn + h;
                    }
                    nand_page_write(s_psn, lsns, 1, 1);
                }
                else if(size == -1)
                {
              
                  invalid_flag = 0;

                  for( k = 1; (k < total_log_blk_num) && (invalid_flag != 1); k++){
                    for( m = 0; m < PAGE_NUM_PER_BLK; m++){
                       if((PMT[k].lpn[m] == lpn) && (PMT[k].lpn_status[m] == 1)) {
                          // invalidate page in log block 
                          PMT[k].lpn_status[m] = -1;    // -1: invalid
                      
                          s_psn = ((PMT[k].pbn * PAGE_NUM_PER_BLK + m) * SECT_NUM_PER_PAGE);
                          s_lsn = lpn * SECT_NUM_PER_PAGE;

                          // read from data block - youkim
                          memset (lsns, 0xFF, sizeof (lsns));
                          for (h = 0; h< SECT_NUM_PER_PAGE; h++) 
                          {
                              lsns[h] = s_lsn + h;
                          }
                          nand_page_read(s_psn, lsns, 1, 1);

                          // invalidate
                          for(h = 0; h<SECT_NUM_PER_PAGE; h++){
                            nand_invalidate(s_psn + h, s_lsn + h);
                          }
                          nand_stat(OOB_WRITE);
                          invalid_flag = 1;
                          break;
                      }
                    }
                  }

              
                  // write into new pbn 
                  s_psn = SECTOR(new_pbn,j* SECT_NUM_PER_PAGE);    
                  s_lsn = lpn * SECT_NUM_PER_PAGE;
          
                  memset (lsns, 0xFF, sizeof (lsns));
    
                  for (h = 0; h< SECT_NUM_PER_PAGE; h++) 
                  {
                    lsns[h] = s_lsn + h;
                  }
                  nand_page_write(s_psn, lsns, 1, 1);
                }
                else{

                }
        }
        // erase the data block 
        if(old_pbn == PMT[0].pbn){
            printf("1. something sucks");
            ASSERT(0);
        }
        nand_erase(old_pbn, 1);
      }
    }
    // erase the log block
    nand_erase(PMT[pmt_index].pbn, 1);
      
    free_RW_blk_num++;
}

int getRWblk()
{
    if(PMT[global_currRWblk].fpc == 0) {
      global_currRWblk++;
      free_RW_blk_num --;
    
      if(free_RW_blk_num == 0)
      {
          return -1;
      }
    }
    
    if( PMT[global_currRWblk].pbn == -1) {
        PMT[global_currRWblk].pbn = nand_get_free_blk(1);
        if(PMT[global_currRWblk].pbn == -1){
          printf("shouldn't happen");
          ASSERT(0);
        }
    }

    return (global_currRWblk);
}

int getFirstRWblk()
{
    if(global_firstRWblk == (total_log_blk_num - 1))
    {
      global_firstRWblk = 0;
    }
    global_firstRWblk++;

    return global_firstRWblk;
}

int getLastlpnfromPMT()
{
    return (PMT[0].lpn[(PAGE_NUM_PER_BLK - PMT[0].fpc -1)]) ;
}

int missing_cnt = 0;
size_t writeToLogBlock(sect_t lsn, int lbn, int lpn)
{
     int page_offset;  
     int data_pbn;
     int currRWblk;
     int currRWpageoffset;
     int firstRWblk;
     int invalid_flag;
     int s_psn, s_lsn;
     int pbn;
     int i, j, k;
     sect_t lsns[SECT_NUM_PER_PAGE];
     int last_lpn;
     
     data_pbn = getPbnFromBMT(lbn);           
     
     page_offset = lpn % PAGE_NUM_PER_BLK;   //PAGE_NUM_PER_BLK = 64

     if(global_SW_blk.logblk.pbn == -1 ) {
          global_SW_blk.logblk.pbn = nand_get_free_blk(1);
          global_SW_blk.logblk.fpc = PAGE_NUM_PER_BLK; 
          global_SW_blk.data_blk   = -1; 
          for( i = 0; i < PAGE_NUM_PER_BLK; i++) {
              global_SW_blk.logblk.lpn[i] = -1;        // -1: no data written
              global_SW_blk.logblk.lpn_status[i] = 0;  // 0: free
          }
     }
     PMT[0] = global_SW_blk.logblk;


     if(page_offset == 0 )  {  //starting page of a block

          if ( global_SW_blk.logblk.fpc == PAGE_NUM_PER_BLK) {    // SW logblock is empty 

            //directly do write on SW block because data block has been written already ! 
            ASSERT(global_SW_blk.data_blk == -1);

          }
          else {

              // completely sequentially written -> switch merge
              if(global_SW_blk.logblk.fpc == 0)  {  // no free pages in SW_BLk
                  merge_switch(global_SW_blk.logblk.pbn, global_SW_blk.data_blk);
                  merge_switch_num++;
              }

              // partially sequentially written -> partial merge
              else {

                  merge_partial(global_SW_blk.logblk.pbn, global_SW_blk.data_blk, global_SW_blk.logblk.fpc,lpn*SECT_NUM_PER_PAGE);
                  merge_partial_num++;
              }

              //allocate new SW_blk and initialize it
              global_SW_blk.logblk.pbn = nand_get_free_blk(1);
              global_SW_blk.logblk.fpc = PAGE_NUM_PER_BLK; 
              global_SW_blk.data_blk   = -1; 
              for( i = 0; i < PAGE_NUM_PER_BLK; i++) {
                  global_SW_blk.logblk.lpn[i] = -1;        // -1: no data written
                  global_SW_blk.logblk.lpn_status[i] = 0;  // 0: free
              }
              PMT[0] = global_SW_blk.logblk;         // insert new SW_blk info into PMT

          }

          ASSERT(BMT[lbn] != -1);

          // invalidate page in data block
          s_psn = ((BMT[lbn] * PAGE_NUM_PER_BLK) * SECT_NUM_PER_PAGE);
          s_lsn = lpn * SECT_NUM_PER_PAGE;

          for(i = 0; i<SECT_NUM_PER_PAGE; i++){
              nand_invalidate(s_psn + i, s_lsn + i);
          } 
          nand_stat(OOB_WRITE);
      
          // write page in SW_blk
          pbn = global_SW_blk.logblk.pbn;
          global_SW_blk.data_blk = BMT[lbn]; 
          global_SW_blk.logblk.fpc--;
          global_SW_blk.logblk.lpn[page_offset] = lpn; //store lpn of the request 
          global_SW_blk.logblk.lpn_status[page_offset] = 1; // 1: valid
          PMT[0] = global_SW_blk.logblk;

          s_psn = SECTOR(pbn, 0);    
          s_lsn = lpn * SECT_NUM_PER_PAGE;
          
          memset (lsns, 0xFF, sizeof (lsns));
    
          for (i = 0; i < SECT_NUM_PER_PAGE; i++) 
          {
            lsns[i] = s_lsn + i;
          }
       
          nand_page_write(s_psn, lsns, 0, 1);
    }
    else {

      // when lbn is the "owner" of SW log block  
      if( BMT[lbn] == global_SW_blk.data_blk) {
          last_lpn = getLastlpnfromPMT();

          //sequential writing
          if(lpn == (last_lpn+1) ) {
          
              // write page
              pbn = global_SW_blk.logblk.pbn;
              s_psn = SECTOR(pbn,(PAGE_NUM_PER_BLK - global_SW_blk.logblk.fpc)* SECT_NUM_PER_PAGE );    
              s_lsn = lpn * SECT_NUM_PER_PAGE;
            
              memset (lsns, 0xFF, sizeof (lsns));
            
              for (i = 0; i < SECT_NUM_PER_PAGE; i++) 
              {
                  lsns[i] = s_lsn + i;
              }
        
              nand_page_write(s_psn, lsns, 0, 1);

              global_SW_blk.logblk.fpc--;
              global_SW_blk.logblk.lpn[page_offset] = lpn; //store lpn of the request 
              global_SW_blk.logblk.lpn_status[page_offset] = 1; // 1: valid
              PMT[0] = global_SW_blk.logblk;
              
              // invalidate page in data block
  
              // invalidate page in log block if some data written in some log block
              invalid_flag = 0;

              for( i = 1; (i < total_log_blk_num) && (invalid_flag != 1); i++){
                for( j = 0; j < PAGE_NUM_PER_BLK; j++){
                   if((PMT[i].lpn[j] == lpn) && (PMT[i].lpn_status[j] == 1)) {
                    // invalidate
                      PMT[i].lpn_status[j] = -1;    // -1: invalid
                    
                      s_psn = ((PMT[i].pbn * PAGE_NUM_PER_BLK + j) * SECT_NUM_PER_PAGE);
                      s_lsn = lpn * SECT_NUM_PER_PAGE;

                      for(k = 0; k<SECT_NUM_PER_PAGE; k++){
                        nand_invalidate(s_psn + k, s_lsn + k);
                      }
                      nand_stat(OOB_WRITE);
                      invalid_flag = 1;
                      break;
                  }
                }
            }
          
            if(invalid_flag == 0 ) {
    
            // invalidate page in data block

              s_psn = ((BMT[lbn] * PAGE_NUM_PER_BLK + page_offset) * SECT_NUM_PER_PAGE);
              s_lsn = lpn * SECT_NUM_PER_PAGE;


              for(i = 0; i<SECT_NUM_PER_PAGE; i++){
                nand_invalidate(s_psn + i, s_lsn + i);
              } 
              nand_stat(OOB_WRITE);
            }
          }
          //random writing
          else {

              if( lpn <= (last_lpn)) {
                  merge_full_SW(lpn * SECT_NUM_PER_PAGE);
                  missing_cnt++;
              }    
              else {
                  // Note that during partial merge, new write will be taken care of 
                  merge_partial(global_SW_blk.logblk.pbn, global_SW_blk.data_blk, global_SW_blk.logblk.fpc,lpn*SECT_NUM_PER_PAGE);
                  merge_partial_num++;
              } 
          
              global_SW_blk.logblk.pbn = nand_get_free_blk(1);
              global_SW_blk.logblk.fpc = PAGE_NUM_PER_BLK; 
              global_SW_blk.data_blk   = -1; 
              for( i = 0; i < PAGE_NUM_PER_BLK; i++) {
                   global_SW_blk.logblk.lpn[i] = -1;        // -1: no data written
                   global_SW_blk.logblk.lpn_status[i] = 0;  // 0: free
              }
              PMT[0] = global_SW_blk.logblk;         // insert new SW_blk info into PMT

          }
      }
      else {

          // invalidate page in log block if some data written in some log block
          invalid_flag = 0;

          for( i = 1; (i < total_log_blk_num) && (invalid_flag != 1); i++){
              for( j = 0; j < PAGE_NUM_PER_BLK; j++){
                 if((PMT[i].lpn[j] == lpn) && (PMT[i].lpn_status[j] == 1)) {
                    // invalidate
                    PMT[i].lpn_status[j] = -1;    // -1: invalid
                    
                    s_psn = ((PMT[i].pbn * PAGE_NUM_PER_BLK + j) * SECT_NUM_PER_PAGE);
                    s_lsn = lpn * SECT_NUM_PER_PAGE;

                    for(k = 0; k<SECT_NUM_PER_PAGE; k++){
                      nand_invalidate(s_psn + k, s_lsn + k);
                    }
                    nand_stat(OOB_WRITE);
                    invalid_flag = 1;
                    break;
                 }
              }
          }
          
          if(invalid_flag == 0 ) {
    
            // invalidate page in data block
            s_psn = ((BMT[lbn] * PAGE_NUM_PER_BLK + page_offset) * SECT_NUM_PER_PAGE);
            s_lsn = lpn * SECT_NUM_PER_PAGE;

            for(i = 0; i<SECT_NUM_PER_PAGE; i++){
                nand_invalidate(s_psn + i, s_lsn + i);
            } 
            nand_stat(OOB_WRITE);
          }

          currRWblk = getRWblk();

          // no available RW log block
          if(currRWblk == -1){
            firstRWblk = getFirstRWblk();
          
            merge_full(firstRWblk);

            //initialize
            PMT[firstRWblk].pbn = nand_get_free_blk(1);
            PMT[firstRWblk].fpc = PAGE_NUM_PER_BLK;
            memset(PMT[firstRWblk].lpn, 0xFF, sizeof(int)*PAGE_NUM_PER_BLK);
            memset(PMT[firstRWblk].lpn_status, 0x00, sizeof(int)*PAGE_NUM_PER_BLK);

            global_currRWblk = firstRWblk; 
            currRWblk = firstRWblk;
          }


          currRWpageoffset = PAGE_NUM_PER_BLK - PMT[currRWblk].fpc; 

          // write page
          pbn = PMT[currRWblk].pbn;
          s_psn = SECTOR(pbn, currRWpageoffset * SECT_NUM_PER_PAGE );    
          s_lsn = lpn * SECT_NUM_PER_PAGE;
            
          PMT[currRWblk].lpn[currRWpageoffset] = lpn;
          PMT[currRWblk].lpn_status[currRWpageoffset] = 1;  // 1: valid

          memset (lsns, 0xFF, sizeof (lsns));
            
          for (i = 0; i < SECT_NUM_PER_PAGE; i++) 
          {
            lsns[i] = s_lsn + i;
          }

          nand_page_write(s_psn, lsns, 0, 1);
          PMT[currRWblk].fpc--;
      }
    }
}

int getPbnFromBMT(int lbn)
{
      if ( BMT[lbn] == -1 ) {
          BMT[lbn] = nand_get_free_blk(1);
      }
      return BMT[lbn];
}

size_t lm_write(sect_t lsn, sect_t size, int map_flag)  
{
  int lbn; int lpn; int offset;  //  logical page number
  int pbn; int sect_num = SECT_NUM_PER_PAGE;
  int s_psn, s_lsn; int i;
  sect_t lsns[SECT_NUM_PER_PAGE];

  lbn = lsn / SECT_NUM_PER_BLK;                             
  lpn = lsn/SECT_NUM_PER_PAGE;					
  offset = (lsn % SECT_NUM_PER_BLK);    

  pbn = getPbnFromBMT(lbn);     

  memset (lsns, 0xFF, sizeof (lsns));

  s_psn = SECTOR(pbn, offset);   
  s_lsn = lpn * SECT_NUM_PER_PAGE;

  for (i = 0; i < SECT_NUM_PER_PAGE; i++) {
      lsns[i] = s_lsn + i;
  }
  size = nand_oob_read(s_psn);

  /* valid_flag = 1 -> valid sect num = 4 valid_flag = -1 -> valid sect num = 0;
     valid_flag = 0 -> all are free (nothing written) */  
  if( size  != 0 ) {
     // Call writeToLogBlock 
      memset (lsns, 0xFF, sizeof (lsns));
      writeToLogBlock(lsn,lbn,lpn);
  }
  else {
      nand_page_write(s_psn, lsns, 0, 1);
  }
  return sect_num;
}

/********************* END **********************************/ 
void lm_end()
{
  printf("switch_merge  : %d\n", merge_switch_num);
  printf("partial_merge : %d\n", merge_partial_num);
  printf("full_merge    : %d\n", merge_full_num);

  if ((BMT != NULL) || (PMT != NULL)) {
    free(BMT);
    free(PMT);
  }
}

/********************* INIT **********************************/ 
int lm_init(blk_t blk_num, blk_t extra_num)   
{
  int i;

  total_blk_num = blk_num;
  BMT = (int *)malloc(sizeof(int) * blk_num);
  PMT = (struct LogMap*)malloc(sizeof(struct LogMap)*extra_num);
  total_log_blk_num = extra_num;

  if ((BMT== NULL) || (PMT == NULL)) { return -1; }

  memset(BMT, -1, sizeof(int) * blk_num);
  for(i = 0; i < total_log_blk_num; i++){
    PMT[i].pbn = -1;
    PMT[i].fpc = PAGE_NUM_PER_BLK;
    memset(PMT[i].lpn, 0xFF, sizeof(int)*PAGE_NUM_PER_BLK);
    memset(PMT[i].lpn_status, 0x00, sizeof(int)*PAGE_NUM_PER_BLK);
  }

  free_SW_blk_num = 1;
  free_RW_blk_num = (total_log_blk_num - free_SW_blk_num);

  global_SW_blk.logblk.pbn = -1;

  return 0;
}

/********************* INIT **********************************/ 
struct ftl_operation lm_operation = {
  init:  lm_init,
  read:  lm_read,
  write: lm_write,
  end:   lm_end
};
  
struct ftl_operation * lm_setup()
{
  return &lm_operation;
}
