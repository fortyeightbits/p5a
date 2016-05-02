#include "types.h"
#include "stat.h"
#include "param.h"
#include "fcntl.h"
#include "user.h"
#include "x86.h"

int thread_create(void (*start_routine)(void*), void *arg)
{
    void* newstack = malloc(2*PGSIZE);
    if((uint)newstack % PGSIZE)
        newstack = newstack + (4096 - (uint)newstack % PGSIZE);

	int pid = clone(start_routine, arg, newstack);
	return pid;
}

int thread_join()
{
	void* tempstack;
	int pid = join(&tempstack);
	free(tempstack);
	return pid;
}

void lock_init(lock_t * padlock)
{
	padlock->locked = 0;
	//padlock->cpu = 0;	
}

void lock_acquire(lock_t * padlock)
{
    //pushcli(); // disable interrupts to avoid deadlock.
   // if(holding(padlock))
     // panic("acquire");

    while(xchg(&padlock->locked, 1) != 0)
      ;

    // Record info about lock acquisition for debugging.
   // padlock->cpu = cpu;
   // getcallerpcs(&padlock, padlock->pcs);
}

void lock_release(lock_t * padlock)
{
  //  if(!holding(padlock))
     // panic("release");

    //padlock->pcs[0] = 0;
    //padlock->cpu = 0;

    xchg(&padlock->locked, 0);

    //popcli();
}

/*
// Check whether this cpu is holding the lock.
int
holding(lock_t *lock)
{
  return lock->locked && lock->cpu == cpu;
}*/
