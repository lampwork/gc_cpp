#ifndef _GC_OBJ_HPP__
#define _GC_OBJ_HPP__
#pragma once

#include <unordered_set>
#include <mutex>

namespace gc
{
    class Allocator;
    template <typename T>
    class Obj;
    template <typename T>
    class ObjRef;

    struct ObjBase
    {
    public:
        struct Hash
        {
            size_t operator()(const ObjBase &o) const
            {
                return std::hash<void *>{}((void *)&o.buf);
            }
        };

    public:
        /* gc support */
        friend class Allocator;
        Allocator *pa;
        bool mark = false;

    public:
        virtual ~ObjBase() {}

    protected:
        char buf[0];
    };

    template <typename T>
    class Obj : ObjBase
    {
        // can only access by proxy
    private:
        friend class Allocator;
        friend class ObjRef<T>;

        T &operator*() const { return *(T *)&buf; }
        T *operator->() const { return (T *)&buf; }

        template <typename... Args>
        Obj(Args &&...a)
        {
            std::cout << "Obj<T>::Obj(Args &&...)" << std::endl;
            new (&buf) T{std::forward<Args>(a)...};
        }
        virtual ~Obj() override { ((T *)&buf)->~T(); }

        Obj(const Obj &) = delete;
        Obj(Obj &&) = delete;
        Obj &operator=(const Obj &) = delete;
        Obj &operator=(Obj &&) = delete;

        static void *operator new(size_t size)
        {
            std::cout << "Obj<T>::operator new: " << sizeof(Obj) + sizeof(T) << std::endl;
            return malloc(sizeof(Obj) + sizeof(T));
        }

        static void operator delete(void *ptr, size_t size)
        {
            std::cout << "Obj<T>::operator delete: Obj(" << sizeof(Obj) << ") + (" << sizeof(T) << ")" << std::endl;
            free(ptr);
        }

    private:
        Obj() = default;
    };

    struct ObjRefBase
    {
        struct Hash
        {
            size_t operator()(const ObjRefBase &o)
            {
                return std::hash<void *>{}(o.ptr);
            }
        };

    protected:
        ObjRefBase(ObjBase *p, Allocator *pa) : pa(pa), ptr(p) {}

    public:
        /* gc support */
        friend class Allocator;
        Allocator *pa;

    protected:
        ObjBase *ptr = nullptr;
    };

    template <typename T>
    struct ObjRef : ObjRefBase
    {
    private:
        friend class Allocator;

    public:
        ObjRef(Obj<T> *p, Allocator *pa);
        ~ObjRef();
        ObjRef &operator=(Obj<T> &obj);
        ObjRef &operator=(const ObjRef<T> &ref);
        ObjRef &operator=(const ObjRef<decltype(nullptr)> &null);
        T &operator*() const { return ((Obj<T> *)ptr)->operator*(); }
        T *operator->() const { return ((Obj<T> *)ptr)->operator->(); }
    };

    template <>
    struct ObjRef<decltype(nullptr)>
    {
        template <typename T>
        operator ObjRef<T>() { return {nullptr, nullptr}; }
    };
    __attribute__((__unused__)) static ObjRef<decltype(nullptr)> nullref;

    class Allocator
    {
    public:
        Allocator();
        ~Allocator();

    public:
        void GC();

    public:
        template <typename T, typename... Args>
        ObjRef<T> New(Args &&...args); // new a obj
    private:
        template <typename T>
        friend class ObjRef;

    private:
        std::mutex mtx; // protect `objs` and `refs`
        std::unordered_set<ObjBase *> objs;
        std::unordered_set<ObjRefBase *> refs;
    };

    Allocator::Allocator() {}
    Allocator::~Allocator()
    {
        std::unique_lock<std::mutex> lk(mtx);
    }

    template <typename T, typename... Args>
    ObjRef<T> Allocator::New(Args &&...args)
    {
        Obj<T> *ret = new Obj<T>{std::forward<Args>(args)...};
        decltype(this->objs.insert(ret)) pair;
        ret->pa = this;
        {
            std::unique_lock<std::mutex> lk(mtx);
            pair = this->objs.insert(ret);
        }
        // std::cout << "New: @ " << &ret->buf << std::endl;
        // std::cout << "New: ret " << ret << std::endl;
        // std::cout << "ret: " << ((T *)&ret->buf)->d << std::endl;
        if (pair.second)
        {
            return {ret, this};
        }
        else
        {
            return nullref;
        }
    }

    void Allocator::GC()
    {
        if (objs.empty())
        {
            return;
        }
        std::unique_lock<std::mutex> lk(mtx);
        for (auto &obj : objs)
        {
            obj->mark = false;
        }
        for (auto ref : refs)
        {
            ref->ptr->mark = true;
        }
        for (auto itr = objs.begin(); itr != objs.end();)
        {
            auto pobj = *itr;
            if (!pobj->mark)
            {
                delete pobj;
                objs.erase(itr++);
            }
            else
            {
                ++itr;
            }
        }
    }

    template <typename T>
    ObjRef<T>::ObjRef(Obj<T> *p, Allocator *pa) : ObjRefBase(p, pa)
    {
        std::unique_lock<std::mutex> lk(pa->mtx);
        pa->refs.insert(this);
    }
    template <typename T>
    ObjRef<T>::~ObjRef()
    {
        if (pa && ptr)
        {
            std::unique_lock<std::mutex> lk(pa->mtx);
            pa->refs.erase(this);
        }
    }
    template <typename T>
    ObjRef<T> &ObjRef<T>::operator=(Obj<T> &obj)
    {
        if (pa != obj.pa)
        {
            std::unique_lock<std::mutex> lk(pa->mtx);
            pa->refs.erase(this);
            pa = obj.pa;
            pa->refs.insert(this);
        }
        ptr = &obj;
        return *this;
    }
    template <typename T>
    ObjRef<T> &ObjRef<T>::operator=(const ObjRef<T> &ref)
    {
        if (pa != ref.pa)
        {
            std::unique_lock<std::mutex> lk(pa->mtx);
            pa->refs.erase(this);
            pa = ref.pa;
            pa->refs.insert(this);
        }
        ptr = ref.ptr;
        return *this;
    }
    template <typename T>
    ObjRef<T> &ObjRef<T>::operator=(const ObjRef<decltype(nullptr)> &null)
    {
        {
            std::unique_lock<std::mutex> lk(pa->mtx);
            pa->refs.erase(this);
        }
        ptr = nullptr;
        return *this;
    }
}

#endif
