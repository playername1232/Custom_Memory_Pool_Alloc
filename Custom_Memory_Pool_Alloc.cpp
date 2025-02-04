#include <algorithm>
#include <iostream>
#include <list>
#include <memory>
#include <signal.h>
#include "Model/byte_queue.h"

// We assume that no more than 64 will be allocated at once: 2048 / 64 = 32. This way we ensure that on default we can fit all 64 queues
#define DEFAULT_ALLOC_SIZE  32
#define MAX_QUEUE_COUNT     64
#define MEMORY_ALLOC_SIZE   2048

byte_queue queues[64];
unsigned char data[MEMORY_ALLOC_SIZE];


/**
 * https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/signal?view=msvc-170
 * Terminates program using signal call SIGABRT - Signal Abort - Abnormal termination
 */
void on_out_of_memory()
{
    std::cout << "Program ran out of memory!";
    int _ = raise(SIGABRT);
}

/**
 * https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/signal?view=msvc-170
 * Terminates program using signal call SIGILL - Invalid instruction 
 */
void on_illegal_operation()
{
    std::cout << "Illegal operation recorded!";
    int _ = raise(SIGILL);
}

/**
 * Reorganizes given array of queues
 * @param entry Array of byte queues
 * @return Sorted byte queues by Pointer (Memory location they point to)
 */
byte_queue* reorganize_byte_queues(byte_queue entry[64])
{
    std::sort(entry, entry + MAX_QUEUE_COUNT, [](const byte_queue &q1, const byte_queue &q2)
    {
        // Sort by activity (active / inactive)
       if(q1.bIs_Active != q2.bIs_Active)
           return q1.bIs_Active > q2.bIs_Active;

        // Sort by memory location
        return q1.MemoryBlockPtr < q2.MemoryBlockPtr;
    });

    return entry;
}

/**
 * 
 * @return First active queue, nullptr if none is active
 */
byte_queue* get_first_queue()
{
    byte_queue temp[64];
    std::copy(std::begin(queues), std::end(queues), std::begin(temp));
    
    reorganize_byte_queues(temp);
    
    for (auto& queue : temp)
    {
        if(queue.bIs_Active == true)
            return &queue;
    }

    return nullptr;
}

/**
 * 
 * @param queue Target queue
 * @return First active queue located after given queue, nullptr if none other is active
 */
byte_queue* get_next_queue(const byte_queue& queue)
{
    bool foundQueue = false;

    for(auto& byte : queues)
    {
        if(foundQueue == false && byte.MemoryBlockPtr == queue.MemoryBlockPtr)
        {
            foundQueue = true;
            continue;
        }

        if(foundQueue && byte.bIs_Active == true)
            return &byte;
    }
    
    return nullptr;
}

/**
 * 
 * @return Last active queue, nullptr if none is active
 */
byte_queue* get_last_queue()
{
    byte_queue temp[64];
    std::copy(std::begin(queues), std::end(queues), std::begin(temp));

    reorganize_byte_queues(temp);
    
    for(int i = MAX_QUEUE_COUNT - 1; i >= 0; i--)
    {
        if(temp[i].bIs_Active == true)
        {
            byte_queue* it = std::find(std::begin(queues), std::end(queues), temp[i]);
        
            // Check if the element was found and calculate the index
            if (it != std::end(temp))
            {
                return &queues[it - queues];
            }
        }
    }

    return nullptr;
}

/**
 * 
 * @param old_location Pointer to source
 * @param location Pointer to destination
 * @param size Size / count of moved elements
 * @param clear Previous memory location is erased after move if true, otherwise no actions are done to previous location
 */
void relocate_bytes(unsigned char* old_location, unsigned char* location, unsigned int size, bool clear = false)
{
    if(location == old_location)
        return;

    std::memmove(location, old_location, size);
    if(clear == true)
    {
        for(unsigned int i = 0; i < size; i++)
        {
            old_location[i] = static_cast<unsigned char>(0x0);
        }
    }
}

/**
 * 
 * @param ptr pointer to allocated memory block
 * @param allocSize size of allocated memory
 * @returns ptr to object if byte was added, otherwise nullptr
 */
byte_queue* add_byte_queue(unsigned char* ptr, unsigned int allocSize)
{
    if(ptr == nullptr)
        return nullptr;

    // Look for an inactive queue to reuse
    for (auto& it : queues)
    {
        if(!it.bIs_Active)
        {
            // Assign the memory to this queue
            it.MemoryBlockPtr = ptr;
            it.AllocatedSize = allocSize;
            it.Size = 0;
            it.bIs_Active = true; // Mark as active
            return &it;
        }
    }

    // No inactive queue found, return nullptr
    return nullptr;
}

/** Goal of this function is to bunch all memory blocks together so there is no unused memory space between them
 * @return Returns true if memory was organized, false if memory couldn't be reorganized */
bool try_organize_memory()
{
    // Eliminate unused queues between used ones
    byte_queue temp[64];
    std::copy(std::begin(queues), std::end(queues), std::begin(temp));
    
    reorganize_byte_queues(temp);
    
    int node_idx = 0;
    bool memory_organized = false;

    if(temp[node_idx].bIs_Active == false)
        return false;

    byte_queue* previous = &temp[node_idx];
    
    if (previous->MemoryBlockPtr != data)
    {
        relocate_bytes(previous->MemoryBlockPtr, data, previous->Size, true);

        byte_queue* it = std::find(std::begin(queues), std::end(queues), *previous);
        
        // Check if the element was found and calculate the index
        if (it != std::end(temp))
        {
            queues[it - queues].MemoryBlockPtr = data;
            memory_organized = true;
        }
        previous->MemoryBlockPtr = data;
    }

    node_idx++;

    while (node_idx < MAX_QUEUE_COUNT)
    {
        byte_queue* next = &temp[node_idx];
        
        if (next->bIs_Active)
        {
            unsigned char* start = previous->MemoryBlockPtr + previous->AllocatedSize;

            if (start != next->MemoryBlockPtr)
            {
                // Reorganize / bunch up queues together to remove free space
                relocate_bytes(next->MemoryBlockPtr, start, next->AllocatedSize);
                byte_queue* it = std::find(std::begin(queues), std::end(queues), *next);
        
                // Check if the element was found and calculate the index
                if (it != std::end(temp))
                {
                    queues[it - queues].MemoryBlockPtr = start;
                    memory_organized = true;
                }
                next->MemoryBlockPtr = start;
            }
            previous = next;
        }
        
        node_idx++;
    }
    
    return memory_organized;
}

/**
 * Use only when creating new queue
 * @param requested_size Requested allocation size
 * @return Pointer to start of available memory block 
 */
unsigned char* first_free_memory(unsigned int requested_size)
{
    byte_queue* queue = get_first_queue();

    if(queue == nullptr)
    {
        // return pointer to beginning of data
        return data;
    }
    
    // Check if the beginning of the memory is allocated
    if(data != queue->MemoryBlockPtr)
    {
        // Check if there is enough space at the beginning of array
        if(data + requested_size <= queue->MemoryBlockPtr)
            return data;

        // return nullptr memory is organized and can't be organized anymore
        if(try_organize_memory() == false)
            return nullptr;

        queue = get_last_queue();
        unsigned char* mem_block_start = queue->MemoryBlockPtr + queue->AllocatedSize;
        
        if(mem_block_start + requested_size <= data + MEMORY_ALLOC_SIZE)
            return mem_block_start;

        // There is not enough space to store the queue
        return nullptr;
    }

    byte_queue* node = get_next_queue(*queue);
    if(node == nullptr)
    {
        return queue->MemoryBlockPtr + queue->AllocatedSize;
    }

    byte_queue temp[64];
    std::copy(std::begin(queues), std::end(queues), std::begin(temp));
    reorganize_byte_queues(temp);
    
    // Look for gaps between active queues
    for(int i = 1; i < MAX_QUEUE_COUNT; i++)
    {
        if(temp[i].bIs_Active == false)
            break;

        // Check for gaps that can fit the requested size
        if(temp[i].MemoryBlockPtr - (queue->MemoryBlockPtr + queue->AllocatedSize) >= requested_size)
        {
            return queue->MemoryBlockPtr + queue->AllocatedSize;
        }

        queue = &temp[i];
    }

    // No gaps found, check end of memory block
    byte_queue* last_queue = get_last_queue();
    unsigned char* ptr_to_first = last_queue->MemoryBlockPtr + last_queue->AllocatedSize;

    // Check if there is enough space at the end
    if(ptr_to_first + requested_size <= data + MEMORY_ALLOC_SIZE)
    {
        return ptr_to_first;
    }

    // Try to reorganize memory one last time
    if(!try_organize_memory())
    {
        return nullptr;
    }

    // Check end of memory block again after reorganization
    last_queue = get_last_queue();
    ptr_to_first = last_queue->MemoryBlockPtr + last_queue->AllocatedSize;

    if(ptr_to_first + requested_size <= data + MEMORY_ALLOC_SIZE)
    {
        return ptr_to_first;
    }

    return nullptr;
}

/**
 * Use only when reallocating already existing queue
 * @param queue Target queue
 * @param size Requested allocation size
 * @return Pointer to start of available memory block 
 */
unsigned char* get_available_memory_start(byte_queue &queue, unsigned int size)
{
    byte_queue* node = get_next_queue(queue);

    if(node == nullptr)
        return data;
    
    // Check if gap between queue and next allocated queue is enough to use current ptr instead of relocating
    if(node != nullptr && node->MemoryBlockPtr - queue.MemoryBlockPtr >= size)
        return queue.MemoryBlockPtr;

    // Gap to the next queue isn't large enough, therefore the queue will be relocated to the end of the byte array
    node = get_last_queue();
    if(node == nullptr)
        return data;
    
    unsigned char* memory_start = node->MemoryBlockPtr + node->AllocatedSize;
    
    
    // check if resized memory would not exceed bounds of the byte array.
    // data + MEMORY_ALLOC_SIZE = first memory block outside of bounds of the byte array
    // if memory_start + size == data + MEMORY_ALLOC_SIZE -> queue is still in range, as it ends on the very end of the byte array
    // if memory_start + size > data + MEMORY_ALLOC_SIZE -> queue is out of range
    
    if(memory_start + size > data + MEMORY_ALLOC_SIZE)
    {
        // data would exceed allocated size of memory
        if(try_organize_memory() == false)
            on_out_of_memory();
        
        node = get_last_queue();
        
        memory_start = node->MemoryBlockPtr + node->AllocatedSize;

        // check if memory reorganization has solved the issue and there's enough space at the end of memory to fit the queue
        if(memory_start + size > data + MEMORY_ALLOC_SIZE)
            on_out_of_memory();
    }
    
    return memory_start;
}

/**
 * Reserves queue in queues array
 * @return Pointer to reserved item in queues array
 * @exception on_out_of_memory is called when allocating more than 64 queues 
 */
byte_queue* create_queue()
{
    unsigned char* start = first_free_memory(DEFAULT_ALLOC_SIZE);
    if(start == nullptr)
        on_out_of_memory();

    byte_queue* result = add_byte_queue(start, DEFAULT_ALLOC_SIZE);
    if(result == nullptr)
        on_out_of_memory();

    result->MemoryBlockPtr = start;
    result->AllocatedSize = DEFAULT_ALLOC_SIZE;
    result->Size = 0;
    result->bIs_Active = true;

    return result;
}

/**
 * 
 * @param queue Target queue
 * @param clear Erases memory handled by queue if true, otherwise no action is done
 */
void destroy_queue(byte_queue* queue, bool clear = false)
{
    if(clear == true)
    {
        for(unsigned int i = 0; i < queue->AllocatedSize; i++)
        {
            queue->MemoryBlockPtr[i] = 0x0;
        }
    }

    // mark queue as inactive, therefore its previous content can be overwritten
    queue->MemoryBlockPtr = nullptr;
    queue->AllocatedSize = 0;
    queue->Size = 0;
    queue->bIs_Active = false;
}

/**
 * 
 * @param queue Target queue
 * @param byte Inserted byte
 * @exception on_out_of_memory is called if no memory space is available to enqueue new byte
 */
void enqueue_byte(byte_queue *queue, unsigned char byte)
{
    // If queue doesn't have enough memory allocated
    if(queue->Size + 1 > queue->AllocatedSize)
    {
        unsigned char* lastPosition = queue->MemoryBlockPtr;

        unsigned int lastSize = queue->AllocatedSize;
        queue->AllocatedSize += DEFAULT_ALLOC_SIZE;
        queue->MemoryBlockPtr = get_available_memory_start(*queue, queue->AllocatedSize);

        relocate_bytes(lastPosition, queue->MemoryBlockPtr, lastSize, true);
    }

    queue->MemoryBlockPtr[queue->Size] = byte;
    queue->Size++;
}

/**
 * 
 * @param queue Target queue
 * @return Removes byte from queue using FIFO
 * @exception on_invalid_operation is called if queue size is equal to 0
 */
unsigned char dequeue_byte(byte_queue* queue)
{
    if(queue->Size == 0)
        on_illegal_operation();

    unsigned char removed_byte = queue->MemoryBlockPtr[0];
    
    for(unsigned int i = 0; i < queue->Size; i++)
    {
        queue->MemoryBlockPtr[i] = queue->MemoryBlockPtr[i + 1];
    }
    
    queue->Size--;
    queue->MemoryBlockPtr[queue->Size] = 0x0;

    if(queue->Size <= queue->AllocatedSize - DEFAULT_ALLOC_SIZE)
        queue->AllocatedSize -= DEFAULT_ALLOC_SIZE;
    
    return removed_byte;
}

void Test_SCSTest()
{
    byte_queue* q0 = create_queue();
    enqueue_byte(q0, 0);  // Queue 0: [0]
    enqueue_byte(q0, 1);  // Queue 0: [0, 1]
    byte_queue* q1 = create_queue();
    enqueue_byte(q1, 3);  // Queue 1: [3]
    enqueue_byte(q0, 2);  // Queue 0: [0, 1, 2]
    enqueue_byte(q1, 4);  // Queue 1: [3, 4]
    printf("%d ", dequeue_byte(q0)); // Expected output: 0
    printf("%d\n", dequeue_byte(q0)); // Expected output: 1
    enqueue_byte(q0, 5);  // Queue 0: [2, 5]
    enqueue_byte(q1, 6);  // Queue 1: [3, 4, 6]
    printf("%d ", dequeue_byte(q0)); // Expected output: 2
    printf("%d\n", dequeue_byte(q0)); // Expected output: 5
    destroy_queue(q0); // Destroy queue 0
    printf("%d ", dequeue_byte(q1)); // Expected output: 3
    printf("%d ", dequeue_byte(q1)); // Expected output: 4
    printf("%d\n", dequeue_byte(q1)); // Expected output: 6
    destroy_queue(q1); // Destroy queue 1
}

void Test_FillQueues()
{
    for(int i = 0; i < MAX_QUEUE_COUNT; i++)
    {
        byte_queue* temp = create_queue();

        for(int j = 1; j <= DEFAULT_ALLOC_SIZE; j++)
        {
            enqueue_byte(temp, static_cast<unsigned char>(j));
        }
    }
}

void Test_Reallocation()
{
    byte_queue* q1 = create_queue();
    for(int i = 1; i <= DEFAULT_ALLOC_SIZE; i++)
    {
        enqueue_byte(q1, static_cast<unsigned char>(i));
    }

    byte_queue* q2 = create_queue();
    for(int i = 1; i <= DEFAULT_ALLOC_SIZE; i++)
    {
        enqueue_byte(q2, static_cast<unsigned char>(i));
    }

    for(int i = 33; i <= DEFAULT_ALLOC_SIZE * 2; i++)
    {
        enqueue_byte(q1, static_cast<unsigned char>(i));
    }
}

void Test_Reallocation_2()
{
    // ----------- 1. section -----------
    // q1, q2 and q2 start with 32 elements each
    byte_queue* q1 = create_queue();
    for(int i = 1; i <= DEFAULT_ALLOC_SIZE; i++)
    {
        enqueue_byte(q1, static_cast<unsigned char>(i));
    }

    byte_queue* q2 = create_queue();
    for(int i = 1; i <= DEFAULT_ALLOC_SIZE; i++)
    {
        enqueue_byte(q2, static_cast<unsigned char>(i));
    }

    byte_queue* q3 = create_queue();
    for(int i = 1; i <= DEFAULT_ALLOC_SIZE; i++)
    {
        enqueue_byte(q3, static_cast<unsigned char>(i));
    }

    // ----------- 2. section -----------
    // q2 is destroyed to release memory
    destroy_queue(q2);

    // ----------- 3. section -----------
    // q1 allocates another 32 elements - this should test that q1 can be simply resized as there is 32 bytes free between q1 and q3

    for(int i = 33; i <= DEFAULT_ALLOC_SIZE * 2; i++)
    {
        enqueue_byte(q1, static_cast<unsigned char>(i));
    }

    // ----------- 4. section -----------
    // remove 48 bytes from q1 to free some space between q1 and q3 - enough for q2 - q1 allocation size is lowered to 32 as 32 is enough to fit 16 bytes.
    // therefore q1 won't occupy too much of memory as it doesn't need
    for(int i = 1; i <= 48; i++)
    {
        printf("Byte removed from q1: %d\n", dequeue_byte(q1));
    }

    // ----------- 5. section -----------
    // create q2 and fill it with 32 bytes
    q2 = create_queue();
    for(int i = 1; i <= DEFAULT_ALLOC_SIZE; i++)
    {
        enqueue_byte(q2, static_cast<unsigned char>(i));
    }

    // Final result:
    // q1 -> q2 -> q3
    // q1 has 16 Size (32 Alloc)
    // q2 has 32 Size (32 Alloc)
    // q3 has 32 Size (32 Alloc)
}

void Test_Reallocation_3()
{
    // ----------- 1. section -----------
    // start with q1, q2, q3, q4 with 32 elements. q5 will start with 33, therefore will be moved "Behind" q6 after adding 33rd character as 33 bytes can't fit to 32 bytes sized memory
    
    byte_queue* q1 = create_queue();
    byte_queue* q2 = create_queue();
    byte_queue* q3 = create_queue();
    byte_queue* q4 = create_queue();
    byte_queue* q5 = create_queue();

    enqueue_byte(q5, 0x0);
    
    byte_queue* q6 = create_queue();

    for(int i = 1; i <= DEFAULT_ALLOC_SIZE; i++)
    {
        enqueue_byte(q1, static_cast<unsigned char>(i));
        enqueue_byte(q2, static_cast<unsigned char>(i));
        enqueue_byte(q3, static_cast<unsigned char>(i));
        enqueue_byte(q4, static_cast<unsigned char>(i));
        enqueue_byte(q5, static_cast<unsigned char>(i));
        enqueue_byte(q6, static_cast<unsigned char>(i));
    }

    // ----------- 2. section -----------
    // Destroy q3 and q4 to release memory
    
    destroy_queue(q3);
    destroy_queue(q4);

    q3 = nullptr;
    q4 = nullptr;

    // ----------- 3. section -----------
    // create 4 new queues and fill them with 32 bytes
    // q11, q12 and q13 will be placed between q2 and q6 (q5 was reallocated after q6, therefore there are 3 * 32 bytes free)
    // q14 will be placed after q5 (because q5 is located after q6 due to its increased size to 64 bytes)

    byte_queue* q11 = create_queue();
    byte_queue* q12 = create_queue();
    byte_queue* q13 = create_queue();
    byte_queue* q14 = create_queue();
    
    for(int i = 1; i <= DEFAULT_ALLOC_SIZE; i++)
    {
        enqueue_byte(q11, static_cast<unsigned char>(i));
        enqueue_byte(q12, static_cast<unsigned char>(i));
        enqueue_byte(q13, static_cast<unsigned char>(i));
        enqueue_byte(q14, static_cast<unsigned char>(i));
    }
    
    // Final result:
    // q3 and q4 are NULL as they were destroyed. They no longer hold pointer to queues in array that are now used by other pointers
    
    // q1 -> q2 -> q11 -> q12 -> q13 -> q6 -> q5 -> q14
    // q1 has 32 Size (32 Alloc)  (Memory Location = &data)
    // q2 has 32 Size (32 Alloc)  (Memory Location = q1->MemoryBlockPtr + 32)
    // q11 has 32 Size (32 Alloc) (Memory Location = q2->MemoryBlockPtr + 32)
    // q12 has 32 Size (32 Alloc) (Memory Location = q11->MemoryBlockPtr + 32)
    // q13 has 32 Size (32 Alloc) (Memory Location = q12->MemoryBlockPtr + 32)
    // q6 has 32 Size (32 Alloc)  (Memory Location = q13->MemoryBlockPtr + 32)
    // q5 has 33 Size (64 Alloc)  (Memory Location = q6->MemoryBlockPtr + 32)
    // q14 has 32 Size (32 Alloc) (Memory Location = q5->MemoryBlockPtr + 64)
}

void Test_InvalidOperation()
{
    byte_queue* q1 = create_queue();
    dequeue_byte(q1);
}

void Test_OutOfMemory()
{
    Test_FillQueues();
    byte_queue* q1 = &queues[0];

    enqueue_byte(q1, 0x5);
}

void Test_OutOfMemory_2()
{
    Test_FillQueues();

    // Program should shut down with "Program ran out of memory!" at this stage as we are trying to allocate space for 65th queue
    byte_queue* invalidQueue = create_queue();

    enqueue_byte(invalidQueue, 0x5);
}

void Test_Reallocate_Start()
{
    byte_queue* q1 = create_queue();
    byte_queue* q2 = create_queue();

    for(int i = 1; i <= DEFAULT_ALLOC_SIZE; i++)
    {
        enqueue_byte(q1, static_cast<unsigned char>(i));
        enqueue_byte(q2, static_cast<unsigned char>(i));
    }

    destroy_queue(q1);
    byte_queue* q3 = create_queue();
    for(int i = DEFAULT_ALLOC_SIZE; i > 0; i--)
    {
        enqueue_byte(q3, static_cast<unsigned char>(i));
    }
}

void Test_Additional()
{
    Test_FillQueues();

    destroy_queue(&queues[2], true);
    destroy_queue(&queues[3], true);
    destroy_queue(&queues[5], true);

    byte_queue* first  = create_queue();
    byte_queue* second = create_queue();
    byte_queue* third  = create_queue();

    for(int i = 1; i <= DEFAULT_ALLOC_SIZE; i++)
    {
        enqueue_byte(first, static_cast<unsigned char>(i));
        enqueue_byte(second, static_cast<unsigned char>(i));
        enqueue_byte(third, static_cast<unsigned char>(i));
    }
}

void Test_Organization()
{
    byte_queue* first  = create_queue();
    byte_queue* second = create_queue();
    byte_queue* third  = create_queue();
    byte_queue* fourth = create_queue();
    byte_queue* fifth  = create_queue();
    byte_queue* sixth  = create_queue();

    for(int i = 1; i <= DEFAULT_ALLOC_SIZE; i++)
    {
        enqueue_byte(first, static_cast<unsigned char>(i));
        enqueue_byte(second, static_cast<unsigned char>(i));
        enqueue_byte(third, static_cast<unsigned char>(i));
        enqueue_byte(fourth, static_cast<unsigned char>(i));
        enqueue_byte(fifth, static_cast<unsigned char>(i));
        enqueue_byte(sixth, static_cast<unsigned char>(i));
    }

    destroy_queue(first, true);
    destroy_queue(fifth, true);
    destroy_queue(fourth, true);
    try_organize_memory();

    // Final result:
    // second -> third -> sixth
    // second has 32 Size (32 Alloc)  (Memory location = data)
    // third has 32 Size (32 Alloc)   (Memory location = second->MemoryBlockPtr + AllocSize)
    // sixth has 32 Size (32 Alloc)   (Memory location = third->MemoryBlockPtr + AllocSize)
}

int main(int argc, char* argv[])
{
    Test_Reallocation_3();
    return 0;
}
