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

#define CACHE_INVALID 0      //û�б�����
#define CACHE_VALID 1

int flash_hit;
extern int rqst_page_cnt;
int read_cache_hit;   //ͳ�ƶ����̻�������
int write_cache_hit;   //ͳ��д���̻�������
int evict;
int read_cache_hit_num;
int write_cache_hit_num;
int update_reqd;
int delay_flash_update;
int save_count;
struct ftl_operation * mpm_setup();

// subpagemap[] �е�Ԫ��,��Ҫ��¼4��LSPN��һ��PPN���ܼ�20B
struct subpagemap_entry {
	unsigned int lspn[SUBPAGE_NUM_PER_PAGE];
    unsigned int ppn;    // LPN-PPN
};  


//pagemap[] �����Ԫ��,ҳӳ�����Ŀ����lpnΪ����,ֻ��¼PPN��4B��С
struct pagemap_entry{
	  _u32 free  : 1; 					 //����Ŀ�Ƿ��ѱ�ʹ��
		_u32 ppn	 : 31;					 //���߼�ҳ��Ӧ������ҳ�� LPN-PPN
};

//ÿ��ӳ��ҳ�ܹ���ŵ���Ŀ����������ӳ����Ŀ���� LPN ˳���ţ�����ӳ��ҳֻ��¼ PPN
//���� 4KB ҳ��С��PPN ���� 4B �㣬ÿ��ӳ��ҳ��� 1024 ����Ŀ
#define MAP_ENTRIES_PER_PAGE  1024

//ҳӳ��� opagemap[] ǰ page_num_for_2nd_map_table ���ŵ���ӳ��ҳ������λ�ã�����ӳ��ҳ��--����ҳ�ţ�
//ӳ����Ŀ������������ page_num_for_2nd_map_table ��ӳ��ҳ��Ӧ�� vpn--ppn ���Լ�����ҳ�� LPN-PPN ��
int TOTAL_MAP_ENTRIES; 

//�ѻ������Ŀ����
int CACHE_NUM_ENTRIES;

/*//ӳ�������
sect_t opagemap_num;
struct opm_entry *opagemap;
*/

//���������Ƿ��ǲ��ָ���д
int mftl_update_write;



/****myFTL���******/

//LSPN �ڵ� ���ݽṹ
struct lspn_node{
	_u32 lspn;  //�ýڵ����ҳ���
//	_u8 lsn[SECT_NUM_PER_SUBPAGE];  //��ҳ��ÿ�������������Ƿ���Ч
	_u8 dirty; 
//	byte data[SECT_SIZE_B * SECT_NUM_PER_SUBPAGE];
	struct lspn_node *next;
}

//���ݻ���LRU����,ͷָ��
struct lspn_node *LRU_list;
int LRU_cache_num;   //LRU������Ŀ����Ӵ��������С�����Ը������õĻ����С������ɿ������ɵĽڵ������������ڴ��оͿ���ֱ�Ӽ���ģ�����ݿռ䡣
int LRU_cur_num;     //LRU����Ŀǰ�Ĵ�С


//ҳӳ�����Ŀ��Ŀ���Լ�ҳӳ�������
sect_t pagemap_num;
struct pagemap_entry *pagemap;

//��ҳ
sect_t subpagemap_num;
struct subpagemap_entry *subpagemap;

//ÿ��ӳ��ҳ�ܹ���ŵ���Ŀ����������ӳ����Ŀ���� LPN ˳���ţ�����ӳ��ҳֻ��¼ PPN
//���� 16KB ҳ��С��PPN ���� 4B �㣬ÿ��ӳ��ҳ��� 1024 *4 ����Ŀ
#define MAP_ENTRIES_PER_PAGE  (SECT_SIZE_B*SECT_NUM_PER_PAGE/4)

//ֱ��ά��һ��ÿ��������ҳ��������Ч�ԡ�

struct sub_state
{
	_u8 isvalid;  //0 ��ʾ������Ч��1 ��ʾ������Ч
	_u8 islog;    //0,��ʾ����ҳ��1 ��ʾ��־ҳ

}
struct sub_state * valid_arr;







