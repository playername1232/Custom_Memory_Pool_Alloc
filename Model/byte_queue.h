#pragma once

typedef struct byte_queue
{
    unsigned char* MemoryBlockPtr = nullptr;
    unsigned int AllocatedSize = 0;
    unsigned int Size = 0;
    bool bIs_Active = false;

    bool operator==(const byte_queue& queue) const
    {
        return (this->MemoryBlockPtr == queue.MemoryBlockPtr &&
                this->AllocatedSize == queue.AllocatedSize &&
                this->Size == queue.Size &&
                this->bIs_Active == queue.bIs_Active);
    }
    
} byt_queue;