
#include "tinyos.h"
#include "kernel_sched.h"
#include "kernel_proc.h"

/** 
  @brief Create a new thread in the current process.
  */
Tid_t sys_CreateThread(Task task, int argl, void* args)
{
	return NOTHREAD;
}

/**
  @brief Return the Tid of the current thread.
 */
Tid_t sys_ThreadSelf()
{
	return (Tid_t) cur_thread();
}

/**
  @brief Join the given thread.
  */
int sys_ThreadJoin(Tid_t tid, int* exitval)
{
	return -1;
}

/**
  @brief Detach the given thread.
  */
int sys_ThreadDetach(Tid_t tid)
{
	return -1;
}

/**
  @brief Terminate the current thread.
  */
void sys_ThreadExit(int exitval)
{


  TCB* curr_thread = cur_thread();//thread to exit.
  PTCB* curr_ptcb = curr_thread->ptcb; // ptcb of current thread.
  curr_ptcb->exitval = exitval;//store the exit value.
  curr_ptcb->exited = 1; //thread finished.

  PCB *curproc = CURPROC;  /* cache for efficiency */
  curproc->thread_count--;

  //wake up all the threads who join this one:
  if(curr_ptcb->refcount > 0)//if the
    kernel_broadcast(&(curr_ptcb->exit_cv));




  /*If thread_count is 0 here, thread to exit is the main thread!Exit the process. */
  if(CURPROC->thread_count == 0){


    /* First, store the exit status */
    curproc->exitval = exitval;

    /* Reparent any children of the exiting process to the 
       initial task */
    PCB* initpcb = get_pcb(1);
    while(!is_rlist_empty(& curproc->children_list)) {
      rlnode* child = rlist_pop_front(& curproc->children_list);
      child->pcb->parent = initpcb;
      rlist_push_front(& initpcb->children_list, child);
    }

    /* Add exited children to the initial task's exited list 
       and signal the initial task */
    if(!is_rlist_empty(& curproc->exited_list)) {
      rlist_append(& initpcb->exited_list, &curproc->exited_list);
      kernel_broadcast(& initpcb->child_exit);
    }

    /* Put me into my parent's exited list */
    rlist_push_front(& curproc->parent->exited_list, &curproc->exited_node);
    kernel_broadcast(& curproc->parent->child_exit);

  

    assert(is_rlist_empty(& curproc->children_list));
    assert(is_rlist_empty(& curproc->exited_list));


     /* 
      Do all the other cleanup we want here, close files etc. 
     */

    /* Release the args data */
    if(curproc->args) {
     free(curproc->args);
      curproc->args = NULL;
    }

    /* Clean up FIDT */
    for(int i=0;i<MAX_FILEID;i++) {
     if(curproc->FIDT[i] != NULL) {
       FCB_decref(curproc->FIDT[i]);
       curproc->FIDT[i] = NULL;
      }
    }

    /* Disconnect my main_thread */
    curproc->main_thread = NULL;

    /* Now, mark the process as exited. */
    curproc->pstate = ZOMBIE;

    

    
  }

  /* Bye-bye cruel world */
 // kernel_sleep(EXITED, SCHED_USER);

}

