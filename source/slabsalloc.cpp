#include "slabsalloc.h"

//allocates memore and stores the object. Also handles eviction incase the class is full.
void * SlabsAlloc::store(size_t sz, Header *& evictedObject) {

    Header *h;

    if(sz<=0)
        return nullptr;

    /*size will contain the closest base 16 size that needs to be alocated.*/
    int i = getSizeClass(sz);
    size_t size = getSizeFromClass(i); 

    printf("store called with size %d",sz); 

    // acquire the lock for the size class in question
    // (assuming that malloc is thread-safe, which it should be)
    std::lock_guard<std::recursive_mutex> lock(slabLock[i]);
    
    
    /* If this malloc will push the memory usage over the limit OR exceeds the class limit,
     * perform an eviction before storing
     */
    if(sz+allocated >= MAX_ALLOC || AllocatedCount[i]>=CLASS_LIM )
    {
        if(head_AllocatedObjects[i] == nullptr)
        {
            TRACE_ERROR("size bucket is empty, but heap is full. I quit.");
            return nullptr;
        }
        // use appropriate cache replacement algorithm
        
        // if LRU, evict from Head (Which points to the oldest object according to our ordering)
        if (algorithm == LRU)
        {
            
            printf("Entered LRU Cache Replacement\n");
            printf("Object being removed %s\n",head_AllocatedObjects[i]->key );

            Stats::Instance().evictions++;

            evictedObject = head_AllocatedObjects[i];
             
            remove((void *) head_AllocatedObjects[i]);

        }

        // else if RANDOM, evict from random location
        else if(algorithm == RANDOM)
        {
            TRACE_DEBUG("Entered RANDOM Algorithm" );
            std::uniform_int_distribution<int> dis(0, AllocatedCount[i]);
            std::mt19937 gen(rd());
            rndNum = dis(gen);
            TRACE_DEBUG("The random number generated is : ",rndNum ); 
            Header * tempObject = head_AllocatedObjects[i];
            while(rndNum!=0)
            {
                tempObject = tempObject->next;
                rndNum--;

            }
            printf("Object being removed %s\n",tempObject->key );

            Stats::Instance().evictions++;
            evictedObject = tempObject;
            remove((void *)tempObject);  

        }
        
        // else if LANDLORD, reduce credit and evict based on least credit
        else if(algorithm == LANDLORD)
        {

           
            Header *temp;
            bool flag=true;

            while(flag)
            {   
                
                temp = head_AllocatedObjects[i];
                uint16_t delta= temp->landlordCost;

                while((temp=temp->next)!=nullptr)
                {
                    if(temp->landlordCost < delta)
                    {
                        delta = temp->landlordCost; // Not considering size[f] from algorithm 
                                                    // because our design involves eviction 
                                                    // from a singl size class. Size should
                                                    // not be a game changer. 
                    }

                }
                temp = head_AllocatedObjects[i];
                
                do
                {
                    temp->landlordCost = temp->landlordCost - delta;
                }while((temp=temp->next)!=nullptr);
                
                temp = head_AllocatedObjects[i];

                do
                {
                    if(temp->landlordCost<=0)
                    {
                        evictedObject = temp;
                        remove((void*)temp);

                        flag=false;
                        break;
                    }
                }while((temp=temp->next)!=nullptr);
            }

            Stats::Instance().evictions++;

        }
       
    }

    // Check in freedObjects 
    if(freedObjects[i]!=nullptr)
    {
        h = freedObjects[i];
        freedObjects[i]=freedObjects[i]->prev;
        if(freedObjects[i]!=nullptr)
        {
            freedObjects[i]->next = nullptr;
        }

        if(tail_AllocatedObjects[i] != nullptr)
            tail_AllocatedObjects[i]->next = h;
        h->prev = tail_AllocatedObjects[i];
        h->next = nullptr;
        tail_AllocatedObjects[i]=h;
        if(head_AllocatedObjects[i]==nullptr)
            head_AllocatedObjects[i] = h;
        

        AllocatedCount[i]++;
        Stats::Instance().total_items++;
        Stats::Instance().bytes = Stats::Instance().bytes + size;

        allocated += size+sizeof(Header);
        printf("allocated:%lu",allocated);

        return h;
    }
    //If this is the first object to be allocated in that size class.
    else if(head_AllocatedObjects[i] == nullptr )
    {
        size_t size_to_malloc = size+sizeof(Header);
        void* ptr = malloc(size_to_malloc);

        if(ptr!=nullptr)
        {
            h = (Header *) ptr;  
            allocated+=size_to_malloc;
        }
        else
        {
            return nullptr;
        }

        head_AllocatedObjects[i] = h;
        tail_AllocatedObjects[i] = head_AllocatedObjects[i];

        h->prev = nullptr;
        h->next = nullptr;

        AllocatedCount[i]++;
        
        Stats::Instance().curr_items++;
        Stats::Instance().total_items++;
        Stats::Instance().bytes = Stats::Instance().bytes + size_to_malloc;

        return tail_AllocatedObjects[i];

    }
    //If freedobjects are not available, allocate using malloc. 
    else
    {
        size_t size_to_malloc = size+sizeof(Header);
        void* ptr = malloc(size_to_malloc);

        if(ptr!=nullptr)
        {
            h = (Header *) ptr;  
            allocated+=size_to_malloc;
        }
        else
        {
            return nullptr;
        }

        if(head_AllocatedObjects[i]==tail_AllocatedObjects[i])
            head_AllocatedObjects[i]->next = h;
        else
            tail_AllocatedObjects[i]->next = h;
        
        h->prev = tail_AllocatedObjects[i];
        h->next = nullptr;

        tail_AllocatedObjects[i]=h;

        AllocatedCount[i]++;
       
        Stats::Instance().curr_items++;
        Stats::Instance().total_items++;
        Stats::Instance().bytes = Stats::Instance().bytes + size_to_malloc;


        return tail_AllocatedObjects[i];
    }
    return nullptr;

}

//remove objects and add to freed objects
void SlabsAlloc::remove(void * ptr) {

    if (ptr == NULL)
    {
        return;
    }

    Header *h;
    h = (Header *)ptr;

    int i = getSizeClass(h->data_size);
    size_t size = h->data_size+sizeof(Header);
    // acquire the lock for the size class in question
    // (assuming that malloc is thread-safe, which it should be)
    std::lock_guard<std::recursive_mutex> lock(slabLock[i]);

    if(h->prev!=nullptr)
        (h->prev)->next = h->next;

    if(h->next!=nullptr)
        (h->next)->prev = h->prev;

    if(freedObjects[i]!=nullptr)
        freedObjects[i]->next = h;

    if(h==tail_AllocatedObjects[i])
        tail_AllocatedObjects[i]=h->prev;

    if(h==head_AllocatedObjects[i])
        head_AllocatedObjects[i]=h->next;

    h->prev = freedObjects[i];
    freedObjects[i] = h;
    h->next= nullptr; 
    allocated -= getSizeFromClass(i)+sizeof(Header);

    AllocatedCount[i]--;
    Stats::Instance().curr_items--;
    Stats::Instance().bytes = Stats::Instance().bytes - size;
    

}

//Updates with data or rearanges objects in a manner that will help eviction. 
void SlabsAlloc::cacheReplacementUpdates(Header* h)
{
    if(algorithm == LRU)
    {
        if(h!=nullptr)
        {
        updateRecentlyUsed(h);
        }
    }
    else if(algorithm == RANDOM)
    {
        ; //do nothing
    }
    else if (algorithm == LANDLORD)
    {
        //SlabsAlloc::missTable.insert{Key,time(null)};
        //modify credit of the file
        double cost = difftime(time(NULL), h->insertedTimestamp);
        h->landlordCost = (h->landlordCost + cost)/2;
        
    }
}

/*Rearanges objects by pushing most recently used objects to the tail so that
 *heads always contain the objects that are meant for eviction in LRU.
 */
void SlabsAlloc::updateRecentlyUsed(Header* h)
{
    Header* temp;
    int i = getSizeClass(h->data_size);
    temp = h;

    if(tail_AllocatedObjects[i]!=temp)
    {

        if(temp->prev!=nullptr)
            (temp->prev)->next = temp->next;
        
        if(temp->next!=nullptr)
            (temp->next)->prev = temp->prev;

        if(head_AllocatedObjects[i]==temp)
            head_AllocatedObjects[i]=head_AllocatedObjects[i]->next;

        tail_AllocatedObjects[i]->next=temp;
        temp->prev = tail_AllocatedObjects[i];
        tail_AllocatedObjects[i] = temp;
        temp->next = nullptr;
    }
}


// number of bytes currently allocated  
size_t SlabsAlloc::bytesAllocated() {
    return allocated; 
}

//Gives the max size of data that can be stored in that class.
size_t SlabsAlloc::getSizeFromClass(int index) {

    return (size_t)(pow(2,index));

}

//Gives the index of the slab class.
int SlabsAlloc::getSizeClass(size_t sz) {
    if (sz < 1) {
        return 0;
    }
    return (int)(ceil(log2(sz)));

}

//Used for flush_all
Header* SlabsAlloc::getFirstObject(int i)
{
    return head_AllocatedObjects[i];
}