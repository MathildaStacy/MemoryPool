#include <memory>
#include <vector>
#include <iostream>
#include <numeric>
#include <cassert>
#include <complex>
#include <array>
#include <memory>
#include <chrono>
#include <format>

template<typename T, typename Allocator = std::allocator<T>>
class ObjectPool {
public:
    ObjectPool() = default;
    explicit ObjectPool(const Allocator& allocator);
    virtual ~ObjectPool();

    //允许移动运算
    ObjectPool(ObjectPool&& src) noexcept = default;
    ObjectPool& operator=(ObjectPool&& rhs) noexcept = default;

    //不允许拷贝构造和赋值运算符
    ObjectPool(const ObjectPool& src) = delete;
    ObjectPool& operator=(const ObjectPool& rhs) = delete;

    //从对象池中获取对象 Reserves and returns an object from the pool. Arguments can be
    //provided which are perfectly forwarded to a constructor of T.
    template<typename... Args>
    std::shared_ptr<T> acquireObject(Args... args);

private:

    std::vector<T*> m_pool;
    std::vector<T*> m_freeObjects;

    static const size_t ms_initialChunkSize { 5 };

    size_t m_newChunkSize { ms_initialChunkSize };

    void addChunk();

    Allocator m_allocator;
};

template <typename T, typename Allocator>
ObjectPool<T, Allocator>::ObjectPool(const Allocator& allocator)
    : m_allocator{ allocator }
{

}

template <typename T, typename Allocator>
void ObjectPool<T, Allocator>::addChunk() {
    std::cout << "Allocating new chunk..." << std::endl;

    // Allocate a new chunk of uninitialized memory big enough to hold
    // m_newChunkSize instances of T, and add the chunk to the pool.
    auto* firstNewObject { m_allocator.allocate(m_newChunkSize) };
    m_pool.push_back(firstNewObject);

    //Create pointers to each individual object in the new chunk
    //and store them in the list of free objects.
    auto oldFreeObjectsSize { m_freeObjects.size() };
    m_freeObjects.resize(oldFreeObjectsSize + m_newChunkSize);
    std::iota(begin(m_freeObjects) + oldFreeObjectsSize, end(m_freeObjects), firstNewObject);

    //Double the chunk size for the next time.
    m_newChunkSize *= 2;
}

template <typename T, typename Allocator>
template <typename... Args>
std::shared_ptr<T> ObjectPool<T, Allocator>::acquireObject(Args ...args) {
    //If there are no free objects, allocate a new chunk.
    if(m_freeObjects.empty()) { addChunk(); }

    //Get a free object.
    T* object { m_freeObjects.back() };

    //Initialize, i.e. construct, an instance T in an
    //uninitialized block of memory using place new, and
    //perfectly forward any provided arguments to the constructor.
    new(object) T { std::forward<Args>(args)...};

    //Remove the object from the list of free objects.
    m_freeObjects.pop_back();

    //Wrap the initialized object and return it.
    return std::shared_ptr<T> { object, [this](T* object) {
        //Destroy object
        std::destroy_at(object);
        //Put the object back in the list of free objects.
        m_freeObjects.push_back(object);
    }};
}

template<typename T, typename Allocator>
ObjectPool<T, Allocator>::~ObjectPool() {
    assert(m_freeObjects.size() == ms_initialChunkSize * (std::pow(2, m_pool.size()) - 1));

    size_t chunkSize { ms_initialChunkSize };

    for(auto* chunk: m_pool) {
        m_allocator.deallocate(chunk, chunkSize);
        chunkSize *= 2;
    }

    m_pool.clear();
}

///////////////////////////////////////////////


class ExpensiveObject {
public:
    ExpensiveObject() {}
    virtual ~ExpensiveObject() = default;

private:
    std::array<double, 4 * 1024 * 1024> m_data;
};

using MyPool = ObjectPool<ExpensiveObject>;

std::shared_ptr<ExpensiveObject> getExpensiveObject(MyPool& pool)
{
    auto object { pool.acquireObject() };

    return object;
}

int main() {
    const size_t NumberOfIterations { 500'000 };

    std::cout << "Starting loop using pool..." << std::endl;
    MyPool requestPool;

    auto start1 { std::chrono::steady_clock::now() };

    for(size_t i { 0 }; i < NumberOfIterations; ++i) {
        auto object {getExpensiveObject(requestPool)};
    }

    auto end1 { std::chrono::steady_clock::now()};
    auto diff1 { end1 - start1 };

    std::cout << std::format("{}ms\n", std::chrono::duration<double, std::milli>(diff1).count());

    std::cout << "Starting loop using new/delete..." << std::endl;

    auto start2 { std::chrono::steady_clock::now() };

    for(size_t i { 0 }; i < NumberOfIterations; ++i)
    {
        auto object { new ExpensiveObject{} };
        delete object;
        object = nullptr;
    }

    auto end2 { std::chrono::steady_clock::now() };
    auto diff2 { end2 - start2 };

    std::cout << std::format("{}ms\n", std::chrono::duration<double, std::milli>(diff2).count());

}


