
#include "tinyos.h"
#include "kernel_sched.h"
#include "kernel_proc.h"
#include "util.h"
#include "kernel_cc.h"
#include "kernel_streams.h"

void start_thread(){

  int exitval;

  TCB* curr_thread = cur_thread();

  Task call = curr_thread->ptcb->task;
  int argl = curr_thread->ptcb->argl;
  void* args = curr_thread->ptcb->args;

  exitval = call(argl,args);
  sys_ThreadExit(exitval);
}

/** 
  @brief Create a new thread in the current process.
  Parameters:
    @param task to be executed
    @param lenght of arguments
    @param array of arguments

  Returns:
    @return the tid(ptcb) of the new thread
    @return NOTHREAD if task is null
  */
Tid_t sys_CreateThread(Task task, int argl, void* args)
{
  if(task == NULL){
    return NOTHREAD;
  }

  TCB* new_tcb = spawn_thread(CURPROC, start_thread);  //initialize TCB

  CURPROC->thread_count++;//increment thread count of current process

  // create ptcb for the thread
  PTCB* new_ptcb = (PTCB*)xmalloc(sizeof(PTCB));

  //make the connections between tcb, pcb and ptcb
  new_ptcb->tcb = new_tcb;
  new_tcb->ptcb = new_ptcb;


  // Initialize PTCB:
  new_ptcb->task = task; // task for the new thread.
  new_ptcb->refcount = 0;
  new_ptcb->exited = 0; // not exited.
  new_ptcb->detached = 0; // not detached.
  new_ptcb->exit_cv = COND_INIT; // initialize the CV

  // Copy the arguments to the ptcb's arguments
  new_ptcb->argl = argl;
  new_ptcb->args = args;

  //initialize the rlnode of ptcb
  rlnode_init(&(new_ptcb->ptcb_list_node),new_ptcb);

  //push the ptcb to the process list
  rlist_push_back(&(CURPROC->ptcb_list), & (new_ptcb->ptcb_list_node));

  //add this thread to the scheduler's list
  wakeup(new_tcb);
  return (Tid_t) new_ptcb;
}


/**
  @brief Return the Tid of the current thread.
 */
Tid_t sys_ThreadSelf()
{
	return (Tid_t) cur_thread()->ptcb;
}

/**
  @brief Join the given thread.
  Parameters:
    @param the tid(ptcb) of the thread to join
    @param the address to store the exit value.
  */
int sys_ThreadJoin(Tid_t tid, int* exitval)
{


  TCB* invoker_thread = cur_thread(); // the invoked thread 
  PTCB* invoker_ptcb = invoker_thread->ptcb; // the invoked thread ptcb 
  PTCB* invoked_ptcb= (PTCB*) tid; // The thread to join


  // if the thread to join to is not in the same process or does not exist
  if(rlist_find(&(CURPROC->ptcb_list), invoked_ptcb, NULL) == NULL){
    //fprintf(stderr, "There is no thread with the given tid in this process.");
    return -1;
  }

  // if the invoker thread tried to join itself
  if(invoker_ptcb == invoked_ptcb){
    //fprintf(stderr, "The given tid corresponds to the current thread.");
    return -1;
  }

  // if the thread to join to is detached
  if(invoked_ptcb->detached == 1){
    //fprintf(stderr, "The tid corresponds to a detached thread.");
    return -1;
  }

  // if the thread to join to is exited
 // if(invoked_ptcb->exited == 1){
    //fprintf(stderr, "The tid corresponds to an exited thread.");
   // return -1;
  //}

  // update the refcount as a thread waits to join this one
  invoked_ptcb->refcount++;

  /*sleep until the other thread exits or becomes detached
   use while loop in case there is a false wake up call */ 
  while(invoked_ptcb->detached == 0 && invoked_ptcb->exited == 0){
    kernel_wait(&invoked_ptcb->exit_cv, SCHED_USER); 
  }

  // the current thread was awoken (this might take some time)
  //decrease the refcount we have one less waiting thread
  invoked_ptcb->refcount--;

  // if the other thread became detached join has failed so return -1
  if(invoked_ptcb->detached == 1){
    //fprintf(stderr, "Thread to join became detached so ThreadJoin has failed.");
    return -1;
  }


  // store the exit value of the joined thread 
  if(exitval != NULL)
    *exitval = invoked_ptcb->exitval;
  
  if(invoked_ptcb->refcount == 0){
    // rlnode remove ptcb from CURPROC list
    rlist_remove(&invoked_ptcb->ptcb_list_node);
    free(invoked_ptcb); // invoked thread exited so we free the ptcb
  }
	return 0;
}

/**
  @brief Detach the given thread.
  */
int sys_ThreadDetach(Tid_t tid)
{

	PTCB* thread_to_detach = (PTCB*) tid; // the thread to detach

  // if the given tid is null return 
  if(thread_to_detach == NULL){
    return -1;
  }

  // if there is no thread with the given tid in this process
  if(rlist_find(&(CURPROC->ptcb_list), thread_to_detach, NULL) == NULL){
    return -1;
  }
  
  // if the thread to detach is exited 
  if(thread_to_detach->exited == 1){ 
    return -1;
  }
  

  // make the thread detached
  thread_to_detach->detached = 1;

  // wake up all the waiting threads because the thread became detached
  kernel_broadcast(&thread_to_detach->exit_cv);

  // all went well so return 0
  return 0;
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
  if(curr_ptcb->refcount > 0)//if there are threads who joined this one wake them up
    kernel_broadcast(&(curr_ptcb->exit_cv));

  /*If thread_count is 0 here, thread to exit is the main thread!Exit the process. */
  if(curproc->thread_count == 0){

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

    if(get_pid(curproc) != 1){
    /* Put me into my parent's exited list */
    rlist_push_front(& curproc->parent->exited_list, &curproc->exited_node);
    kernel_broadcast(& curproc->parent->child_exit);
    }
  

    assert(is_rlist_empty(& curproc->children_list));
    assert(is_rlist_empty(& curproc->exited_list));

    // Removing the ptcbs from the ptcb list since there are no more threads in the process
    while(!is_rlist_empty(&curproc->ptcb_list)){
      rlnode** ptr = (rlnode**) rlist_pop_front(&curproc->ptcb_list);
      free(*ptr);
    }

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
  kernel_sleep(EXITED, SCHED_USER);

}

