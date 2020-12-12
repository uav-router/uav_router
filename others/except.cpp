#include <iostream>
#include <system_error>
#include <netdb.h>

class eai_category_impl:
    public std::error_category
{
public:
    virtual const char * name() const noexcept {
        return "addrinfo";
    }
    virtual std::string message(int ev) const {
        return gai_strerror(ev);
    }
};

eai_category_impl eai_category_instance;

const std::error_category & eai_category()
{
    return eai_category_instance;
}

int main()
{
    try
    {
        throw std::system_error(EAI_SYSTEM, eai_category(), "hello world");
    }
    catch (const std::system_error& ex)
    {
        std::cout << ex.code() << '\n';
        std::cout << ex.code().message() << '\n';
        std::cout << ex.what() << '\n';
        //std::cout << ex << std::endl;
    }
}