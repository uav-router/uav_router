#include <iostream>
#include <iomanip>
#include <algorithm>
#include <iterator>
 
// Print contents of an array in C++ using std::ostream_iterator

int test(int* arr, int len) {
    
    for (int i = len; i; i--) 
        std::cout<<std::hex<<std::setw(2)<<std::setfill('0')<<*arr++<<" ";
    std::cout<<std::endl;
    return 0;
}

int main()
{
    int input[] = { 11, 12, 13, 14, 15 };
    return test(input, 5);
}