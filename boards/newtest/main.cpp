#include <hackflight.hpp>
#include "mysimboard.hpp"

int main()
{
    hf::Hackflight h;
    h.init(new hf::SimBoard());

    printf("read\n");

    while (true) {

    }
}
