#ifndef GNN_BUFFER_CONSUMER_H_
#define GNN_BUFFER_CONSUMER_H_

namespace GNN {

/**
 * @class BufferConsumer
 * @brief 一个抽象接口，允许模块（如ComputeModule）被DmaBuffer通知。
 * * 实现了这个接口的模块可以注册到DmaBuffer，
 * 当DmaBuffer的某个缓冲区准备好数据时，它会调用onDataReady方法。
 */
class BufferConsumer {
public:
    virtual ~BufferConsumer() = default;

    /**
     * @brief DmaBuffer调用的回调函数，用于通知数据已准备好。
     * @param bank_id 哪个bank的数据已准备就绪。
     */
    virtual void onDataReady(int bank_id) = 0;
};

} // namespace GNN

#endif // GNN_BUFFER_CONSUMER_H_
