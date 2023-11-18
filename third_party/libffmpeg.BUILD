cc_library(
    name = "libffmpeg_macos",
    srcs = glob(
        [
            "local/lib/libav*.dylib",
        ],
    ),
    hdrs = glob(["local/include/libav*/*.h"]),
    includes = ["local/include/"],
    linkopts = [
        "-lavcodec",
        "-lavformat",
        "-lavutil",
    ],
    linkstatic = 1,
    visibility = ["//visibility:public"],
)

cc_library(
    name = "libffmpeg_linux",
    srcs = glob(
        [
            "lib/libav*.so",
        ],
    ),
    hdrs = glob(["include/libav*/*.h"]),
    includes = ["include"],
    linkopts = [
        "-lavcodec",
        "-lavformat",
        "-lavutil",
    ],
    linkstatic = 1,
    visibility = ["//visibility:public"],
)
