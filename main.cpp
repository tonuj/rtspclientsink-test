#include "gst.h"

int main(int argc, char *argv[])
{
    std::unique_ptr<GST> gst(new GST);

    gst->run(argc, argv);

    return 0;
}
