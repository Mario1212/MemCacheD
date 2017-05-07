#ifndef SLABSALLOC_H
#define SLABSALLOC_H

#include <cassert>
#include <functional>
#include <cstdlib>
#include <cmath>
#include <iostream>
#include <new>
#include <mutex>
#include <sys/mman.h>
#include <stdint.h>
#include <time.h>
#include <random>
#include <unistd.h>

#include "Trace.h"

using namespace std;

#define CLASS_LIM   1024  //limit on number of objects per size class
#define NUM_CLASSES 23

typedef enum {
    LRU=0,
    RANDOM,
    LANDLORD
} ALG_T;

class Header {
    public:
        char key[251];
        size_t data_size;
        uint16_t flags;
        int32_t expiration_time;
        time_t expiration_timestamp;
        time_t last_updated_timestamp;
        unsigned int last_updated_client;
        int64_t cas_unique;
        bool valid;
        uint16_t landlordCost; //for landlord replacement algorithm

        Header * prev;
        Header * next;
};

struct r_usage    //used in stats class below 
{
  uint32_t seconds;
  uint32_t microseconds; 
};

class Stats
{
public:
  Stats()
  {
    ;
  }

   pid_t pid;
   int32_t uptime;
   std::string version;
   int32_t pointer_size;
   r_usage userUsage;
   r_usage systemUsage;
   uint32_t curr_items = 0;
   uint32_t total_items;
   uint64_t bytes;
   uint32_t curr_connections;
   uint32_t total_connections;
   uint32_t connection_structures;
   uint32_t reserved_fds;
   uint64_t cmd_get;
   uint64_t cmd_set;
   uint64_t cmd_flush;
   uint64_t cmd_touch;
   uint64_t get_hits;
   uint64_t get_misses;
   uint64_t delete_misses;
   uint64_t delete_hits;
   uint64_t incr_misses;
   uint64_t incr_hits;
   uint64_t decr_misses;
   uint64_t decr_hits;  
   uint64_t cas_misses; 
   uint64_t cas_hits; 
   uint64_t cas_badval;
   uint64_t touch_hits; 
   uint64_t touch_misses; 
   uint64_t auth_cmds; 
   uint64_t auth_errors; 
   uint64_t evictions;
   uint64_t reclaimed;
   uint64_t bytes_read;
   uint64_t bytes_written;
   uint32_t limit_maxbytes;
   uint32_t threads; 
   uint64_t conn_yields; 
   uint32_t hash_power_level;
   uint64_t hash_bytes;
   bool hash_is_expanding = false;
   uint64_t expired_unfetched;
   uint64_t evicted_unfetched; 



};

class SlabsAlloc {
public:
  SlabsAlloc(ALG_T algorithm, size_t max_heap_size)
    : MAX_ALLOC(max_heap_size),
      requested (0),
      allocated (0),
      maxRequested (0),
      maxAllocated (0),
      algorithm(algorithm)

  {
   // statsObject->pid = ::getpid();
   for (auto& f : freedObjects) {
      f = nullptr;
    }
    for (auto& f : head_AllocatedObjects) {
      f = nullptr;
    }
    for (auto& f : tail_AllocatedObjects) {
      f = nullptr;
    }
    for (auto& f : AllocatedCount) {
      f = 0;
    }
    TRACE_DEBUG("MAX_ALLOC=",MAX_ALLOC);


    statsObject.pid=::getpid();
       
  }

  ~SlabsAlloc()
  {
  }
  
  enum { Alignment = 16 };
  
  void * store(size_t sz);
  void remove(void * ptr);
  size_t getSize(void * p);
  void updateRecentlyUsed(Header* h);
  void cacheReplacementUpdates(Header* h);

  // number of bytes currently allocated  
  size_t bytesAllocated();
  
  // max number of bytes allocated  
  size_t maxBytesAllocated();

  // number of bytes *requested*
  // (e.g., malloc(4) might result in an allocation of 8; 4 = bytes requested, 8 = bytes allocated)
  size_t bytesRequested();
  
  // max number of bytes *requested*
  size_t maxBytesRequested();

  ALG_T algorithm;

  // Return maximum size of object for a given size class.
  static size_t getSizeFromClass(int index);
  
  // Return the size class for a given size.
  static int getSizeClass(size_t sz);

  Stats statsObject;

private:

  recursive_mutex heapLock;
  
  size_t MAX_ALLOC;
  size_t allocated;
  size_t requested;
  size_t maxAllocated;
  size_t maxRequested;
  uint16_t rndNum;
  std::random_device rd;
  
  uint16_t AllocatedCount[NUM_CLASSES];
  recursive_mutex slabLock[NUM_CLASSES];
  Header * head_AllocatedObjects[NUM_CLASSES];
  Header * tail_AllocatedObjects[NUM_CLASSES];
  Header * freedObjects[NUM_CLASSES];
  
  
};

extern "C" {
  void reportStats();
  void walk(const std::function< void(Header *) >& f);
}


#endif
