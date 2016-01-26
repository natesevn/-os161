/*
 * Driver code for airballoon problem
 */
#include <types.h>
#include <lib.h>
#include <thread.h>
#include <synch.h>
#include <test.h>

#define NROPES 16
static int ropes_left = NROPES;

// Data structures for rope mappings
int ropes_to_stakes[NROPES];

// Synchronization primitives
struct semaphore rope_sem; 
struct lock rope_locks[NROPES];
struct semaphore done_sem;

/*
 * Describe your design and any invariants or locking protocols 
 * that must be maintained. Explain the exit conditions. How
 * do all threads know when they are done?  
 */

/*
 * Design:
 *  data structure: an array of ropes (integers)
 *      - the indices represent the rope and balloon hook number
 *          (note that these are the same because Lord FlowerKiller
 *          never changes the ropes on the balloon hook's side.
 *      - the values in the array represent the stakes that the
 *          ropes are attached to.
 *
 *  synchronization primitives:
 *      - semaphore rope_sem: a semaphore used by the main function 
 *          to keep track of the four threads (dandelion, marigold, 
 *          balloon, flowerkiller).
 *      - lock rope_locks[NROPES]: an array of locks to protect
 *          the ropes.
 *      - semaphore done_sem: a semaphore used by the balloon thread
 *          to keep track of the other three threads (dandelion,
 *          marigold, flowerkiller). It is just used to make sure
 *          that the statement "Balloon freed and Prince Dandelion
 *          escapes!" is printed after the other threads have finished
 *          their respective print statements. 
 * 
 *  locking protocols and invariants:
 *      - whenever a thread does something with a rope, it has to
 *          acquire the rope's lock first. After it's done, it releases
 *          the lock and decrements the ropes_left variable.
 *      - threads know they are done if the ropes_left variable reaches
            0, signifying that all ropes have been successfully severed.
 *      - after the dandelion, marigold, and flowerkiller threads have
 *          finished, they call V on the done_sem semaphore, which is
 *          being P'd by the balloon thread. Once the balloon thread
 *          finishes P'ing the done_sem semaphore, it prints out the
 *          "Balloon freed and Prince Dandelion escapes!" message.
 *      - after all four (dandelion, marigold, balloon, flowerkiller)
 *          threads have finished, they call V on the rope_sem semaphore,
 *          which the main function is P'ing on. The problem is "solved"
 *          once the main function has succeessfully called P on
 *          all four threads.          
 */

static
void
dandelion(void *p, unsigned long arg)
{
    // Start the thread
    kprintf("Dandelion thread starting\n");
    
    while(ropes_left > 0) {
        // Generate a random balloon_hook_index
        int balloon_hook_index = random() % NROPES;

        // Acquire the rope_lock and sever the rope
        // Then release the rope_lock
        bool rope_severed = false;

        lock_acquire(&rope_locks[balloon_hook_index]);
        if(ropes_to_stakes[balloon_hook_index] != -1) {
            kprintf("Dandelion severed rope %d\n", balloon_hook_index);
            ropes_to_stakes[balloon_hook_index] = -1;
            rope_severed = true;
            ropes_left--;
        }
        lock_release(&rope_locks[balloon_hook_index]);

        // Yield the thread if we have successfully severed a rope
        if(rope_severed == true) {
            thread_yield();
        }
    }

    // Finish up the thread and increment the semaphores
    kprintf("Dandelion thread done\n");
    V(&rope_sem);
    V(&done_sem);
    (void)p;
    (void)arg;
}

static
void
marigold(void *p, unsigned long arg)
{
    // Start the thread
    kprintf("Marigold thread starting\n");
    
    while(ropes_left > 0) {
        // Generate a random ground_stake_index
        int ground_stake_index = random() % NROPES;
        
        // Iterate through the ropes_to_stakes mapping
        // and search for a matching stake
        bool rope_severed = false;
        int i = 0;
        for(i = 0; i < NROPES; i++) {
            if(ropes_to_stakes[i] == ground_stake_index) {
                lock_acquire(&rope_locks[i]);

                // Double check if the rope has been cut
                // while we were acquiring the rope lock
                if(ropes_to_stakes[i] != ground_stake_index) {
                    lock_release(&rope_locks[i]);
                    break;
                } else {
                    // Cut the rope
                    kprintf("Marigold severed rope %d from stake %d\n",
                        i, ground_stake_index);
                    ropes_to_stakes[i] = -1;
                    rope_severed = true;
                    ropes_left--;
                    lock_release(&rope_locks[i]);
                }
            };
        }
        
        // Yield the thread if we have successfully severed a rope
        if(rope_severed == true) {
            thread_yield();
        }
    }    
    
    // Finish up the thread and increment the semaphores
    kprintf("Marigold thread done\n");
    V(&rope_sem);
    V(&done_sem);
    (void)p;
    (void)arg;
}

static
void
flowerkiller(void *p, unsigned long arg)
{
    kprintf("Lord FlowerKiller thread starting\n");
    
    while(ropes_left > 0) {
        // Generate random stakes to switch from and to
        int stake_from = random() % NROPES;
        int stake_to = random() % NROPES;
        
        // Iterate through the ropes_to_stakes mapping
        // and search for a matching stake
        bool rope_moved = false;
        int i = 0;
        for(i = 0; i < NROPES; i++) {
            if(ropes_to_stakes[i] == stake_from) {
                lock_acquire(&rope_locks[i]);
                
                // Double check if the rope has been cut
                // while we were acquiring the rope lock
                if(ropes_to_stakes[i] != stake_from) {
                    lock_release(&rope_locks[i]);
                    break;
                } else {
                    // Move the rope from stake_from to stake_to
                    kprintf("Lord FlowerKiller switched rope %d from" 
                       " stake %d to stake %d\n", i, stake_from, stake_to);
                    ropes_to_stakes[i] = stake_to;
                    rope_moved = true;
                    lock_release(&rope_locks[i]);
                }
            }
        }
        
        // Yield the thread if we have successfully severed a rope
        if(rope_moved == true) {
            thread_yield();
        }
    }

    // Finish up the thread and increment the semaphores 
    kprintf("Lord FlowerKiller thread done\n");
    V(&rope_sem);
    V(&done_sem);
    (void)p;
    (void)arg;
}

static
void
balloon(void *p, unsigned long arg)
{
    kprintf("Balloon thread starting\n");
   
    // Making use of a semaphore to make sure that the escape
    // statement is printed after the other threads finish
    int num_threads = 3;
    for(num_threads = 3; num_threads > 0; num_threads--) {
        P(&done_sem);
    }
    
    // Finish up the thread and increment the semaphore
    kprintf("Balloon thread done\n");
    kprintf("Balloon freed and Prince Dandelion escapes!\n");
    V(&rope_sem);
    (void)p;
    (void)arg;
}


// Change this function as necessary
int
airballoon(int nargs, char **args)
{
    int err = 0;
    (void)nargs;
    (void)args;

    // Initialize ropes_left, mappings, and locks
    ropes_left = 16;
    int i = 0;
    for(i = 0; i < NROPES; i++) {
        ropes_to_stakes[i] = i;
        rope_locks[i] = *lock_create("rope_lock");
    }
    
    // Create the semaphores and fork the threads
    done_sem = *sem_create("done_sem", 0);
    rope_sem = *sem_create("rope_sem", 0);

    err = thread_fork("Marigold Thread",
              NULL, marigold, NULL, 0);
    if(err)
        goto panic;
    
    err = thread_fork("Dandelion Thread",
              NULL, dandelion, NULL, 0);
    if(err)
        goto panic;
    
    err = thread_fork("Lord FlowerKiller Thread",
              NULL, flowerkiller, NULL, 0);
    if(err)
        goto panic;

    err = thread_fork("Air Balloon",
              NULL, balloon, NULL, 0);
    if(err)
        goto panic;

    // Wait until the threads are finished
    int num_threads = 4;
    for(num_threads = 4; num_threads > 0; num_threads--) {
        P(&rope_sem);
    }
    kprintf("Main thread done\n");

    goto done;
panic:
    panic("airballoon: thread_fork failed: %s)\n",
          strerror(err));
    
done:
    return 0;
}
