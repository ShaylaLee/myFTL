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
int read_cache_hit;
int write_cache_hit;
int evict;
int read_cache_hit_num;
int write_cache_hit_num;
int update_reqd;
int delay_flash_update;
int save_count;
struct ftl_operation * opm_setup();

//ÿ��ӳ����Ŀά����״̬�� opagemap[] �е�Ԫ�أ����� index ��ʾ�߼�ҳ��
struct opm_entry {
  _u32 free  : 1;            //����Ŀ�Ƿ��ѱ�ʹ��
  _u32 ppn   : 31;           //���߼�ҳ��Ӧ������ҳ�� LPN-PPN
  _u32 cache_status : 1;     //����Ŀ�Ƿ񱻻���
  _u32 update : 1;           //�û����Ƿ� dirty ��ǰ�����ѻ��棩
  _u32 cache_age : 30;       //����Ŀ���ȶ�
};

//mapdir[] �����Ԫ�أ�DFTL �е� GTD ����¼����ӳ��ҳ������λ��
struct omap_dir{
  unsigned int ppn;  
};

//ÿ��ӳ��ҳ�ܹ���ŵ���Ŀ����������ӳ����Ŀ���� LPN ˳���ţ�����ӳ��ҳֻ��¼ PPN
//���� 4KB ҳ��С��PPN ���� 4B �㣬ÿ��ӳ��ҳ��� 1024 ����Ŀ
#define MAP_ENTRIES_PER_PAGE  1024

//ҳӳ��� opagemap[] ǰ page_num_for_2nd_map_table ���ŵ���ӳ��ҳ������λ�ã�����ӳ��ҳ��--����ҳ�ţ�
//ӳ����Ŀ������������ page_num_for_2nd_map_table ��ӳ��ҳ��Ӧ�� vpn--ppn ���Լ�����ҳ�� LPN-PPN ��
int TOTAL_MAP_ENTRIES; 

//�ѻ������Ŀ����
int CACHE_NUM_ENTRIES;

//ӳ�������
sect_t opagemap_num;
struct opm_entry *opagemap;

//���������Ƿ��ǲ��ָ���д
int dftl_update_write;
