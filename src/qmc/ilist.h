#pragma once

#include <iterator>

#include "util/common.h"

namespace dbt
{
struct IListNodeBase {
    IListNodeBase *getPrev() const { return nprev; }
    IListNodeBase *getNext() const { return nnext; }
    void setPrev(IListNodeBase *n) { nprev = n; }
    void setNext(IListNodeBase *n) { nnext = n; }

private:
    IListNodeBase *nprev{}, *nnext{};
};

struct IListBase {
    static void insertBefore(IListNodeBase *next, IListNodeBase *node)
    {
        auto prev = next->getPrev();
        node->setNext(next);
        node->setPrev(prev);
        prev->setNext(node);
        next->setPrev(node);
    }

    static void remove(IListNodeBase *node)
    {
        auto prev = node->getPrev();
        auto next = node->getNext();
        next->setPrev(prev);
        prev->setNext(next);
    }
};

template <typename T>
struct IListSentinel;

template <typename T>
struct IListIterator;

template <typename T>
struct IListNode : IListNodeBase {
    friend struct IListSentinel<T>;
    friend struct IListIterator<T>;

    IListIterator<T> getIter() { return IListIterator<T>(*this); }

private:
    IListNode *getPrev() const
    {
        return static_cast<IListNode *>(IListNodeBase::getPrev());
    }
    IListNode *getNext() const
    {
        return static_cast<IListNode *>(IListNodeBase::getNext());
    }
    void setPrev(IListNode *n) { IListNodeBase::setPrev(n); }
    void setNext(IListNode *n) { IListNodeBase::setNext(n); }
};

template <typename T>
struct IListSentinel : IListNode<T> {
    IListSentinel() : IListNode<T>() { reset(); }
    void reset()
    {
        IListNode<T>::setPrev(this);
        IListNode<T>::setNext(this);
    }
    bool empty() const { return IListNode<T>::getPrev() == this; }
};

template <typename T>
struct IListIterator {
    using iterator_category = std::bidirectional_iterator_tag;
    using value_type = T;
    using difference_type = uintptr_t;
    using pointer = T *;
    using reference = T &;

    DEFAULT_COPY(IListIterator)
    DEFAULT_MOVE(IListIterator)
    ~IListIterator() = default;

    IListIterator(IListNode<T> &n) : pos(&n) {}
    IListIterator(T *n) : pos(n) {}

    bool operator==(IListIterator const &rhs) const { return pos == rhs.pos; }
    bool operator!=(IListIterator const &rhs) const { return pos != rhs.pos; }

    T &operator*() const { return *static_cast<T *>(pos); }
    T *operator->() const { return &operator*(); }

    IListIterator &operator++()
    {
        pos = pos->getNext();
        return *this;
    }
    IListIterator &operator--()
    {
        pos = pos->getPrev();
        return *this;
    }
    IListIterator operator++(int)
    {
        auto old = *this;
        ++*this;
        return old;
    }
    IListIterator operator--(int)
    {
        auto old = *this;
        --*this;
        return old;
    }

    auto getIListNode() const { return pos; }

private:
    IListNode<T> *pos{};
};

template <typename T>
struct IList : IListBase {
    using value_type = T;
    using reference = T &;
    using pointer = T *;
    using iterator = IListIterator<T>;
    using difference_type = ptrdiff_t;
    using size_type = size_t;

    IList() = default;
    ~IList() = default;

    NO_COPY(IList)
    NO_MOVE(IList)

    bool empty() const { return sentinel.empty(); }

    void insert(iterator it, pointer n)
    {
        IListBase::insertBefore(it.getIListNode(), n);
    }
    void insert(iterator it, reference n) { insert(it, &n); }
    void push_back(pointer n) { insert(end(), n); }
    void push_back(reference n) { insert(end(), &n); }

    void remove(reference n) { IListBase::remove(&n); }
    iterator erase(iterator it)
    {
        assert(end() != it);
        remove(*it++);
        return it;
    }

    iterator begin() { return ++iterator(sentinel); }
    iterator end() { return iterator(sentinel); }

private:
    IListSentinel<T> sentinel;
};

}  // namespace dbt
