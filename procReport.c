/* 
 *  Phuc Pham N
 *  University of Washington, Tacoma
 *  TCSS 422 - Operating Systems
 *  Assignment 3: Linux Kernel Module - Page Table Walker
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched/signal.h>
#include <asm/pgtable.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>

/**
  * README: 
  *
  * To run this file, we need
  * - Linux kernel 5.4.0-42-generic
  * - install gcc library: sudo apt install gcc
  * 
  * To build kernel module
  * $make
  * To install a newly built module
  * $sudo insmod ./procReport.ko
  * To print message 
  * $dmesg OR $sudo tail -fn 50 /var/log/syslog
  * To remove a previously installed the module
  * $sudo rmmod ./procReport.ko
  * To see the content of /proc/proc_report
  * $sudo cat /proc/proc_report
  */

/*************** Module Descriptions ***************/
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Phuc Pham N");
MODULE_DESCRIPTION("Linux Kernel Module - Page Table Walker");
MODULE_VERSION("1.0");

/*************** Definition ***************/
#define FILE_NAME "proc_report"

/*** Definition Struct of a Processing Node ***/
typedef struct _nProcess {
  //proc_id for the process ID
  int proc_id;
  //proc_name for the process name
  char * proc_name;
  //contig_pages which is the number of contiguous pages
  int contig_pages;
  //noncontig_pages which is the number of non-contiguous pages
  int noncontig_pages;
  //total_pages which is the total number of memory pages allocated
  int total_pages;
  struct _nProcess * next;
} nProcess;

nProcess *copy_head = NULL; 

//Reference: https://www.log2base2.com/data-structures/linked-list/inserting-a-node-at-the-end-of-a-linked-list.html
/**
  * Insert new node at the end of a linked list
 **/ 
void insert(nProcess **head, nProcess *newNode) {  
  //if head is NULL, it is an empty list
  if(*head == NULL) {  // when linked list is empty
    *head = newNode;    
  } else { //Otherwise, find the last node and add the newNode
    nProcess *lastNode = *head;
    //last node's next address will be NULL.
    while(lastNode->next != NULL)
    {
      lastNode = lastNode->next;
    }
    //add the newNode at the end of the linked list
    lastNode->next = newNode;
  }
}

/**
  * Get all nodes in the linked list and print the data
 **/ 
int totalConPages = 0;
int totalNonconPages = 0;
int totalPages = 0;
void report(nProcess *head) {     
  printk("PROCESS REPORT:");
  printk("proc_id,proc_name,contig_pages,noncontig_pages,total_pages");   
  //iterate the entire linked list
  while (head != NULL) {
    //print proc_id,proc_name,contig_pages,noncontig_pages,total_pages
    printk("%d,%s,%d,%d,%d", head->proc_id, head->proc_name,
        head->contig_pages, head->noncontig_pages, head->total_pages);    
    // add to total page count
    totalConPages += head->contig_pages;
    totalNonconPages += head->noncontig_pages;
    totalPages += head->total_pages;
    head = head->next;
  }   
  //print the sum of the total number of contiguous pages, total number of non-contiguous pages, and the total number of pages
  printk("TOTALS,,%d,%d,%d", totalConPages, totalNonconPages, totalPages);     
}

//Reference: https://stackoverflow.com/questions/20868921/traversing-all-the-physical-pages-of-a-process
/*** translate a virtual address to a physical address ***/
unsigned long virt2phys(struct mm_struct * mm, unsigned long virt) {   
  pgd_t *pgd; p4d_t *p4d; pud_t *pud; pmd_t *pmd; pte_t *pte;
  struct page *page;
  unsigned long phys;

  pgd = pgd_offset(mm, virt);
  if (pgd_none(*pgd) || pgd_bad(*pgd))
    return 0;
  p4d = p4d_offset(pgd, virt);
  if (p4d_none(*p4d) || p4d_bad(*p4d))
    return 0;
  pud = pud_offset(p4d, virt);
  if (pud_none(*pud) || pud_bad(*pud))
    return 0;
  pmd = pmd_offset(pud, virt);
  if (pmd_none(*pmd) || pmd_bad(*pmd))
    return 0;  
  if (!(pte = pte_offset_map(pmd, virt)))
    return 0;  
  if (!(page = pte_page(*pte)))
    return 0;  
  phys = page_to_phys(page);
  pte_unmap(pte);
  return phys;
}

//Reference https://gist.github.com/BrotherJing/c9c5ffdc9954d998d1336711fa3a6480#file-helloproc-c-L7
/*** write a kernel module, which create a read/write proc file ***/
static int my_proc_show(struct seq_file *m,void *v){	  
	seq_printf(m, "PROCESS REPORT: \n");
  seq_printf(m, "proc_id,proc_name,contig_pages,noncontig_pages,total_pages \n");
  while (copy_head != NULL) {
    //print proc_id,proc_name,contig_pages,noncontig_pages,total_pages
    seq_printf(m,"%d,%s,%d,%d,%d\n", copy_head->proc_id, copy_head->proc_name,
        copy_head->contig_pages, copy_head->noncontig_pages, copy_head->total_pages);    
    copy_head = copy_head->next;
  }   
  seq_printf(m, "TOTALS,,%d,%d,%d\n", totalConPages, totalNonconPages, totalPages); 
	return 0;
}

static int my_proc_open(struct inode *inode,struct file *file){
  //single_open which writes the content in my_proc_show function to the /proc/proc_report file
	return single_open(file,my_proc_show,NULL);
}

//create file_operations structure my_fops in which we can map the my_proc_open functions for the proc entry
static struct file_operations my_fops={
	.owner = THIS_MODULE,
	.open = my_proc_open, //open the /proc/proc_report
	.release = single_release,
	.read = seq_read,
	.llseek = seq_lseek	
};

//Kernel module initialized
int proc_init (void) {  
  unsigned long vpage;
  unsigned long prevPhys;
  unsigned long phys = 0;  
  struct task_struct *task;  
  struct vm_area_struct *vma;
  struct proc_dir_entry *entry;
  nProcess *head = NULL; 
  nProcess *newNode;
  //Reference http://embeddedguruji.blogspot.com/2018/12/linux-kernel-driver-to-print-all.html?m=1
  //for all processes 
  for_each_process(task) {
    //if every process with PID > 650 
    if(task->pid > 650) { 
      int conPages = 0;
      int nonConPages = 0;
      int totPages = 0;
      vma = 0;
      prevPhys = 0;
      if (task->mm && task->mm->mmap) {
        for (vma = task->mm->mmap; vma; vma = vma->vm_next) {
          //Iterates through virtual pages.
          prevPhys = 0;
          for (vpage = vma->vm_start; vpage < vma->vm_end; vpage += PAGE_SIZE) {
            //a virtual address to a physical address 
            phys = virt2phys(task->mm, vpage);            
            if ( phys != 0) {
              totPages += 1;
              //If the next page in the process’s virtual memory space is mapped to 
              //the current page’s physical location plus PAGE_SIZE, 
              //record a “tick” for a contiguous mapping
              if ((prevPhys + PAGE_SIZE) - phys == 0) {
                conPages += 1;
              } else { //Else record a tick for a “non-contiguous” mapping.
                nonConPages += 1;
              }
              prevPhys = phys;
            }
          }
        }        
      }
      //create a new node 
      //allocate memory for the node
      newNode = kmalloc(sizeof(nProcess), GFP_KERNEL);  
      //assign data for the node
      newNode->proc_id = task->pid;
      newNode->proc_name = task->comm;        
      newNode->contig_pages = conPages;
      newNode->noncontig_pages = nonConPages;        
      newNode->total_pages = totPages;   
      newNode->next = NULL; 
      //insert the new node to the linked list
      insert(&head,newNode);        
    }
  }

  //allocate memory for the node
  copy_head = kmalloc(sizeof(nProcess), GFP_KERNEL);  

  *copy_head = *head;
  //print report in /var/log/syslog
  report(head); 

  //create a proc_report file in /proc directory
  entry = proc_create(FILE_NAME,0660,NULL,&my_fops);
	if(!entry){
		return -1;	
	}else{
		printk("\n");
	}
  return 0;
}

//Performing cleanup of module
void proc_cleanup(void) {   
  //delete /proc/proc_report file   
  remove_proc_entry(FILE_NAME,NULL);  
}

module_init(proc_init);
module_exit(proc_cleanup);