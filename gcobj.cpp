#include <memory>
#include <iostream>
#include <pthread.h>
#include <thread>
#include <unistd.h>

#include "gcobj.hpp"

gc::Allocator *pga;
std::thread gc_thd;

struct Type
{
    Type(int i, double d) : i(i), d(d)
    {
        std::cout << "Type(int, double): " << this->i << ", " << this->d << std::endl;
    }
    ~Type()
    {
        std::cout << "~Type(): {" << i << ", " << d << "}" << std::endl;
    }
    friend std::ostream &operator<<(std::ostream &out, const Type &t)
    {
        return out << "Type{i: " << t.i << ", " << t.d << "}" << std::endl;
    }
    int i;
    double d;
};

struct Type2
{

    ~Type2()
    {
        std::cout << "~Type2(): {" << *type1 << ", " << *type2
                  << ", " << i << ", " << d << "}" << std::endl;
    }

    gc::ObjRef<Type> type1;
    gc::ObjRef<Type> type2;
    int i;
    double d;
};

void gc_init()
{
    pga = new gc::Allocator;
    gc_thd = std::thread([]() {
        while(1) {
            pga->GC();
        } 
    });
}

void normal_st_test()
{
    auto &ga = *pga;
    auto tp = ga.New<Type>(1, 2.);
    std::cout << *tp << std::endl;
    tp = ga.New<Type>(2, 1.1);
    std::cout << *tp << std::endl;
}

void st_member_test()
{
    auto &ga = *pga;
    auto tp2 = ga.New<Type2>();
    tp2->d = 3.14;
    tp2->i = 2;
    tp2->type1 = ga.New<Type>(1, 1.24);
    tp2->type2 = ga.New<Type>(4, 4.24);
}

int main()
{
    gc_init();
    normal_st_test();
    st_member_test();
    std::this_thread::sleep_for(std::chrono::seconds(3));
}
