#include <gflags/gflags.h>
#include <glog/logging.h>

#include <filesystem>

int main(int argc, char **argv) {
    ::google::InitGoogleLogging(argv[0]);
    ::gflags::ParseCommandLineFlags(&argc, &argv, true);
    return 0;
}