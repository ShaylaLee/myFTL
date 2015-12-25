/*
 * DiskSim Storage Subsystem Simulation Environment (Version 3.0)
 * Revision Authors: John Bucy, Greg Ganger
 * Contributors: John Griffin, Jiri Schindler, Steve Schlosser
 *
 * Copyright (c) of Carnegie Mellon University, 2001, 2002, 2003.
 *
 * This software is being provided by the copyright holders under the
 * following license. By obtaining, using and/or copying this software,
 * you agree that you have read, understood, and will comply with the
 * following terms and conditions:
 *
 * Permission to reproduce, use, and prepare derivative works of this
 * software is granted provided the copyright and "No Warranty" statements
 * are included with all reproductions and derivative works and associated
 * documentation. This software may also be redistributed without charge
 * provided that the copyright and "No Warranty" statements are included
 * in all redistributions.
 *
 * NO WARRANTY. THIS SOFTWARE IS FURNISHED ON AN "AS IS" BASIS.
 * CARNEGIE MELLON UNIVERSITY MAKES NO WARRANTIES OF ANY KIND, EITHER
 * EXPRESSED OR IMPLIED AS TO THE MATTER INCLUDING, BUT NOT LIMITED
 * TO: WARRANTY OF FITNESS FOR PURPOSE OR MERCHANTABILITY, EXCLUSIVITY
 * OF RESULTS OR RESULTS OBTAINED FROM USE OF THIS SOFTWARE. CARNEGIE
 * MELLON UNIVERSITY DOES NOT MAKE ANY WARRANTY OF ANY KIND WITH RESPECT
 * TO FREEDOM FROM PATENT, TRADEMARK, OR COPYRIGHT INFRINGEMENT.
 * COPYRIGHT HOLDERS WILL BEAR NO LIABILITY FOR ANY USE OF THIS SOFTWARE
 * OR DOCUMENTATION.
 *
 */



/*
 * DiskSim Storage Subsystem Simulation Environment (Version 2.0)
 * Revision Authors: Greg Ganger
 * Contributors: Ross Cohen, John Griffin, Steve Schlosser
 *
 * Copyright (c) of Carnegie Mellon University, 1999.
 *
 * Permission to reproduce, use, and prepare derivative works of
 * this software for internal use is granted provided the copyright
 * and "No Warranty" statements are included with all reproductions
 * and derivative works. This software may also be redistributed
 * without charge provided that the copyright and "No Warranty"
 * statements are included in all redistributions.
 *
 * NO WARRANTY. THIS SOFTWARE IS FURNISHED ON AN "AS IS" BASIS.
 * CARNEGIE MELLON UNIVERSITY MAKES NO WARRANTIES OF ANY KIND, EITHER
 * EXPRESSED OR IMPLIED AS TO THE MATTER INCLUDING, BUT NOT LIMITED
 * TO: WARRANTY OF FITNESS FOR PURPOSE OR MERCHANTABILITY, EXCLUSIVITY
 * OF RESULTS OR RESULTS OBTAINED FROM USE OF THIS SOFTWARE. CARNEGIE
 * MELLON UNIVERSITY DOES NOT MAKE ANY WARRANTY OF ANY KIND WITH RESPECT
 * TO FREEDOM FROM PATENT, TRADEMARK, OR COPYRIGHT INFRINGEMENT.
 */


#include "disksim_global.h"
//flshsim
#include "ssd_interface.h"

#define WARM 1      //在warm后，将条目的更新全部置0
#define SYN 0       //SYN=1时，使用自带工具合成trace； SYN=0时，使用外部输入的 trace

void warmFlashsynth(){

  memset(dm_table, -1, sizeof(int) * DM_MGR_SIZE);

  nand_stat_reset();
  reset_flash_stat();

  if(ftl_type == 3){
    opagemap_reset();
  }

  else if(ftl_type == 4)
  {
    write_count = 0;
    read_count = 0;
  }
}

void warmFlash(char *tname){

  FILE *fp = fopen(tname, "r");
  char buffer[80];
  double time;
  int devno, bcount, flags;
  long int blkno;
  double delay;
  int i;
  
	if (SYN == 0)
	{
	  while(fgets(buffer, sizeof(buffer), fp)){
		sscanf(buffer, "%lf %d %d %d %d\n", &time, &devno, &blkno, &bcount, &flags);
		bcount = ((blkno + bcount -1) / SECT_NUM_PER_PAGE - (blkno)/SECT_NUM_PER_PAGE + 1) * SECT_NUM_PER_PAGE;
		blkno /= SECT_NUM_PER_PAGE;
		blkno *= SECT_NUM_PER_PAGE;
	//  assert(flags == 0||flags == 1);
		//printf("the start blno is:%d\n",blkno); 
		delay = callFsim(blkno, bcount,0);   

		for(i = blkno; i<(blkno+bcount); i++){ dm_table[i] = DEV_FLASH; }
	  }
	  fclose(fp); 
	}
	else
	{
		bcount = 8;
		printf("disksim_main: cd SYN=1.\n");
		//512MB--1048576, 1047543
        //32GB--67108864, 67043320.  128GB--268435456, 268173304
		for(blkno = 0; blkno < 67043320; blkno += 8)  
			callFsim2(blkno, bcount, 0);
		for(i = 0; i<67108864; i++){ dm_table[i] = DEV_FLASH; }	
		printf("disksim_main: callFsim2 over.\n");
	}
	
  nand_stat_reset();

  if(ftl_type == 3) opagemap_reset(); 

  else if(ftl_type == 4) {
    write_count = 0; read_count = 0; }
  
} 
int main (int argc, char **argv)
{
  int i, free_blk_num;
  int len;
  void *addr;
  void *newaddr;


  if(argc == 2) {
     disksim_restore_from_checkpoint (argv[1]);
  } 
  else {
    len = 8192000 + 2048000 + ALLOCSIZE;
    addr = malloc (len);
    newaddr = (void *) (rounduptomult ((long)addr, ALLOCSIZE));
    len -= ALLOCSIZE;

    disksim = disksim_initialize_disksim_structure (newaddr, len);
    disksim_setup_disksim (argc, argv);
  }

  memset(dm_table, -1, sizeof(int)*DM_MGR_SIZE);

  if(ftl_type != -1){

    initFlash();
    reset_flash_stat();
    nand_stat_reset();
  }

 // warmFlashsynth();
  warmFlash(argv[4]);
  
  free_blk_num = 0;
  for(i = 0; i < nand_blk_num; i++)
	if( nand_blk[i].state.free == 1)
		free_blk_num++;
  printf("In disksim_main(), warmFlash is over. Simulation begins! free_blk_num: %d.\n", free_blk_num);

   if( WARM == 1)
		cache_clean_update();
  
  disksim_run_simulation ();

  disksim_cleanup_and_printstats ();

  return 0;
}
