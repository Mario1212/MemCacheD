#include "memo.h"

SlabsAlloc* alloc;

namespace  Memo
{
    std::unordered_map<std::string, Header*> Table;
    recursive_mutex TableLock;

    void update_Expiration_Timestamp(Header* h, int32_t expiration_time)
    {
        if (expiration_time < 0) {
            h->expiration_timestamp = std::numeric_limits<time_t>::min();
        }
        else if (expiration_time == 0){
            h->expiration_timestamp = 0;
        }
        else if (expiration_time >  SecondsIn30Days) {
            h->expiration_timestamp = expiration_time;
        }
        else {
            h->expiration_timestamp = time(NULL) + expiration_time;
        }
    }

    bool validExpirationTime(Header* h) {
        if (h->expiration_timestamp == 0 || h->expiration_timestamp >= time(NULL)) {
            return true;
        }
        return false;
    }

    Header* get(std::string key, bool isStatsChanged)
    {
        if (isStatsChanged)
        {   
            // Increment stats only if get called by handle_get. Calls from other functions are internal
            Stats::Instance().cmd_get++;
            
        }

        TableLock.lock();

        std::unordered_map<std::string,Header*>::const_iterator got = Table.find (key);
        if ( got != Table.end() )
        {
            if (validExpirationTime(got->second)) {
                alloc->cacheReplacementUpdates((Header *) got->second);
                // it is a get hit
                if (isStatsChanged) {
                    // Increment stats only if get called by handle_get. Calls from other functions are internal

                    Stats::Instance().get_hits++;
                }

                TableLock.unlock();
                return got->second;
            }
            else
            {
                Table.erase({key});
                TRACE_DEBUG("Key expired");
                TableLock.unlock();
                return nullptr;
            }
        }
        else
        {
            //it is a get miss.
            if (isStatsChanged)
            {
                // Increment stats only if get called by handle_get. Calls from other functions are internal

                Stats::Instance().get_misses++;
            }
        }
        TableLock.unlock();
        return nullptr;
    }

    RESPONSE set(std::string key, uint16_t flags, int32_t expiration_time, size_t size, std::string value, bool cas)
    {
        Header* h;
        TRACE_DEBUG("called ",__FUNCTION__);

        //Stats::Instance().cmd_set++;

        h=get(key);
        if (h == nullptr) {
            if(cas)
            {
                return NOT_FOUND;
            }
            return(add(key, flags, expiration_time, size, value));
        }
        else {

            return(replace(key, flags, expiration_time, size, value, cas));
        }

        
    }

    RESPONSE add(std::string key, uint16_t flags, int32_t expiration_time, size_t size, std::string value, bool updateExpirationTime)
    {
        Header* h;
        Header *evictedObject;
        evictedObject = NULL;
        char* temp;
        TRACE_DEBUG("called ",__FUNCTION__);

        Stats::Instance().cmd_set++;

        h=get(key);

        if(h==nullptr)//if value not present in hash table already, allocate memory and update header. 
        {
            //add header information

            TRACE_DEBUG("add called");

            h = (Header*) alloc->store(size, evictedObject);
            if(evictedObject!=NULL)
            {   
                TableLock.lock();
                Table.erase({evictedObject->key});
                TableLock.unlock();
            }
            if(h!=nullptr)
            {
                std::strncpy(h->key, key.c_str(), 251);
                h->flags = flags;
                if (updateExpirationTime) {
                    update_Expiration_Timestamp(h, expiration_time);
                }
                else {
                    h->expiration_timestamp = expiration_time;
                }
                h->data_size = size;
                temp = (char*) (h+1);
                std::strncpy(temp,value.c_str(),size+1);
                h->insertedTimestamp = time(NULL);

                std::thread::id this_id = std::this_thread::get_id();
                h->last_updated_client = this_id;
                h->landlordCost = 0;

                TRACE_DEBUG("Thread id: ",h->last_updated_client);

                TRACE_DEBUG("adding ",key.c_str());

                TableLock.lock();
                Table.insert({key,h});
                TableLock.unlock();
                return STORED;
            }
        }
        return NOT_STORED;
    }

    RESPONSE replace(std::string key, uint16_t flags, int32_t expiration_time, size_t size, std::string value, bool cas, bool updateExpirationTime)
    {
        Header* h;
        char* temp;
        TRACE_DEBUG("called ",__FUNCTION__);

        Stats::Instance().cmd_set++;

        h=get(key);
        //TRACE_DEBUG("%p",h);
        if(cas)
        {
            if (h->last_updated_client != std::this_thread::get_id())
            {
                return EXISTS;
            }
        }

        if(h!=nullptr)
        {
            if(alloc->getSizeClass(h->data_size)==alloc->getSizeClass(size))
            {   
                h->flags = flags;
                if (updateExpirationTime) {
                    update_Expiration_Timestamp(h, expiration_time);
                }
                else {
                    h->expiration_timestamp = expiration_time;
                }
                h->data_size = size;
                temp = (char*) (h+1);
                TRACE_DEBUG(temp);
                std::strncpy(temp,value.c_str(),size+1);
                TRACE_DEBUG(": replaced with :",temp);
                h->last_updated_client = std::this_thread::get_id();

                return STORED;
            }
            else
            {   TRACE_DEBUG("different size");
                alloc->remove((void*)h);
                TableLock.lock();
                Table.erase({key});
                TableLock.unlock();
                return(add(std::string(key),flags,expiration_time,size,std::string(value),updateExpirationTime));
            }
        }
        else
        {
            return NOT_STORED;
        }
    }

    RESPONSE append(std::string key, size_t size, std::string value) {

        Header* h;
        char* temp;
        int16_t temp_flags;
        int32_t temp_expiration_time; 

        TRACE_DEBUG("called ",__FUNCTION__);

        Stats::Instance().cmd_set++;

        h = get(key);

        if(h==nullptr)
        {
            return NOT_STORED;
        }
        else if(alloc->getSizeClass(h->data_size)==alloc->getSizeClass(h->data_size + size))
        {
            temp = (char*) (h+1);
            std::strncat(temp,value.c_str(), size+1);
            h->data_size = h->data_size + size;
            h->last_updated_client = std::this_thread::get_id();
            return STORED;
        }
        else {
            temp = (char *) (h + 1);
            std::strncat(temp, value.c_str(), size + 1);
            size = h->data_size + size;
            temp_flags = h->flags;
            temp_expiration_time = h->expiration_time;
            alloc->remove((void *) h);
            TableLock.lock();
            Table.erase({key});
            TableLock.unlock();
            return (add(key, temp_flags, temp_expiration_time, size, std::string(temp)));
        }
    }

    RESPONSE prepend(std::string key, size_t size, std::string value) {
        Header* h;
        char* data;
        int16_t temp_flags;
        int32_t temp_expiration_time;

        TRACE_DEBUG("called ",__FUNCTION__);

        Stats::Instance().cmd_set++;

        h = get(key);

        if(h==nullptr)
        {
            return NOT_STORED;
        }
        else if(alloc->getSizeClass(h->data_size)==alloc->getSizeClass(h->data_size + size))
        {

            TRACE_DEBUG("same size class");
            data = (char*) (h+1);
            std::string temp = value + std::string(data);
            std::strncpy(data, temp.c_str(), std::strlen(temp.c_str())+1);
            h->data_size = h->data_size + size;
            h->last_updated_client = std::this_thread::get_id();
            return STORED;
        }
        else
        {


            TRACE_DEBUG("different size class");
            data = (char*) (h+1);

            std::string temp = value + std::string(data);
            size = h->data_size + size;
            temp_flags = h->flags;
            temp_expiration_time = h->expiration_time;

            alloc->remove((void*)h);
            TableLock.lock();
            Table.erase({key});
            TableLock.unlock();

            return(add(key,temp_flags,temp_expiration_time,size,temp));

        }

    }

    RESPONSE mem_delete(std::string key) {
        // delete code
        Header* h;
        TRACE_DEBUG("called ",__FUNCTION__);

        h = get(key);

        if(h!=nullptr)
        {
            alloc->remove((void*)h);
            TableLock.lock();
            Table.erase({key});
            TableLock.unlock();
            return DELETED;
        }
        else
        {       
            return NOT_FOUND;
        }
    }

    Header* incr(std::string key, std::string value) {
        
        Header* h;
        TRACE_DEBUG("called ",__FUNCTION__);

        h = get(key);
        char* temp;
        long unsigned int num;

        if(h==nullptr)
        {
            //incr on missing keys. update stats 
            Stats::Instance().incr_misses++;
            return nullptr;
        }
        else
        {
            //incr hit. update stats 
            Stats::Instance().incr_hits++;
            temp = (char*) (h+1);
            TRACE_DEBUG("value=",temp);
            try
            {
                num = strtol(temp, NULL,10);
            }
            catch(std::exception& e)
            {
                return nullptr;
            }
            num += std::strtol(value.c_str(), NULL,10);
            TRACE_DEBUG("incremented:",num);

            std::string num_str = std::to_string(num);
            RESPONSE res = replace(key, h->flags, h->expiration_time, std::strlen(num_str.c_str()), num_str, false, false);
            if (res == STORED) {
                return get(key);
            }
            return nullptr;
        }
    }

    Header* decr(std::string key, std::string value) {
        Header* h;
        TRACE_DEBUG("called ",__FUNCTION__);

        h = get(key);
        char* temp;
        long unsigned int num;

        if(h==nullptr)
        {
            // it is decr miss. update stats
            Stats::Instance().decr_misses++;
            return nullptr;
        }
        else
        {
            //decr hit. update stats
            Stats::Instance().decr_hits++;
            
            temp = (char*) (h+1);
            TRACE_DEBUG("value=",temp);
            try
            {
                num = std::strtol(temp, NULL,10);
            }
            catch(std::exception& e)
            {
                return nullptr;
            }
            int64_t signed_num = (int64_t )num - strtol(value.c_str(), NULL,10);
            if (signed_num < 0) {
                num = 0;
            }
            else {
                num -= strtol(value.c_str(), NULL,10);
            }
            TRACE_DEBUG("decremented:",num);

            std::string num_str = std::to_string(num);
            RESPONSE res = replace(key, h->flags, h->expiration_time, std::strlen(num_str.c_str()), num_str, false, false);
            if (res == STORED) {
                return get(key);
            }
            return nullptr;

        }
    }

    void stats(char*& response_str, size_t* response_len) {
        // stats code
        TRACE_DEBUG("Called Stats function " );
        Stats::Instance().printStats(response_str,response_len);

    }

    void flush_all(int32_t exptime) {
        // expire all objects after exptime

        Header* temp;
        int i;

        for(i=0;i<NUM_CLASSES;i++)
        {
            temp = alloc->getFirstObject(i);

            if(temp!=nullptr)
            {
                if(exptime==0)
                {
                    update_Expiration_Timestamp(temp,-1);
                }
                else
                {
                    update_Expiration_Timestamp(temp,exptime);
                }

            }
        }


        Stats::Instance().cmd_flush++;

    }

}
