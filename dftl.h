/* 
 * Contributors: Youngjae Kim (youkim@cse.psu.edu)
 *               Aayush Gupta (axg354@cse.psu.edu)
 *
 * In case if you have any doubts or questions, kindly write to: youkim@cse.psu.edu 
 * 
 * Description: This is a header file for dftl.c.  
 * 
 */

#include "type.h"

#define CACHE_INVALID 0      //没有被缓存
#define CACHE_VALID 1

int flash_hit;
extern int rqst_page_cnt;
int read_cache_hit;
int write_cache_hit;
int evict;
int read_cache_hit_num;
int write_cache_hit_num;
int update_reqd;
int delay_flash_update;
int save_count;
struct ftl_operation * opm_setup();

// subpagemap[] 中的元素,需要记录4个LSPN和一个PPN，总计20B
struct subpagemap_entry {
	unsigned int lspn[SUBPAGE_NUM_PER_PAGE];
    unsigned int ppn;    // LPN-PPN
};  


//pagemap[] 数组的元素,页映射表条目，以lpn为索引,只记录PPN，4B大小
struct pagemap_entry{
    unsigned int ppn;    // LPN-PPN
};

//每个映射页能够存放的条目数量，由于映射条目按照 LPN 顺序存放，所以映射页只记录 PPN
//对于 4KB 页大小，PPN 按照 4B 算，每个映射页存放 1024 个条目
#define MAP_ENTRIES_PER_PAGE  1024

//页映射表 opagemap[] 前 page_num_for_2nd_map_table 项存放的是映射页的物理位置（虚拟映射页号--物理页号）
//映射条目的数量（包括 page_num_for_2nd_map_table 个映射页对应的 vpn--ppn ，以及数据页的 LPN-PPN ）
int TOTAL_MAP_ENTRIES; 

//已缓存的条目数量
int CACHE_NUM_ENTRIES;

/*//映射表数组
sect_t opagemap_num;
struct opm_entry *opagemap;
*/

//本次请求是否是部分更新写
int dftl_update_write;



/****myFTL添加******/

//LSPN 节点 数据结构
struct lspn_node{
	_u8 lsn[SECT_NUM_PER_SUBPAGE];  //子页中每个扇区的数据是否有效
}

//数据缓存LRU链表
struct lspn_node *LRU_list;


//页映射表条目数目，以及页映射表数组
sect_t pagemap_num;
struct pagemap_entry *pagemap;

//子页
sect_t subpagemap_num;
struct subpagemap_entry *subpagemap;





