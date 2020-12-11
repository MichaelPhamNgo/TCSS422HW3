/*
TCSS - Operating Systems
Armoni Atherton, Joshua Atherton
Running instructions:
To run do tail -fn 10 /var/log/syslog 
To remove a previously installed the module:
sudo rmmod ./procReport.ko
To install a newly built module:
sudo insmod ./procReport.ko   
cd /proc to view file
Do "make clean" before pushing to git-hub
*/

#include <linux/module.h> /* Needed by all modules */
#include <linux/kernel.h> /* Needed for KERN_INFO */
#include <linux/sched/signal.h>
#include <asm/pgtable.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Armoni Atherton, Josh Atherton");
MODULE_DESCRIPTION("Count the segmentation of the pages memorry.");
MODULE_VERSION("1.0");

/* ***** Linked list struct to count pg stats **** */ 
typedef struct _counter_list {
  unsigned long pid;
  char * name;
  unsigned long contig_pages;
  unsigned long noncontig_pages;
  unsigned long total_pages;
  struct _counter_list * next;
} counter_list;

/* ***** Prototypes ******** */
static int proc_init (void);
static unsigned long virt2phys(struct mm_struct * mm, unsigned long vpage);
static void iterate_pages(void);
static void write_to_console(void);

//proc file functions
static int proc_report_show(struct seq_file *m, void *v);
static int proc_report_open(struct inode *inode, struct  file *file);
static void proc_cleanup(void);

/* ***** Global values **** */ 
static const struct file_operations proc_report_fops = {
  .owner = THIS_MODULE,
  .open = proc_report_open,
  .read = seq_read,
  .llseek = seq_lseek,
  .release = single_release,
};
// to count total pages stats
static unsigned long total_contig_pgs = 0;
static unsigned long total_noncontig_pgs = 0;
static unsigned long total_pgs = 0;
// linked list of page stats
static counter_list *stats_list_head = NULL;

/* ***** Functions For the kernel ***************** */ 
/**
 * Inialize  and start the kernal task.
 */
static int proc_init (void) { 
  
  iterate_pages();
  write_to_console();
  proc_create("proc_report", 0, NULL, &proc_report_fops);

  return 0;
}

/**
 * Iterate through the page tables collecting stats on memory.
 */
static void iterate_pages(void) {
  struct task_struct *task;
  struct vm_area_struct *vma;
  counter_list *curr = stats_list_head;
  counter_list *node;
  unsigned long vpage;
  unsigned long prev_page_phys;
  unsigned long phys = 0;

  for_each_process(task) {

    //Check vaild process.
    if (task->pid > 650) { 
      //allocate space for a new node
      node = kmalloc(sizeof(counter_list), GFP_KERNEL);
      //update fields for a page
      node->pid = task->pid;
      node->name = task->comm;
      node->contig_pages = 0;
      node->noncontig_pages = 0;
      node->total_pages = 0;
      node->next = NULL;

      vma = 0;
      prev_page_phys = 0;
      if (task->mm && task->mm->mmap) {
        for (vma = task->mm->mmap; vma; vma = vma->vm_next) {
          //Iterates through virtual pages.
          prev_page_phys = 0;
          for (vpage = vma->vm_start; vpage < vma->vm_end; vpage += PAGE_SIZE) {
            phys = virt2phys(task->mm, vpage);
            //If memeory has been allocated.
            if ( phys != 0) {
              node->total_pages += 1;
              if ((prev_page_phys + PAGE_SIZE) - phys == 0) { // contiguous
                node->contig_pages += 1;
              } else {
                node->noncontig_pages += 1;
              }
              prev_page_phys = phys;
            }
          }
        }

        // add to total page count
        total_contig_pgs += node->contig_pages;
        total_noncontig_pgs += node->noncontig_pages;
        total_pgs += node->total_pages;
      }

      //insert node into linked list
      if(stats_list_head == NULL) {  // when linked list is empty
        stats_list_head = node;
        curr = stats_list_head;    
      } else { // Point the previous last node to the new node created.
        curr->next = node;
        curr = curr->next;
      }
    } // end if > 650
  } // end for_each
}

/**
 * Get the mapping of virtual to physical memory addresses.
 */
static unsigned long virt2phys(struct mm_struct * mm, unsigned long vpage) { 
  pgd_t *pgd;
  p4d_t *p4d;
  pud_t *pud;
  pmd_t *pmt;
  pte_t *pte;
  struct page *page;

  pgd = pgd_offset(mm, vpage);
  if (pgd_none(*pgd) || pgd_bad(*pgd))
    return 0;

  p4d = p4d_offset(pgd, vpage);
  if (p4d_none(*p4d) || p4d_bad(*p4d))
    return 0;

  pud = pud_offset(p4d, vpage);
  if (pud_none(*pud) || pud_bad(*pud))
    return 0;
   pmt = pmd_offset(pud, vpage);
  if (pmd_none(*pmt) || pmd_bad(*pmt))
    return 0;

  if (!(pte = pte_offset_map(pmt, vpage)))
    return 0;

  if (!(page = pte_page(*pte)))
    return 0;

  //unsigned long physical_page_addr = page_to_phys(page);
  pte_unmap(pte);
  return page_to_phys(page); //physical_page_addr;
}

static void write_to_console(void) {
  counter_list * item;
  item = stats_list_head;
  while (item) {
    //proc_id,proc_name,contig_pages,noncontig_pages,total_pages
    printk("%lu,%s,%lu,%lu,%lu", item->pid, item->name,
        item->contig_pages, item->noncontig_pages, item->total_pages);
    item = item->next;
  }
  // TOTALS,,contig_pages,noncontig_pages,total_pages
  printk("TOTALS,,%lu,%lu,%lu",
    total_contig_pgs, total_noncontig_pgs, total_pgs); 
}

/**
 * Finish and end the kernal task.
 */
static void proc_cleanup(void) {
  remove_proc_entry("proc_report", NULL);
}

/**
 * write 
 */
static int proc_report_show(struct seq_file *m, void *v) {

  counter_list *item = stats_list_head;
  counter_list *prev = item;
  seq_printf(m, "PROCESS REPORT: \nproc_id,proc_name,contig_pages,noncontig_pages,total_pages \n");

  while (item) {
    //proc_id,proc_name,contig_pages,noncontig_pages,total_pages
    seq_printf(m, "%lu,%s,%lu,%lu,%lu\n", item->pid, item->name,
        item->contig_pages, item->noncontig_pages, item->total_pages);
    prev = item;
    item = item->next;
    kfree(prev);
  }
  // TOTALS,,contig_pages,noncontig_pages,total_pages
  seq_printf(m, "TOTALS,,%lu,%lu,%lu\n",
    total_contig_pgs, total_noncontig_pgs, total_pgs); 

  return 0;
}

/**
 * Open the proc file
 */
static int proc_report_open(struct inode *inode, struct  file *file) {
  return single_open(file, proc_report_show, NULL);
}


/* ******** Kernal Run functions ************************ */
module_init(proc_init);
module_exit(proc_cleanup);